#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

enum
{
	Mhz = 1000*1000,
	Pwmsrcclk = 25*Mhz,
	Ctl = 1,

	PWMSAR = 0x0C/4,
	PWMPR = 0x10/4,
};

static u32int *pwm2;
static char *uid = "mntpm";

static void
wr(u32int *base, int reg, u32int v)
{
	base[reg] = v;
}

static u32int
rd(u32int *base, int reg)
{
	return base[reg];
}

static void
setbrightness(int p)
{
	u32int v;

	if(p < 0)
		p = 0;
	if(p > 100)
		p = 100;

	v = Pwmsrcclk / rd(pwm2, PWMSAR);
	wr(pwm2, PWMPR, (Pwmsrcclk/(v*p/100))-2);
}

static void
fsread(Req *r)
{
	u32int m, v;
	char msg[256];

	if(r->fid->file->aux == (void*)Ctl){
		m = Pwmsrcclk / rd(pwm2, PWMSAR);
		v = Pwmsrcclk / (rd(pwm2, PWMPR)+2);
		snprint(msg, sizeof(msg), "brightness %d\n", v*100/m);
		readstr(r, msg);
	}
	respond(r, nil);
}

static void
fswrite(Req *r)
{
	char msg[256], *f[4];
	int nf;

	if(r->fid->file->aux == (void*)Ctl){
		snprint(msg, sizeof(msg), "%.*s",
			utfnlen((char*)r->ifcall.data, r->ifcall.count), (char*)r->ifcall.data);
		nf = tokenize(msg, f, nelem(f));
		if(nf < 2){
			respond(r, "invalid ctl message");
			return;
		}
		if(strcmp(f[0], "brightness") == 0)
			setbrightness(atoi(f[1]));
	}

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
	fprint(2, "usage: aux/imx8pm [-D] [-m /mnt/pm] [-s service]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *mtpt, *srv;

	mtpt = "/mnt/pm";
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

	if((pwm2 = segattach(0, "pwm2", 0, 0x18)) == (void*)-1)
		sysfatal("pwm2");

	fs.tree = alloctree(uid, uid, DMDIR|0555, nil);
	createfile(fs.tree->root, "ctl", uid, 0666, (void*)Ctl);
	postmountsrv(&fs, srv, mtpt, MREPL);

	exits(nil);
}
