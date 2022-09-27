#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

#define MIN(a,b) (a<b?a:b)
#define MAX(a,b) (a>b?a:b)
#define CLAMP(x,a,b) MAX(a,MIN(x,b))

typedef struct Out Out;

enum
{
	Dac,
	Hp,
	Spk,
	Nout,

	Ctl = 1,
	Vol,
};

struct Out
{
	char *name;
	int volreg;
	int volmax;
	int togglemask;
	void (*toggle)(Out *o, int on);
	int on;
	int vol[2];
};

static char *uid = "audio";
static int data;
static int reg1a;
static int rate = 44100;
static int ⅓d;

static void
wr(int a, int v)
{
	u8int c;

	//fprint(2, "[0x%x] ← 0x%ux\n", a, v & 0x1ff);
	c = v & 0xff;
	if(pwrite(data, &c, 1, a<<1 | ((v>>8)&1)) < 1)
		fprint(2, "reg %x write failed: %r\n", a);
}

static void
dactoggle(Out *, int on)
{
	wr(0x05, (!on)<<3);
}

static void
classdspk(Out *, int on)
{
	wr(0x31, (on ? 3 : 0)<<6 | 0x37); /* class D SPK out */
}

static void
toggle(Out *o, int on)
{
	if(on)
		reg1a |= o->togglemask;
	else
		reg1a &= ~o->togglemask;
	wr(0x1a, reg1a);
	if(o->toggle != nil)
		o->toggle(o, on);
	o->on = on;
}

static Out out[Nout] =
{
	[Dac] = {"master", 0x0a, 0xff, 0, dactoggle, 0},
	[Hp] = {"hp", 0x02, 0x7f, 3<<5, nil, 0},
	[Spk] = {"spk", 0x28, 0x7f, 3<<3, classdspk, 0},
};

static void
setvol(Out *o, int l, int r)
{
	int zc;

	o->vol[0] = l = CLAMP(l, 0, 100);
	o->vol[1] = r = CLAMP(r, 0, 100);
	if(l > 0)
		l += o->volmax - 100;
	if(r > 0)
		r += o->volmax - 100;

	zc = o->volmax < 0x80;
	wr(o->volreg+0, 0<<8 | zc<<7 | l);
	wr(o->volreg+1, 1<<8 | zc<<7 | r);
}

static void
set3d(int x)
{
	⅓d = CLAMP(x, 0, 100);
	x = (⅓d+5)/7;
	wr(0x10, x<<1 | (x ? 1 : 0)<<0);
}

static int
setrate(int s)
{
	u32int k;

	if(s != 44100 && s != 48000)
		return -1;

	/*
	 * getting DAC ready for s16c2r44100:
	 *
	 * f₁ = mclk₀ = 25Mhz (set in sai)
	 * pllprescale = /2 → *actual* mclk₁ is 25/2 = 12.5Mhz
	 * sysclk = 44.1kHz*256 = 11.2896Mhz
	 *   → dacdiv = /(1*256) = sysclk/(1*256) = 44.1kHz
	 * f₂ = 4*2*sysclk = 90.3168Mhz
	 *
	 * PLL freq:
	 *   R = f₂/f₁
	 *   N = int(R) = 7
	 *   K = 2²⁴*(R-N) = 3780644.9623
	 *
	 * dacdiv = /(1*256) → DAC at max rate
	 *  → pick bclk rate 1.4112Mhz (sysclk/8)
	 *  → bclkdiv = /8
	 *
	 * class D clk needs to be ~768kHz (700-800)
	 *  → sysclk/768000 = 14
	 *  → dclkdiv = /16 → dclk = 705.6kHz
	 */
	wr(0x1a, reg1a = reg1a & ~(1<<0)); /* disable pll */

	wr(0x04,
		0<<3 | /* dacdiv → sysclk/(1*256) = 44100 */
		2<<1 | /* sysclkdiv → /2 */
		1<<0 | /* clksel → pll output */
		0
	);
	wr(0x34,
		1<<5 | /* enable fractional mode */
		1<<4 | /* pllprescale */
		7<<0 | /* N */
		0
	);
	k = s == 44100 ? 3780645 : 14500883; /* K */
	wr(0x35, (k>>16) & 0xff);
	wr(0x36, (k>>8) & 0xff);
	wr(0x37, k & 0xff);

	wr(0x08, 7<<6 | 7<<0); /* dclkdiv → sysclk/16; bclkdiv → sysclk/8 */
	wr(0x1a, reg1a = reg1a | 1<<0); /* enable pll */

	rate = s;

	return 0;
}

