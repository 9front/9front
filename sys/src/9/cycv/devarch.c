#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

#define fpga ((ulong*) FPGAMGR_BASE)

enum { REMAP = 0x0 / 4 };

enum { Timeout = 3000 };

enum {
	FPGASTAT,
	FPGACTRL,
	FPGAINTEN = 0x830/4,
	FPGAINTTYPE = 0x838/4,
	FPGAINTPOL = 0x83C/4,
	FPGAINTSTATUS = 0x840/4,
	FPGAEOI = 0x84C/4,
	FPGAPINS = 0x850/4,
};

enum {
	/*FPGACTRL*/
	HPSCONFIG = 1<<0,
	NCONFIGPULL = 1<<2,
	AXICFGEN = 1<<8,
	/*FPGAPINS*/
	NSTATUS = 1<<0,
	CONF_DONE = 1<<1,
	INIT_DONE = 1<<2,
	CRC_ERROR = 1<<3,
	CVP_CONF_DONE = 1<<4,
	PR_READY = 1<<5,
	PR_ERROR = 1<<6,
	PR_DONE = 1<<7,
	NCONFIG_PIN = 1<<8,
	NSTATUS_PIN = 1<<9,
	CONF_DONE_PIN = 1<<10,
	FPGA_POWER_ON = 1<<11
};

enum {
	Qdir = 0,
	Qfpga,
	Qbase,

	Qmax = 16,
};

static Dirtab archdir[Qmax] = {
	".",		{ Qdir, 0, QTDIR },	0,	0555,
	"fpga",		{ Qfpga, 0 }, 		0,	0660,
};
static int narchdir = Qbase;

static Physseg *axi;

static Ref fpgawopen;
enum { FPGABUFSIZ = 65536 };
static uchar *fpgabuf;
static int fpgabufp;
static int fpgaok;

static Rendez fpgarend;
static u32int fpgawaitset, fpgawaitclr;
static int
donewaiting(void *)
{
	u32int s;
	
	s = fpga[FPGAPINS];
	return (s & fpgawaitset | ~s & fpgawaitclr) != 0;
	
}
static void
fpgairq(Ureg *, void *)
{
	fpga[FPGAEOI] = -1;
	if(donewaiting(nil))
		wakeup(&fpgarend);
}
static int
fpgawait(u32int set, u32int clr, int timeout)
{
	int s;

	fpgawaitset = set;
	fpgawaitclr = clr;
	if(donewaiting(nil)) return 0;
	s = spllo();
	fpga[FPGAINTEN] = 0;
	fpga[FPGAEOI] = -1;
	fpga[FPGAINTPOL] = set;
	fpga[FPGAINTEN] = set | clr;
	tsleep(&fpgarend, donewaiting, nil, timeout);
	fpga[FPGAINTEN] = 0;
	fpga[FPGAEOI] = -1;
	splx(s);
	return donewaiting(nil) ? 0 : -1;
}

static void
fpgaconf(void)
{
	int msel;
	enum { PORFAST = 1, AES = 2, AESMAYBE = 4, COMP = 8, FPP32 = 16 };
	static uchar mseltab[16][3] = {
		[0] {0, 1, PORFAST},
		[4] {0, 1, 0},
		[1] {0, 2, PORFAST|AES},
		[5] {0, 2, AES},
		[2] {0, 3, COMP|AESMAYBE|PORFAST},
		[6] {0, 3, COMP|AESMAYBE},
		[8] {1, 1, PORFAST|FPP32},
		[12] {1, 1, FPP32},
		[9] {1, 2, AES|FPP32|PORFAST},
		[13] {1, 2, AES|FPP32},
		[10] {1, 4, COMP|AESMAYBE|PORFAST|FPP32},
		[14] {1, 4, COMP|AESMAYBE|FPP32}
	};

	axi->attr |= SG_FAULT;
	procflushpseg(axi);
	flushmmu();

	if((fpga[FPGAPINS] & FPGA_POWER_ON) == 0)
		error("FPGA powered off");
	msel = fpga[FPGASTAT] >> 3 & 0x1f;
	if(msel >= 16 || mseltab[msel][1] == 0){
		print("MSEL set to invalid setting %#.2x\n", msel);
		error("MSEL set to invalid setting");
	}
	fpga[FPGACTRL] = fpga[FPGACTRL] & ~0x3ff
		| mseltab[msel][0] << 9 /* cfgwdth */
		| mseltab[msel][1]-1 << 6 /* cdratio */
		| NCONFIGPULL | HPSCONFIG;
	if(fpgawait(0, NSTATUS, Timeout) < 0 || fpgawait(0, CONF_DONE, Timeout) < 0)
		error("FPGA won't enter reset phase");
	fpga[FPGACTRL] &= ~NCONFIGPULL;
	if(fpgawait(NSTATUS, 0, Timeout) < 0)
		error("FPGA won't enter configuration phase");
	fpga[FPGACTRL] |= AXICFGEN;
}

