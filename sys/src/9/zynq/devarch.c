#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

enum {
	Qdir = 0,
	Qtemp,
	Qpl,
	Qfbctl,
	Qbase,

	Qmax = 16,
};

static Dirtab archdir[Qmax] = {
	".",		{ Qdir, 0, QTDIR },	0,	0555,
	"temp",		{ Qtemp, 0},		0,	0440,
	"pl",		{ Qpl, 0 }, 		0,	0660,
	"fbctl",	{ Qfbctl, 0 }, 		0,	0660,
};
static int narchdir = Qbase;

static int temp = -128;
static ulong *devc;
static int dmadone;
enum { PLBUFSIZ = 8192 };
static uchar *plbuf;
static Rendez plinitr, pldoner, pldmar;
static QLock plrlock, plwlock;
static Ref plwopen;
static Physseg *axi;

enum {
	DEVCTRL = 0,
	DEVISTS = 0xc/4,
	DEVMASK,
	DEVSTS,
	DMASRC = 0x18/4,
	DMADST,
	DMASRCL,
	DMADSTL,
	XADCCFG = 0x100/4,
	XADCSTS,
	XADCMASK,
	XADCMSTS,
	XADCCMD,
	XADCREAD,
	XADCMCTL,
	
	FPGA0_CLK_CTRL = 0x170/4,
};

enum {
	PROG = 1<<30,
	DONE = 1<<2,
	INITPE = 1<<1,
	INIT = 1<<4,
	DMADONE = 1<<13,
};

static void
scram(void)
{
	splhi();
	slcr[0x100/4] |= 1<<4;
	slcr[0x104/4] |= 1<<4;
	slcr[0x108/4] |= 1<<4;
	slcr[DEVCTRL] &= ~PROG;
	slcr[0x244/4] = 1<<4|1<<5;
}

static void
xadcirq(Ureg *, void *)
{
	int v;
	static int al, notfirst;
	
	while((devc[XADCMSTS] & 1<<8) == 0){
		v = ((u16int)devc[XADCREAD]) >> 4;
		if(v == 0){
			if(notfirst)
				print("temperature sensor reads 0, shouldn't happen\n");
			break;
		}
		notfirst = 1;
		temp = v * 5040 / 4096 - 2732;
		if(temp >= 800){
			if(al == 0)
				print("temperature exceeds 80 deg C\n");
			al = 1;
		}
		if(temp <= 750)
			al = 0;
		if(temp >= 900){
			print("chip temperature exceeds 90 deg C, shutting down");
			scram();
		}
	}
	devc[XADCSTS] = -1;
}

static void
xadctimer(void)
{
	devc[XADCCMD] = 1<<26 | 0<<16;
}

static void
xadcinit(void)
{
	int i;
	int x;

	devc = vmap(DEVC_BASE, 0x11C);
	devc[XADCMCTL] |= 1<<4;
	devc[XADCMCTL] &= ~(1<<4);
	devc[XADCCMD] = 0x08030000;
	for(i = 0; i < 15; i++)
		devc[XADCCMD] = 0;
	while((devc[XADCMSTS] & 1<<10) == 0)
		;
	while((devc[XADCMSTS] & 1<<8) == 0){
		x = devc[XADCREAD];
		USED(x);
	}
	devc[XADCCFG] = 0x80001114;
	devc[XADCMASK] = ~(1<<8);
	devc[XADCSTS] = -1;
	intrenable(XADCIRQ, xadcirq, nil, LEVEL, "xadc");
	addclock0link(xadctimer, XADCINTERVAL);
}

static int
isplinit(void *)
{
	return devc[DEVSTS] & INIT;
}

static int
ispldone(void *)
{
	return devc[DEVISTS] & DONE;
}

static int
isdmadone(void *)
{
	return dmadone;
}

static void
plirq(Ureg *, void *)
{
	ulong fl;
	
	fl = devc[DEVISTS];
	if((fl & INITPE) != 0)
		wakeup(&plinitr);
	if((fl & DONE) != 0){
		slcr[0x900/4] = 0xf;
		slcr[0x240/4] = 0;
		devc[DEVMASK] |= DONE;
		axi->attr &= ~SG_FAULT;
		wakeup(&pldoner);
	}
	if((fl & DMADONE) != 0){
		dmadone++;
		wakeup(&pldmar);
	}
	devc[DEVISTS] = fl;
}

