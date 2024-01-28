#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

enum {
	QSPIDIV = 1,
	QSPISIZ = 1<<24,
};

enum {
	CONF,
	INTSTAT,
	INTEN,
	INTDIS,
	INTMASK,
	SPIEN,
	DELAY,
	TXD0,
	RXD,
	IDLE,
	TXTHRES,
	RXTHRES,
	TXD1 = 0x80/4,
	TXD2,
	TXD3,
	LQSPICFG = 0xA0/4,
};

enum {
	TXFULL = 1<<3,
	TXNFULL = 1<<2,
	RXNEMPTY = 1<<4,
	STARTCOM = 1<<16,
};

enum {
	Qdir = 0,
	Qboot,
	Qbase,

	Qmax = 16,
};

static ulong *qspi;
static QLock qspil;

static Dirtab qspidir[Qmax] = {
	".",		{ Qdir, 0, QTDIR },	0,	0555,
	"boot",		{ Qboot, 0},		65536,	0640,
};
static int nqspidir = Qbase;

static ulong
qspicmd(int n, ulong d)
{
	while((qspi[INTSTAT] & TXNFULL) == 0)
		;
	if(n == 4)
		qspi[TXD0] = d;
	else
		qspi[TXD1 - 1 + n] = d;
	qspi[CONF] |= STARTCOM;
	while((qspi[INTSTAT] & (TXNFULL|RXNEMPTY)) != (TXNFULL|RXNEMPTY))
		;
	return qspi[RXD];
}

static void
qspiinit(void)
{
	static int done;
	
	if(done)
		return;
	qspi = vmap(QSPI_BASE, 0x100);
	qspi[LQSPICFG] &= ~(1<<31);
	qspi[CONF] = 1<<31 | 1<<19 | 1<<15 | 1<<14 | 1<<10 | 3<<6 | QSPIDIV<<3 | 1;
	qspi[SPIEN] = 1;	
}

static void
qspisel(int sel)
{
	if(sel)
		qspi[CONF] &= ~(1<<10);
	else
		qspi[CONF] |= 1<<10;
}

static void
waitbusy(void)
{
	ulong d;

	for(;;){
		qspisel(1);
		d = qspicmd(2, 0x05);
		qspisel(0);
		if((d & 1<<24) == 0)
			break;
		tsleep(&up->sleep, return0, nil, 1);
	}
}

static ulong
doread(uvlong addr, void *a, ulong n)
{
	ulong d, *aa, ret;

	if(addr >= QSPISIZ)
		return 0;
	if(addr + n > QSPISIZ)
		n = QSPISIZ - addr;
	evenaddr((uintptr) a);
	qspisel(1);
	qspicmd(4, 0x6B | addr << 8);
	qspicmd(1, 0);
	ret = n;
	aa = a;
	while(n > 0){
		d = qspicmd(4, 0);
		if(n >= 4){
			*aa++ = d;
			n -= 4;
		}else{
			memmove(aa, (char*) &d + 4 - n, n);
			break;
		}
	}
	qspisel(0);
	return ret;
}

static ulong
dowrite(uvlong addr, void *a, ulong n)
{
	ulong *aa, ret, nn;

	if(addr >= QSPISIZ)
		return 0;
	if(addr + n > QSPISIZ)
		n = QSPISIZ - addr;
	evenaddr((uintptr) a);
	ret = n;
	aa = a;
	while(n > 0){
		qspisel(1);
		qspicmd(1, 6);
		qspisel(0);
		qspisel(1);
		qspicmd(4, 0x32 | addr << 8);
		if(n > 256)
			nn = 256;
		else
			nn = n;
		n -= nn;
		addr += nn;
		while(nn > 0)
			if(nn >= 4){
				qspicmd(4, *aa++);
				nn -= 4;
			}else{
				qspicmd(n, *aa);
				break;
			}
		qspisel(0);
		waitbusy();
	}
	return ret;
}

static void
doerase(ulong addr)
{
	qspisel(1);
	qspicmd(1, 6);
	qspisel(0);
	qspisel(1);
	qspicmd(4, 0xD8 | addr << 8);
	qspisel(0);
	waitbusy();
}

static Walkqid*
qspiwalk(Chan* c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, qspidir, nqspidir, devgen);
}

static int
qspistat(Chan* c, uchar* dp, int n)
{
	return devstat(c, dp, n, qspidir, nqspidir, devgen);
}

static Chan*
qspiopen(Chan* c, int omode)
{
	devopen(c, omode, qspidir, nqspidir, devgen);
	if(c->qid.path == Qboot){
		qlock(&qspil);
		if((omode & OTRUNC) != 0)
			doerase(0);
	}
	return c;
}

static void
qspiclose(Chan* c)
{
	if(c->qid.path == Qboot && (c->flag & COPEN) != 0)
		qunlock(&qspil);
}

static long
qspiread(Chan *c, void *a, long n, vlong offset)
{
	switch((ulong)c->qid.path){
	case Qdir:
		return devdirread(c, a, n, qspidir, nqspidir, devgen);
	case Qboot:
		return doread(offset, a, n);
	default:
		error(Egreg);
	}
}

static long
qspiwrite(Chan *c, void *a, long n, vlong offset)
{
	switch((ulong)c->qid.path){
	case Qboot:
		return dowrite(offset, a, n);
	default:
		error(Egreg);
	}
}

static Chan*
qspiattach(char* spec)
{
	qspiinit();
	return devattach('Q', spec);
}

Dev qspidevtab = {
	'Q',
	"qspi",
	
	devreset,
	devinit,
	devshutdown,
	qspiattach,
	qspiwalk,
	qspistat,
	qspiopen,
	devcreate,
	qspiclose,
	qspiread,
	devbread,
	qspiwrite,
	devbwrite,
	devremove,
	devwstat,
};