static void
fpgawrite(uchar *a, int n)
{
	int m;

	if(waserror()){
		fpgaok = 0;
		nexterror();
	}
	if((fpga[FPGAPINS] & NSTATUS) == 0)
		error("FPGA reports configuration error");
	fpgaok = 1;
	while(fpgabufp + n >= FPGABUFSIZ){
		m = FPGABUFSIZ - fpgabufp;
		memmove(&fpgabuf[fpgabufp], a, m);
		cleandse(fpgabuf, fpgabuf + FPGABUFSIZ);
		dmacopy((void *) FPGAMGRDATA, fpgabuf, FPGABUFSIZ, SRC_INC);
		a += m;
		n -= m;
		fpgabufp = 0;
	}
	memmove(&fpgabuf[fpgabufp], a, n);
	fpgabufp += n;
	poperror();
}

static void
fpgafinish(void)
{
	if(!fpgaok) return;
	while((fpgabufp & 3) != 0)
		fpgabuf[fpgabufp++] = 0;
	cleandse(fpgabuf, fpgabuf + fpgabufp);
	dmacopy((void *) FPGAMGRDATA, fpgabuf, fpgabufp, SRC_INC);
	fpga[FPGACTRL] &= ~AXICFGEN;
	if(fpgawait(CONF_DONE, NSTATUS, Timeout) < 0){
		print("FPGA stuck in configuration phase -- truncated file?\n");
		return;
	}
	if((fpga[FPGAPINS] & NSTATUS) == 0){
		print("FPGA reports configuration error\n");
		return;
	}
	if(fpgawait(INIT_DONE, 0, Timeout) < 0){
		print("FPGA stuck in initialization phase\n");
		return;
	}
	fpga[FPGACTRL] &= ~HPSCONFIG;
	
	axi->attr &= ~SG_FAULT;
	procflushpseg(axi);
	flushmmu();
}

static long
archread(Chan *c, void *a, long n, vlong)
{
	switch((ulong)c->qid.path){
	case Qdir:
		return devdirread(c, a, n, archdir, narchdir, devgen);
	default:
		error(Egreg);
		return -1;
	}
}

static long
archwrite(Chan *c, void *a, long n, vlong)
{
	switch((ulong)c->qid.path){
	case Qfpga:
		fpgawrite(a, n);
		return n;
	default:
		error(Egreg);
		return -1;
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
	if((ulong)c->qid.path == Qfpga && (c->mode == OWRITE || c->mode == ORDWR)){
		if(incref(&fpgawopen) != 1){
			c->flag &= ~COPEN;
			decref(&fpgawopen);
			error(Einuse);
		}
		fpgaok = 0;
		fpgaconf();
		fpgabuf = xspanalloc(FPGABUFSIZ, 64, 0);
		fpgabufp = 0;
	}
	return c;
}

static void
archclose(Chan* c)
{
	if((c->flag & COPEN) != 0)
	if((ulong)c->qid.path == Qfpga && (c->mode == OWRITE || c->mode == ORDWR)){
		fpgafinish();
		//xfree(fpgabuf);
		fpgabuf = nil;
		decref(&fpgawopen);
	}
}

void
archinit(void)
{
	Physseg seg;

	fpga[FPGAINTEN] = 0;
	fpga[FPGAEOI] = -1;
	fpga[FPGAINTTYPE] = -1;
	intrenable(FPGAMGRIRQ, fpgairq, nil, LEVEL, "fpgamgr");
	
	resetmgr[BRGMODRST] &= ~7;
	l3[REMAP] = 0x18;

	memset(&seg, 0, sizeof seg);
	seg.attr = SG_PHYSICAL | SG_DEVICE | SG_NOEXEC | SG_FAULT;
	seg.name = "axi";
	seg.pa = 0xFF200000;
	seg.size = 0x200000;
	axi = addphysseg(&seg);

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