static void
reset(void)
{
	int i;

	for(i = 0; i < Nout; i++){
		out[i].vol[0] = -1;
		out[i].vol[1] = -1;
	}

	toggle(out+Dac, 0);
	wr(0x1c, 1<<7 | 1<<4 | 1<<3 | 1<<2); /* Vmid/r bias; Vgs/r on; Vmid soft start */
	wr(0x19, 0); /* power down */
	sleep(200);
	wr(0x0f, 0); /* reset registers to default */

	setrate(rate);
	set3d(⅓d);

	wr(0x07, 1<<6 | 0<<2 | 2<<0); /* master mode; 16 bits; i²s */

	wr(0x06, 1<<3 | 0<<2); /* soft mute; ramp up DAC volume fast */
	wr(0x2f, 3<<2); /* output mixer on */
	wr(0x22, 1<<8); /* L DAC to mixer */
	wr(0x25, 1<<8); /* R DAC to mixer */

	wr(0x17, 1<<8 | 3<<6 | 1<<1 | 1<<0); /* thermal shutdown on; avdd=3.3v; faster response; slow clock on */
	wr(0x1c, 1<<7 | 1<<4 | 1<<3 | 1<<2); /* Vmid/r bias; Vgs/r on; Vmid soft start */
	wr(0x19, 1<<7); /* start Vmid (playback) */
	sleep(650);
	wr(0x1c, 1<<3); /* done with anti-pop */
	wr(0x19, 1<<7 | 1<<6); /* Vref on */

	wr(0x09, 1<<6); /* ADCLRC → gpio (for jack detect output) */
	wr(0x30, 3<<4 | 2<<2 | 1<<1); /* gpio jack detect out; JD2 jack detect in; Tsense on */
	wr(0x1b, 1<<6 | 0<<3 | 0<<0); /* 20kΩ; capless mode disabled; 44.1/48kHz */
	wr(0x18, 1<<6 | 0<<5); /* HP switch on; high = HP */

	/* turn on all outputs */
	toggle(out+Hp, 1);
	toggle(out+Spk, 1);

	/* enable/unmute DAC */
	reg1a |= 3<<7;
	wr(0x1a, reg1a); 
	toggle(out+Dac, 1);

	/* sensible defaults */
	setvol(out+Spk, 100, 100);
	setvol(out+Hp, 75, 75);
	setvol(out+Dac, 80, 80);
}

static void
fsread(Req *r)
{
	char msg[256], *s, *e;
	Out *o;
	int i;

	s = msg;
	e = msg+sizeof(msg);
	*s = 0;
	if(r->fid->file->aux == (void*)Ctl){
		for(i = 0, o = out; i < Nout; i++, o++)
			s = seprint(s, e, "%s %s\n", o->name, o->on ? "on" : "off");
	}else if(r->fid->file->aux == (void*)Vol){
		for(i = 0, o = out; i < Nout; i++, o++)
			s = seprint(s, e, "%s %d %d\n", o->name, o->vol[0], o->vol[1]);
		s = seprint(s, e, "speed %d\n", rate);
		seprint(s, e, "3d %d\n", ⅓d);
	}

	readstr(r, msg);
	respond(r, nil);
}

static int
setoradd(int x, char *s)
{
	int d;

	d = atoi(s);
	if(*s == '+' || *s == '-')
		return x + d;

	return d;
}

static void
fswrite(Req *r)
{
	int nf, on, i, vl, vr;
	char msg[256], *f[4];
	Out *o;

	snprint(msg, sizeof(msg), "%.*s",
		utfnlen((char*)r->ifcall.data, r->ifcall.count), (char*)r->ifcall.data);
	nf = tokenize(msg, f, nelem(f));
	if(nf == 1 && strcmp(f[0], "reset") == 0){
		reset();
		goto Done;
	}else if(nf == 2 && strcmp(f[0], "speed") == 0){
		if(setrate(atoi(f[1])) != 0){
			respond(r, "not supported");
			return;
		}
		goto Done;
	}else if(nf == 2 && strcmp(f[0], "3d") == 0){
		set3d(atoi(f[1]));
		goto Done;
	}
	if(nf < 2){
Emsg:
		respond(r, "invalid ctl message");
		return;
	}

	for(i = 0, o = out; i < Nout && strcmp(f[0], o->name) != 0; i++, o++)
		;
	if(i >= Nout)
		goto Emsg;

	if(r->fid->file->aux == (void*)Ctl){
		if(nf != 2)
			goto Emsg;
		if(strcmp(f[1], "on") == 0)
			on = 1;
		else if(strcmp(f[1], "off") == 0)
			on = 0;
		else if(strcmp(f[1], "toggle") == 0)
			on = !o->on;
		else
			goto Emsg;
		toggle(o, on);
	}else if(r->fid->file->aux == (void*)Vol){
		vl = setoradd(o->vol[0], f[1]);
		vr = setoradd(o->vol[1], nf < 3 ? f[1] : f[2]);
		setvol(o, vl, vr);
	}

Done:
	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

static Srv fs = {
	.read = fsread,
	.write = fswrite,
};

static void
usage(void)
{
	fprint(2, "usage: %s [-1] [-D] [-m /dev] [-s service]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *mtpt, *srv;
	int ctl, oneshot;

	mtpt = "/dev";
	srv = nil;
	oneshot = 0;
	ARGBEGIN{
	case '1':
		oneshot = 1;
		break;
	case 'D':
		chatty9p = 1;
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	case 's':
		srv = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if((data = open("#J/i2c3/i2c.1a.data", OWRITE)) < 0)
		sysfatal("i2c data: %r");
	if((ctl = open("#J/i2c3/i2c.1a.ctl", OWRITE)) < 0)
		sysfatal("i2c ctl: %r");
	fprint(ctl, "subaddress 1\n");
	fprint(ctl, "size %d\n", 0x38<<1);
	close(ctl);

	reset();

	if(oneshot)
		exits(nil);

	fs.tree = alloctree(uid, uid, DMDIR|0555, nil);
	createfile(fs.tree->root, "audioctl", uid, 0666, (void*)Ctl);
	createfile(fs.tree->root, "volume", uid, 0666, (void*)Vol);
	/* have to mount -b to shadow sai's useless files */
	postmountsrv(&fs, srv, mtpt, MBEFORE);

	exits(nil);
}