static void
plinit(void)
{
	Physseg seg;

	memset(&seg, 0, sizeof seg);
	seg.attr = SG_PHYSICAL | SG_DEVICE | SG_NOEXEC | SG_FAULT;
	seg.name = "axi";
	seg.pa = 0x40000000;
	seg.size = 0x8000000;
	axi = addphysseg(&seg);

	devc[DEVCTRL] &= ~(PROG|1<<25);
	devc[DEVCTRL] |= 3<<26|PROG;
	devc[DEVISTS] = -1;
	devc[DEVMASK] = ~(DONE|INITPE|DMADONE);
	intrenable(DEVCIRQ, plirq, nil, LEVEL, "pl");
	
	slcr[FPGA0_CLK_CTRL] = 1<<20 | 10<<8;
}

static void
plconf(void)
{
	axi->attr |= SG_FAULT;
	procflushpseg(axi);
	flushmmu();

	slcr[0x240/4] = 0xf;
	slcr[0x900/4] = 0xa;
	devc[DEVISTS] = DONE|INITPE|DMADONE;
	devc[DEVCTRL] |= PROG;
	devc[DEVCTRL] &= ~PROG;
	devc[DEVMASK] &= ~DONE;
	devc[DEVCTRL] |= PROG;

	while(waserror())
		;
	sleep(&plinitr, isplinit, nil);
	poperror();
}

static long
plwrite(uintptr pa, long n)
{
	dmadone = 0;
	coherence();
	devc[DMASRC] = pa;
	devc[DMADST] = -1;
	devc[DMASRCL] = n>>2;
	devc[DMADSTL] = 0;

	while(waserror())
		;
	sleep(&pldmar, isdmadone, nil);
	poperror();

	return n;
}

static long
plcopy(uchar *d, long n)
{
	long ret;
	ulong nn;
	uintptr pa;
	
	if((n & 3) != 0 || n <= 0)
		error(Eshort);

	eqlock(&plwlock);
	if(waserror()){
		qunlock(&plwlock);
		nexterror();
	}

	ret = n;
	pa = PADDR(plbuf);
	while(n > 0){
		if(n > PLBUFSIZ)
			nn = PLBUFSIZ;
		else
			nn = n;
		memmove(plbuf, d, nn);
		cleandse(plbuf, plbuf + nn);
		clean2pa(pa, pa + nn);
		n -= plwrite(pa, nn);
		d += nn;
	}

	qunlock(&plwlock);
	poperror();

	return ret;
}

void
archinit(void)
{
	slcr[2] = 0xDF0D;
	xadcinit();
	plinit();
}

static long
archread(Chan *c, void *a, long n, vlong offset)
{
	char buf[64];

	switch((ulong)c->qid.path){
	case Qdir:
		return devdirread(c, a, n, archdir, narchdir, devgen);
	case Qtemp:
		snprint(buf, sizeof(buf), "%d.%d\n", temp/10, temp%10);
		return readstr(offset, a, n, buf);
	case Qpl:
		eqlock(&plrlock);
		if(waserror()){
			qunlock(&plrlock);
			nexterror();
		}
		sleep(&pldoner, ispldone, nil);
		qunlock(&plrlock);
		poperror();
		return 0;
	case Qfbctl:
		return fbctlread(c, a, n, offset);
	default:
		error(Egreg);
	}
}

static long
archwrite(Chan *c, void *a, long n, vlong offset)
{
	switch((ulong)c->qid.path){
	case Qpl:
		return plcopy(a, n);
	case Qfbctl:
		return fbctlwrite(c, a, n, offset);
	default:
		error(Egreg);
	}
}

Walkqid*
archwalk(Chan* c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, archdir, narchdir, devgen);
}

static int
archstat(Chan* c, uchar* dp, int n)
{
	return devstat(c, dp, n, archdir, narchdir, devgen);
}

static Chan*
archopen(Chan* c, int omode)
{
	devopen(c, omode, archdir, narchdir, devgen);
	if((ulong)c->qid.path == Qpl && (c->mode == OWRITE || c->mode == ORDWR)){
		if(incref(&plwopen) != 1){
			c->flag &= ~COPEN;
			decref(&plwopen);
			error(Einuse);
		}
		plbuf = smalloc(PLBUFSIZ);
		plconf();
	}
	return c;
}

static void
archclose(Chan* c)
{
	if((c->flag & COPEN) != 0)
	if((ulong)c->qid.path == Qpl && (c->mode == OWRITE || c->mode == ORDWR)){
		free(plbuf);
		plbuf = nil;
		decref(&plwopen);
	}
}

static Chan*
archattach(char* spec)
{
	return devattach('P', spec);
}

Dev archdevtab = {
	'P',
	"arch",
	
	devreset,
	devinit,
	devshutdown,
	archattach,
	archwalk,
	archstat,
	archopen,
	devcreate,
	archclose,
	archread,
	devbread,
	archwrite,
	devbwrite,
	devremove,
	devwstat,
};

