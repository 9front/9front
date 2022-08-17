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
	Spk,
	Hp,
	Nout,

	Ctl = 1,
	Vol,
};

struct Out
{
	char *name;
	int volreg;
	int volmax;
	void (*toggle)(Out *o, int on);
	int on;
	int vol[2];
};

static char *uid = "audio";
static int data, reg1a;

static void
wr(int a, int v)
{
	u8int c;

	//fprint(2, "[0x%x] ← 0x%ux = ", a, v & 0x1ff);
	c = v & 0xff;
	pwrite(data, &c, 1, a<<1 | ((v>>8)&1));
}

static void
dactoggle(Out *o, int on)
{
	if(o->on = on)
		reg1a |= 3<<7;
	else
		reg1a &= ~(3<<7);
	wr(0x1a, reg1a);
}

static void
spktoggle(Out *o, int on)
{
	if(o->on = on)
		reg1a |= 3<<3;
	else
		reg1a &= ~(3<<3);
	wr(0x31, (on ? 3 : 0)<<6); /* class D SPK out */
	wr(0x1a, reg1a);
}

static void
hptoggle(Out *o, int on)
{
	if(o->on = on)
		reg1a |= 3<<5;
	else
		reg1a &= ~(3<<5);
	wr(0x1a, reg1a);
}

static Out out[Nout] =
{
	[Dac] = {"master", 0x0a, 0xff, dactoggle, 0},
	[Spk] = {"spk", 0x28, 0x7f, spktoggle, 0},
	[Hp] = {"hp", 0x02, 0x7f, hptoggle, 0},
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
	wr(o->volreg+1, 0<<8 | zc<<7 | r);
	wr(o->volreg+1, 1<<8 | zc<<7 | r);
}

static void
reset(void)
{
	Out *o;
	int i;

	wr(0x0f, 0); /* reset registers to default */
	wr(0x04, 0); /* sysclk (div 1) derived from mclk; dacdiv=sysclk/256 */
	wr(0x05, 0<<3); /* unmute DAC */
	wr(0x06, 1<<3 | 1<<2); /* ramp up DAC volume slowly */
	wr(0x07, 2); /* i²s, 16-bit words, slave mode */
	wr(0x08, 7<<6); /* class D divider: sysclk/16 */
	wr(0x19, 1<<7 | 1<<6); /* Vmid = playback, VREF on */
	wr(0x22, 1<<8); /* L DAC to mixer */
	wr(0x25, 1<<8); /* R DAC to mixer */
	wr(0x2f, 3<<2); /* output mixer on */
	wr(0x30, 1<<1); /* Tsense on */
	wr(0x33, 5<<0); /* +5.1dB AC SPK boost - Reform's speakers can be too quiet */

	/* sensible defaults */
	setvol(&out[Dac], 100, 100);
	setvol(&out[Spk], 80, 80);
	setvol(&out[Hp], 65, 65);

	/* enable every output and let the user decide later */
	for(i = 0, o = out; i < Nout; i++, o++)
		o->toggle(o, 1);
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
		seprint(s, e, "speed 44100\n");
	}

	readstr(r, msg);
	respond(r, nil);
}

static void
fswrite(Req *r)
{
	int nf, on, i, vl, vr;
	char msg[256], *f[4];
	Out *o;

	snprint(msg, sizeof(msg), "%.*s",
		utfnlen((char*)r->ifcall.data, r->ifcall.count), (char*)r->ifcall.data);
	if((nf = tokenize(msg, f, nelem(f))) < 2){
		if(nf == 1 && strcmp(f[0], "reset") == 0){
			reset();
			goto Done;
		}
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
		o->toggle(o, on);
	}else if(r->fid->file->aux == (void*)Vol){
		vl = atoi(f[1]);
		vr = nf < 3 ? vl : atoi(f[2]);
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
	fprint(2, "usage: aux/wm8960\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *mtpt, *srv;
	int ctl;

	mtpt = "/mnt/wm8960";
	srv = nil;
	ARGBEGIN{
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

	fs.tree = alloctree(uid, uid, DMDIR|0555, nil);
	createfile(fs.tree->root, "audioctl", uid, 0666, (void*)Ctl);
	createfile(fs.tree->root, "volume", uid, 0666, (void*)Vol);

	postmountsrv(&fs, srv, mtpt, MREPL);

	exits(nil);
}
