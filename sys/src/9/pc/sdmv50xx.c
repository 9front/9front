/*
 * Marvell 88SX[56]0[48][01] Serial ATA (SATA) driver
 *
 * See MV-S101357-00 Rev B Marvell PCI/PCI-X to 8-Port/4-Port
 * SATA Host Controller, ATA-5 ANSI NCITS 340-2000.
 *
 * This is a heavily-modified version (by Coraid) of a heavily-modified
 * version (from The Labs) of a driver written by Coraid, Inc.
 * The original copyright notice appears at the end of this file.
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include	"../port/sd.h"
#include <fis.h>

#define	dprint(...)	// print(__VA_ARGS__)
#define	idprint(...)	print(__VA_ARGS__)
#define	Ticks		MACHP(0)->ticks

enum {
	NCtlr		= 4,
	NCtlrdrv		= 8,
	NDrive		= NCtlr*NCtlrdrv,

	SrbRing = 32,

	/* Addresses of ATA register */
	ARcmd		= 027,
	ARdev		= 026,
	ARerr		= 021,
	ARfea		= 021,
	ARlba2		= 025,
	ARlba1		= 024,
	ARlba0		= 023,
	ARseccnt	= 022,
	ARstat		= 027,

	ATAerr		= 1<<0,
	ATAdrq		= 1<<3,
	ATAdf 		= 1<<5,
	ATAdrdy 	= 1<<6,
	ATAbusy 	= 1<<7,
	ATAabort	= 1<<2,
	ATAobs		= 1<<1 | 1<<2 | 1<<4,
	ATAeIEN	= 1<<1,
	ATAbad		= ATAbusy|ATAdf|ATAdrq|ATAerr,

	SFdone 		= 1<<0,
	SFerror 		= 1<<1,

	PRDeot		= 1<<15,

	/* EDMA interrupt error cause register */
	ePrtDataErr	= 1<<0,
	ePrtPRDErr	= 1<<1,
	eDevErr		= 1<<2,
	eDevDis		= 1<<3,
	eDevCon	= 1<<4,
	eOverrun	= 1<<5,
	eUnderrun	= 1<<6,
	eSelfDis		= 1<<8,
	ePrtCRQBErr	= 1<<9,
	ePrtCRPBErr	= 1<<10,
	ePrtIntErr	= 1<<11,
	eIORdyErr	= 1<<12,

	/* flags for sata 2 version */
	eSelfDis2	= 1<<7,
	SerrInt		= 1<<5,

	/* EDMA Command Register */
	eEnEDMA	= 1<<0,
	eDsEDMA 	= 1<<1,
	eAtaRst 		= 1<<2,

	/* Interrupt mask for errors we care about */
	IEM		= eDevDis | eDevCon | eSelfDis,
	IEM2		= eDevDis | eDevCon | eSelfDis2,

	/* phyerrata magic */
	Mpreamp	= 0x7e0,
	Dpreamp	= 0x720,

	REV60X1B2	= 0x7,
	REV60X1C0	= 0x9,

	/* general mmio registers */
	Portswtch	= 0x1d64/4,

	/* drive states */
	Dnull 		= 0,
	Dnew,
	Dready,
	Derror,
	Dmissing,
	Dreset,
	Dlast,

	/* sata mode */
	DMautoneg	= 0,
	DMsatai,
	DMsataii,
};

typedef struct Arb Arb;
typedef struct Bridge Bridge;
typedef struct Chip Chip;
typedef struct Ctlr Ctlr;
typedef struct Drive Drive;
typedef struct Edma Edma;
typedef struct Prd Prd;
typedef struct Rx Rx;
typedef struct Srb Srb;
typedef struct Tx Tx;

/*
 * there are 4 drives per chip.  thus an 8-port
 * card has two chips.
 */
struct Chip
{
	Arb	*arb;
	Edma	*edma;
};

struct Drive
{
	Lock;

	Ctlr	*ctlr;
	SDunit	*unit;
	char	name[10];
	Sfis;

	Bridge	*bridge;
	Edma	*edma;
	Chip	*chip;
	int	chipx;

	int	drivechange;
	int	state;
	uvlong	sectors;
	uint	secsize;
	ulong	pm2;		/* phymode 2 init state */
	ulong	intick;		/* check for hung drives. */
	int	wait;
	int	mode;		/* DMautoneg, satai or sataii. */

	char	serial[20+1];
	char	firmware[8+1];
	char	model[40+1];
	uvlong	wwn;

	ushort	info[256];

	Srb	*srb[SrbRing-1];
	int	nsrb;
	Prd	*prd;
	Tx	*tx;
	Rx	*rx;

	Srb	*srbhead;
	Srb	*srbtail;
	int	driveno;		/* ctlr*NCtlrdrv + unit */
};

struct Ctlr
{
	Lock;

	int	irq;
	int	tbdf;
	int	rid;
	ulong	magic;
	int	enabled;
	int	type;
	SDev	*sdev;
	Pcidev	*pcidev;

	uchar	*mmio;
	ulong	*lmmio;
	Chip	chip[2];
	int	nchip;
	Drive	drive[NCtlrdrv];
	int	ndrive;
};

struct Srb			/* request buffer */
{
	Lock;
	Rendez;
	Srb	*next;

	Drive	*drive;
	uvlong	blockno;
	int	count;
	int	req;
	int	flag;
	uchar	*data;

	uchar	cmd;
	uchar	lba[6];
	uchar	sectors;
	int	sta;
	int	err;
};

/*
 * Memory-mapped I/O registers in many forms.
 */
struct Bridge			/* memory-mapped per-drive registers */
{
	ulong	status;
	ulong	serror;
	ulong	sctrl;
	ulong	phyctrl;
	ulong	phymode3;
	ulong	phymode4;
	uchar	fill0[0x14];
	ulong	phymode1;
	ulong	phymode2;
	char	fill1[8];
	ulong	ctrl;
	char	fill2[0x34];
	ulong	phymode;
	char	fill3[0x88];
};				/* must be 0x100 hex in length */

struct Arb			/* memory-mapped per-chip registers */
{
	ulong	config;		/* satahc configuration register (sata2 only) */
	ulong	rqop;		/* request queue out-pointer */
	ulong	rqip;		/* response queue in pointer */
	ulong	ict;		/* inerrupt caolescing threshold */
	ulong	itt;		/* interrupt timer threshold */
	ulong	ic;		/* interrupt cause */
	ulong	btc;		/* bridges test control */
	ulong	bts;		/* bridges test status */
	ulong	bpc;		/* bridges pin configuration */
	char	fill1[0xdc];
	Bridge	bridge[4];
};

struct Edma			/* memory-mapped per-drive DMA-related registers */
{
	ulong	config;		/* configuration register */
	ulong	timer;
	ulong	iec;		/* interrupt error cause */
	ulong	iem;		/* interrupt error mask */

	ulong	txbasehi;		/* request queue base address high */
	ulong	txi;		/* request queue in pointer */
	ulong	txo;		/* request queue out pointer */

	ulong	rxbasehi;		/* response queue base address high */
	ulong	rxi;		/* response queue in pointer */
	ulong	rxo;		/* response queue out pointer */

	ulong	ctl;		/* command register */
	ulong	testctl;		/* test control */
	ulong	status;
	ulong	iordyto;		/* IORDY timeout */
	char	fill[0x18];
	ulong	sataconfig;	/* sata 2 */
	char	fill[0xac];
	ushort	pio;		/* data register */
	char	pad0[2];
	uchar	err;		/* features and error */
	char	pad1[3];
	uchar	seccnt;		/* sector count */
	char	pad2[3];
	uchar	lba0;
	char	pad3[3];
	uchar	lba1;
	char	pad4[3];
	uchar	lba2;
	char	pad5[3];
	uchar	lba3;
	char	pad6[3];
	uchar	cmdstat;		/* cmd/status */
	char	pad7[3];
	uchar	altstat;		/* alternate status */
	uchar	fill2[0x1df];
	Bridge	port;
	char	fill3[0x1c00];	/* pad to 0x2000 bytes */
};

/*
 * Memory structures shared with card.
 */
struct Prd			/* physical region descriptor */
{
	ulong	pa;		/* byte address of physical memory */
	ushort	count;		/* byte count (bit0 must be 0) */
	ushort	flag;
	ulong	zero;		/* high long of 64 bit address */
	ulong	reserved;
};

struct Tx				/* command request block */
{
	ulong	prdpa;		/* physical region descriptor table structures */
	ulong	zero;		/* must be zero (high long of prd address) */
	ushort	flag;		/* control flags */
	ushort	regs[11];
};

struct Rx				/* command response block */
{
	ushort	cid;		/* cID of response */
	uchar	cEdmaSts;	/* EDMA status */
	uchar	cDevSts;		/* status from disk */
	ulong	ts;		/* time stamp */
};

static	Ctlr 	*mvsatactlr[NCtlr];
static	Drive 	*mvsatadrive[NDrive];
static	int	nmvsatadrive;
static	char	*diskstates[Dlast] = {
	"null",
	"new",
	"ready",
	"error",
	"missing",
	"reset",
};

extern SDifc sdmv50xxifc;

/*
 * Request buffers.
 */
static struct
{
	Lock;
	Srb	*freechain;
	int	nalloc;
} srblist;

static Srb*
allocsrb(void)
{
	Srb *p;

	ilock(&srblist);
	if((p = srblist.freechain) == nil){
		srblist.nalloc++;
		iunlock(&srblist);
		p = smalloc(sizeof *p);
	}else{
		srblist.freechain = p->next;
		iunlock(&srblist);
	}
	return p;
}

static void
freesrb(Srb *p)
{
	ilock(&srblist);
	p->next = srblist.freechain;
	srblist.freechain = p;
	iunlock(&srblist);
}

static int
satawait(uchar *p, uchar mask, uchar v, int ms)
{
	int i;

	for(i=0; i<ms && (*p & mask) != v; i++)
		microdelay(1000);
	return (*p & mask) == v;
}

/* unmask in the pci registers err done */
static void
portswitch(ulong *mmio, int port, uint coal, uint on)
{
	ulong m;

	m = 3<<(port&3)*2 | coal<<8;
	if((port&7) >= 4)
		m <<= 9;
	if(on)
		mmio[Portswtch] |= m;
	else
		mmio[Portswtch] &= m;
}

static char*
dnam(Drive *d)
{
	if(d->unit)
		return d->unit->name;
	return d->name;
}

/* I give up, marvell.  You win. */
static void
phyerrata(Drive *d)
{
	ulong n, m;
	enum { BadAutoCal = 0xf << 26, };

	if(d->ctlr->type == 1){
		/* set phyctrl bits [0:1] to 01 per MV-S102013-00 Rev C. */
		n = d->bridge->phyctrl;
		n &= ~3;
		d->bridge->phyctrl = n | 1;
		return;
	}
	microdelay(200);
	n = d->bridge->phymode2;
	while ((n & BadAutoCal) == BadAutoCal) {
		dprint("%s: badautocal\n", dnam(d));
		n &= ~(1<<16);
		n |= 1<<31;
		d->bridge->phymode2 = n;
		microdelay(200);
		d->bridge->phymode2 &= ~(1<<16 | 1<<31);
		microdelay(200);
		n = d->bridge->phymode2;
	}
	n &= ~(1<<31);
	d->bridge->phymode2 = n;
	microdelay(200);

	/* abra cadabra!  (random magic) */
	m = d->bridge->phymode3;
	m &= ~0x7f800000;
	m |= 0x2a800000;
	d->bridge->phymode3 = m;

	/* fix phy mode 4 */
	m = d->bridge->phymode3;
	n = d->bridge->phymode4;
	n &= ~(1<<1);
	n |= 1;
	switch(d->ctlr->rid){
	case REV60X1B2:
	default:
		d->bridge->phymode4 = n;
		d->bridge->phymode3 = m;
		break;
	case REV60X1C0:
		d->bridge->phymode4 = n;
		break;
	}

	/* revert values of pre-emphasis and signal amps to the saved ones */
	n = d->bridge->phymode2;
	n &= ~Mpreamp;
	n |= d->pm2;
	n &= ~(1<<16);
	d->bridge->phymode2 = n;
}

static void
edmacleanout(Drive *d)
{
	int i;
	Srb *srb;

	for(i=0; i<nelem(d->srb); i++){
		if(srb = d->srb[i]){
			d->srb[i] = nil;
			d->nsrb--;
			srb->flag |= SFerror|SFdone;
			wakeup(srb);
		}
	}
	while(srb = d->srbhead){
		d->srbhead = srb->next;
		srb->flag |= SFerror|SFdone;
		wakeup(srb);
	}
}

static int
edmadisable(Drive *d, int reset)
{
	Edma *e;

	e = d->edma;
	if(!reset && (e->ctl & eEnEDMA) == 0)
		return 0;
	e->ctl = eDsEDMA;
	microdelay(1);
	if(reset)
		e->ctl = eAtaRst;
	microdelay(25);
	e->ctl = 0;
	if (satawait((uchar *)&e->ctl, eEnEDMA, 0, 3*1000) == 0){
		print("%s: eEnEDMA never cleared on reset\n", dnam(d));
		return -1;
	}
	edmacleanout(d);
	return 0;
}

static void
resetdisk(Drive *d)
{
	ulong n;

	d->sectors = 0;
	d->unit->sectors = 0;
	if (d->ctlr->type == 2) {
		/*
		 * without bit 8 we can boot without disks, but
		 * inserted disks will never appear.  :-X
		 */
		n = d->edma->sataconfig;
		n &= 0xff;
		n |= 0x9b1100;
		d->edma->sataconfig = n;
		n = d->edma->sataconfig;	/* flush */
		USED(n);
	}
	if(edmadisable(d, 1) == -1){
	}
	phyerrata(d);
	d->bridge->sctrl = 0x301 | d->mode<<4;
	d->state = Dmissing;
}

static void
edmainit(Drive *d)
{
	int i;

	if(d->tx != nil)
		return;

	d->tx = xspanalloc(32*sizeof(Tx), 1024, 0);
	d->rx = xspanalloc(32*sizeof(Rx), 256, 0);
	d->prd = xspanalloc(32*sizeof(Prd), 32, 0);
	for(i = 0; i < 32; i++)
		d->tx[i].prdpa = PCIWADDR(&d->prd[i]);
	coherence();
}

static int
configdrive(Ctlr *ctlr, Drive *d, SDunit *unit)
{
	dprint("%s: configdrive\n", unit->name);
	d->unit = unit;
	resetdisk(d);
	portswitch(ctlr->lmmio, d->driveno, 0, 1);
	delay(100);
	if(d->bridge->status){
		dprint("%s: configdrive: found drive %lux\n", unit->name, d->bridge->status);
		return 0;
	}
	return -1;
}

static int
edmaenable(Drive *d)
{
	Edma *edma;

	dprint("%s: enabledrive..", dnam(d));

	if((d->bridge->status & 0xf) != 3){
		dprint("%s: not present\n", dnam(d));
		return -1;
	}
	edma = d->edma;
	if(satawait(&edma->cmdstat, ATAbusy, 0, 5*1000) == 0){
		dprint("%s: busy timeout\n", dnam(d));
		return -1;
	}
	edma->iec = 0;
	d->chip->arb->ic &= ~(0x101 << d->chipx);
	edma->config = 0x51f;
	if (d->ctlr->type == 2)
		edma->config |= 7<<11;
	edma->txi = PCIWADDR(d->tx);
	edma->txo = (ulong)d->tx & 0x3e0;
	edma->rxi = (ulong)d->rx & 0xf8;
	edma->rxo = PCIWADDR(d->rx);
	edma->ctl |= 1;		/* enable dma */
	return 0;
}

static int
enabledrive(Drive *d)
{
	dprint("%s: enabledrive..", dnam(d));
	if(edmaenable(d) == 0){
		switch(d->bridge->status){
		case 0x113:
		case 0x123:
			d->state = Dnew;
			break;
		}
		return 0;
	}
	print("mv50: enable reset\n");
	d->state = Dreset;
	return -1;
}

static void
disabledrive(Drive *d)
{
	if(d->tx == nil)	/* never enabled */
		return;
	d->edma->ctl = 0;
	d->edma->iem = 0;
	portswitch(d->ctlr->lmmio, d->driveno, 0, 0);
}

static int
setudmamode(Drive *d, uchar mode)
{
	Edma *edma;

	dprint("%s: setudmamode %d\n", dnam(d), mode);
	edma = d->edma;
	if(edma == nil) {
		iprint("setudamode(m%d): zero d->edma\m", d->driveno);
		return 0;
	}
	if(satawait(&edma->cmdstat, ~ATAobs, ATAdrdy, 250) == 0){
		iprint("%s: cmdstat 0x%.2ux ready timeout\n", dnam(d), edma->cmdstat);
		return 0;
	}
	edma->altstat = ATAeIEN;
	edma->err = 3;
	edma->seccnt = 0x40 | mode;
	edma->cmdstat = 0xef;
	microdelay(1);
	if(satawait(&edma->cmdstat, ATAbusy, 0, 5*1000) == 0){
		iprint("%s: cmdstat 0x%.2ux busy timeout\n", dnam(d), edma->cmdstat);
		return 0;
	}
	return 1;
}

static int
identifydrive(Drive *d)
{
	char *s;
	int i;
	ushort *id;
	Edma *edma;
	SDunit *u;

	dprint("%s: identifydrive\n", dnam(d));
	setfissig(d, 0);			/* BOTCH; need to find and set signature */
	if(setudmamode(d, 5) == 0)	/* BOTCH; run after identify */
		goto Error;

	id = d->info;
	memset(d->info, 0, sizeof d->info);
	edma = d->edma;
	if(satawait(&edma->cmdstat, ~ATAobs, ATAdrdy, 5*1000) == 0)
		goto Error;

	edma->altstat = ATAeIEN;	/* no interrupts */
	edma->cmdstat = 0xec;
	microdelay(1);
	if(satawait(&edma->cmdstat, ATAbusy, 0, 5*1000) == 0)
		goto Error;
	for(i = 0; i < 256; i++)
		id[i] = edma->pio;
	if(edma->cmdstat & ATAbad)
		goto Error;
	d->sectors = idfeat(d, id);
	d->secsize = idss(d, id);
	idmove(d->serial, id+10, 20);
	idmove(d->firmware, id+23, 8);
	idmove(d->model, id+27, 40);
	d->wwn = idwwn(d, id);

	u = d->unit;
	memset(u->inquiry, 0, sizeof u->inquiry);
	u->inquiry[2] = 2;
	u->inquiry[3] = 2;
	u->inquiry[4] = sizeof u->inquiry - 4;
	idmove((char*)u->inquiry+8, id+27, 40);

	if(enabledrive(d) == 0) {
		d->state = Dready;
		d->drivechange = 1;
		s = nil;
		if(d->feat & Dllba)
			s = "L";
		idprint("%s: %sLBA %llud sectors\n", dnam(d), s, d->sectors);
	} else
		d->state = Derror;
	if(d->state == Dready)
		return 0;
	return -1;
Error:
	dprint("error...");
	d->state = Derror;
	return -1;
}

/*
 * p. 163:
 *	M	recovered error
 *	P	protocol error
 *	N	PhyRdy change
 *	W	CommWake
 *	B	8-to-10 encoding error
 *	D	disparity error
 *	C	crc error
 *	H	handshake error
 *	S	link sequence error
 *	T	transport state transition error
 *	F	unrecognized fis type
 *	X	device changed
 */

static char stab[] = {
[1]	'M',
[10]	'P',
[16]	'N',
[18]	'W', 'B', 'D', 'C', 'H', 'S', 'T', 'F', 'X'
};
static ulong sbad = 7<<20 | 3<<23;

static void
serrdecode(ulong r, char *s, char *e)
{
	int i;

	e -= 3;
	for(i = 0; i < nelem(stab) && s < e; i++)
		if(r & 1<<i && stab[i]){
			*s++ = stab[i];
			if(sbad & 1<<i)
				*s++ = '*';
		}
	*s = 0;
}

static char *iectab[] = {
	"ePrtDataErr",
	"ePrtPRDErr",
	"eDevErr",
	"eDevDis",
	"eDevCon",
	"SerrInt",
	"eUnderrun",
	"eSelfDis2",
	"eSelfDis",
	"ePrtCRQBErr",
	"ePrtCRPBErr",
	"ePrtIntErr",
	"eIORdyErr",
};

static char*
iecdecode(ulong cause)
{
	int i;

	for(i = 0; i < nelem(iectab); i++)
		if(cause&(1<<i))
			return iectab[i];
	return "";
}

enum{
	Cerror1	= ePrtDataErr|ePrtPRDErr|eOverrun|ePrtCRQBErr|ePrtCRPBErr|ePrtIntErr,
	Cerror2	= ePrtDataErr|ePrtPRDErr|ePrtCRQBErr|
			ePrtCRPBErr|ePrtIntErr|eDevErr|eSelfDis2,
};

static void
updatedrive(Drive *d)
{
	int x;
	ulong cause;
	Edma *edma;
	char buf[32+4+1];

	edma = d->edma;
	if((edma->ctl & eEnEDMA) == 0){
		/* FEr SATA#4 40xx */
		x = d->edma->cmdstat;
		USED(x);
	}
	cause = edma->iec;
	if(cause == 0)
		return;
	dprint("%s: cause %.8lux [%s]\n", dnam(d), cause, iecdecode(cause));
	if(cause & eDevCon)
		d->state = Dnew;
	if(cause & eDevDis && d->state == Dready)
		iprint("%s: pulled: st=%.8lux\n", dnam(d), cause);
	switch(d->ctlr->type){
	case 1:
		if(cause & eUnderrun){
			/* FEr SATA#5 50xx for revs A0, B0 */
			if(d->ctlr->rid < 2)
				d->state = Dreset;
			else{
				d->state = Derror;
				dprint("%s: underrun\n", dnam(d));
			}
		}
		if(cause & (eDevErr | eSelfDis)){
			/*
			 * FEr SATA#7 60xx for refs A0, B0
			 * check for IRC error.  we only check the
			 * ABORT flag as we don't get the upper nibble
			 */
			if(d->ctlr->rid < 2)
			if(edma->altstat & ATAerr && edma->err & ATAabort)
				d->state = Dreset;
			else
				d->state = Derror;
		}
		if(cause & Cerror1)
			d->state = Dreset;
		break;
	case 2:
		if(cause & Cerror2)
			d->state = Dreset;
		if(cause & SerrInt){
			serrdecode(d->bridge->serror, buf, buf+sizeof buf);
			dprint("%s: serror %.8lux [%s]\n", dnam(d), d->bridge->serror, buf);
			d->bridge->serror = ~0; /*d->bridge->serror;*/
		}
		break;
	}
	edma->iec = ~cause;
}

/*
 * Requests
 */
static Srb*
srbrw(int rw, Drive *d, uchar *data, uint sectors, uvlong lba)
{
	int i;
	Srb *srb;
	static uchar cmd[2][2] = { 0xC8, 0x25, 0xCA, 0x35 };

	srb = allocsrb();
	srb->req = rw;
	srb->drive = d;
	srb->blockno = lba;
	srb->sectors = sectors;
	srb->count = sectors * d->secsize;
	srb->flag = 0;
	srb->data = data;

	for(i=0; i<6; i++)
		srb->lba[i] = lba >> 8*i;
	srb->cmd = cmd[srb->req!=SDread][(d->feat&Dllba)!=0];
	return srb;
}

#define CMD(r, v) (((r)<<8) | (v))
static void
mvsatarequest(ushort *cmd, Srb *srb, int llba)
{
	*cmd++ = CMD(ARseccnt, 0);
	*cmd++ = CMD(ARseccnt, srb->sectors);
	*cmd++ = CMD(ARfea, 0);
	if(llba){
		*cmd++ = CMD(ARlba0, srb->lba[3]);
		*cmd++ = CMD(ARlba0, srb->lba[0]);
		*cmd++ = CMD(ARlba1, srb->lba[4]);
		*cmd++ = CMD(ARlba1, srb->lba[1]);
		*cmd++ = CMD(ARlba2, srb->lba[5]);
		*cmd++ = CMD(ARlba2, srb->lba[2]);
		*cmd++ = CMD(ARdev, 0xe0);
	}else{
		*cmd++ = CMD(ARlba0, srb->lba[0]);
		*cmd++ = CMD(ARlba1, srb->lba[1]);
		*cmd++ = CMD(ARlba2, srb->lba[2]);
		*cmd++ = CMD(ARdev, srb->lba[3] | 0xe0);
	}
	*cmd = CMD(ARcmd, srb->cmd) | 1<<15;
}

static uintptr
advance(uintptr pa, int shift)
{
	int n, mask;

	mask = 0x1F<<shift;
	n = (pa & mask) + (1<<shift);
	return (pa & ~mask) | (n & mask);
}

static void
startsrb(Drive *d, Srb *srb)
{
	int i;
	Edma *edma;
	Prd *prd;
	Tx *tx;

	if(d->nsrb >= nelem(d->srb)){
		srb->next = nil;
		if(d->srbhead)
			d->srbtail->next = srb;
		else
			d->srbhead = srb;
		d->srbtail = srb;
		return;
	}

	d->nsrb++;
	for(i=0; i<nelem(d->srb); i++)
		if(d->srb[i] == nil)
			break;
	if(i == nelem(d->srb))
		panic("sdmv50xx: no free srbs");
	d->intick = Ticks;
	d->srb[i] = srb;
	edma = d->edma;
	tx = (Tx*)KADDR(edma->txi);
	tx->flag = i<<1 | (srb->req == SDread);
	prd = KADDR(tx->prdpa);
	prd->pa = PCIWADDR(srb->data);
	prd->count = srb->count;
	prd->flag = PRDeot;
	mvsatarequest(tx->regs, srb, d->feat&Dllba);
	coherence();
	edma->txi = advance(edma->txi, 5);
	d->intick = Ticks;
}

enum{
	Rpidx	= 0x1f<<3,
};

static void
completesrb(Drive *d)
{
	Edma *edma;
	Rx *rx;
	Srb *srb;

	edma = d->edma;
	if((edma->ctl & eEnEDMA) == 0)
		return;

	while((edma->rxo&Rpidx) != (edma->rxi&Rpidx)){
		rx = (Rx*)KADDR(edma->rxo);
		if(srb = d->srb[rx->cid]){
			d->srb[rx->cid] = nil;
			d->nsrb--;
			if(rx->cDevSts & ATAbad)
				srb->flag |= SFerror;
			if (rx->cEdmaSts)
				iprint("cEdmaSts: %02ux\n", rx->cEdmaSts);
			srb->sta = rx->cDevSts;
			srb->flag |= SFdone;
			wakeup(srb);
		}else
			iprint("srb missing\n");
		edma->rxo = advance(edma->rxo, 3);
		if(srb = d->srbhead){
			d->srbhead = srb->next;
			startsrb(d, srb);
		}
	}
}

static int
srbdone(void *v)
{
	Srb *srb;

	srb = v;
	return srb->flag & SFdone;
}

/*
 * Interrupts
 */
static void
mv50interrupt(Ureg*, void *v)
{
	int i;
	ulong cause, tk0, m;
	Arb *a;
	Ctlr *ctlr;
	Drive *drive;
	static uint st;

	ctlr = v;
	ilock(ctlr);
	cause = ctlr->lmmio[0x1d60/4];
//	dprint("sd%c: mv50interrupt: %.8lux\n", ctlr->sdev->idno, cause);
	for(i=0; cause && i<ctlr->ndrive; i++)
		if(cause & (3<<(i*2+i/4))){
			drive = &ctlr->drive[i];
			if(drive->edma == 0)
				continue;	/* not ready yet. */
			ilock(drive);
			updatedrive(drive);
			tk0 = Ticks;
			a = ctlr->chip[i/4].arb;
			m = 0x0101 << i%4;
			while(a->ic & m){
				a->ic = ~m;
				completesrb(drive);
				if(TK2MS(Ticks - tk0) > 3000){
					print("%s: irq wedge\n", dnam(drive));
					drive->state = Dreset;
					break;
				}
			}
			iunlock(drive);
		}
	iunlock(ctlr);
}

enum{
	Nms		= 256,
	Midwait		= 16*1024/Nms - 1,
	Mphywait	= 512/Nms - 1,
};

static void
hangck(Drive *d)
{
	Edma *e;

	e = d->edma;
	if(d->nsrb > 0
	&& TK2MS(Ticks - d->intick) > 5*1000
	&& (e->rxo&Rpidx) == (e->rxi&Rpidx)){
		print("%s: drive hung; resetting\n", dnam(d));
		d->state = Dreset;
	}
}

static void
checkdrive(Drive *d, int i)
{
	static ulong s, olds[NCtlr*NCtlrdrv];

	ilock(d);
	s = d->bridge->status;
	if(s != olds[i]){
		dprint("%s: status: %.8lux -> %.8lux: %s\n", dnam(d), olds[i], s, diskstates[d->state]);
		olds[i] = s;
	}
	hangck(d);
	switch(d->state){
	case Dnew:
	case Dmissing:
		switch(s){
		case 0x000:
			break;
		default:
			dprint("%s: unknown state %.8lux\n", dnam(d), s);
		case 0x100:
			if(++d->wait&Mphywait)
				break;
		reset:	d->mode ^= 1;
			dprint("%s: reset; new mode %d\n", dnam(d), d->mode);
			resetdisk(d);
			break;
		case 0x123:
		case 0x113:
			s = d->edma->cmdstat;
			if(s == 0x7f || (s&~ATAobs) != ATAdrdy){
				if((++d->wait&Midwait) == 0)
					goto reset;
			}else if(identifydrive(d) == -1)
				goto reset;
		}
		break;
	case Dready:
		if(s != 0)
			break;
		iprint("%s: pulled: st=%.8lux\n", dnam(d), s); /* never happens */
	case Dreset:
	case Derror:
		dprint("%s reset: mode %d\n", dnam(d), d->mode);
		resetdisk(d);
		break;
	}
	iunlock(d);
}

static void
satakproc(void*)
{
	int i;

	while(waserror())
		;
	for(;;){
		tsleep(&up->sleep, return0, 0, Nms);
		for(i = 0; i < nmvsatadrive; i++)
			checkdrive(mvsatadrive[i], i);
	}
}

static void
initdrive(Drive *d)
{
	edmainit(d);
	d->mode = DMsatai;
	if(d->ctlr->type == 1){
		d->edma->iem = IEM;
		d->bridge = &d->chip->arb->bridge[d->chipx];
	}else{
		d->edma->iem = IEM2;
		d->bridge = &d->chip->edma[d->chipx].port;
//		d->edma->iem = ~(1<<6);
		d->pm2 = Dpreamp;
		if(d->ctlr->lmmio[0x180d8/4] & 1)
			d->pm2 = d->bridge->phymode2 & Mpreamp;
	}
}

static SDev*
mv50pnp(void)
{
	int i, nunit;
	ulong n, *mem;
	uchar *base;
	uvlong io;
	Ctlr *ctlr;
	Drive *d;
	Pcidev *p;
	SDev *head, *tail, *sdev;
	static int ctlrno, done;

	if(done++)
		return nil;

	p = nil;
	head = nil;
	tail = nil;
	while((p = pcimatch(p, 0x11ab, 0)) != nil){
		if(p->ccrb != Pcibcstore || p->ccru + p->ccrp || p->did&0x0f00)
			continue;
		if(p->mem[0].size == 0 || (p->mem[0].bar & 1) != 0)
			continue;
		switch(p->did){
		case 0x5040:
		case 0x5041:
		case 0x5080:
		case 0x5081:
		case 0x6041:
		case 0x6081:
			break;
		default:
			print("mv50pnp: unknown did %ux ignored\n", (ushort)p->did);
			continue;
		}
		if (ctlrno >= NCtlr) {
			print("mv50pnp: too many controllers\n");
			break;
		}
		nunit = (p->did&0xf0) >> 4;
		print("#S/sd%c: Marvell 88sx%ux: %d sata-%s ports with%s flash\n",
			'E' + ctlrno, (ushort)p->did, nunit,
			((p->did&0xf000)==0x6000? "II": "I"),
			(p->did&1? "": "out"));
		io = p->mem[0].bar & ~0xF;
		mem = (ulong*)vmap(io, p->mem[0].size);
		if(mem == nil){
			print("sdmv50xx: can't map %llux\n", io);
			continue;
		}
		if((sdev = malloc(sizeof *sdev)) == nil)
			continue;
		if((ctlr = malloc(sizeof *ctlr)) == nil){
			free(sdev);
			continue;
		}
		ctlr->rid = p->rid;

		/* avert thine eyes!  (what does this do?) */
		mem[0x104f0/4] = 0;
		ctlr->type = (p->did >> 12) & 3;
		if(ctlr->type == 1){
			n = mem[0xc00/4];
			n &= ~(3<<4);
			mem[0xc00/4] = n;
		}

		sdev->ifc = &sdmv50xxifc;
		sdev->ctlr = ctlr;
		sdev->nunit = nunit;
		sdev->idno = 'E';
		ctlr->sdev = sdev;
		ctlr->irq = p->intl;
		ctlr->tbdf = p->tbdf;
		ctlr->pcidev = p;
		ctlr->lmmio = mem;
		ctlr->mmio = (uchar*)mem;
		ctlr->nchip = (nunit+3)/4;
		ctlr->ndrive = nunit;
		ctlr->enabled = 0;
		for(i = 0; i < ctlr->nchip; i++){
			base = ctlr->mmio+0x20000+0x10000*i;
			ctlr->chip[i].arb = (Arb*)base;
			ctlr->chip[i].edma = (Edma*)(base + 0x2000);
		}
		for (i = 0; i < nunit; i++) {
			d = &ctlr->drive[i];
			snprint(d->name, sizeof d->name, "mv50%d.%d", ctlrno, i);
			d->sectors = 0;
			d->ctlr = ctlr;
			d->driveno = ctlrno*NCtlrdrv + i;
			d->chipx = i%4;
			d->chip = &ctlr->chip[i/4];
			d->edma = &d->chip->edma[d->chipx];
			mvsatadrive[d->driveno] = d;
			initdrive(d);
		}
		mvsatactlr[ctlrno] = ctlr;
		nmvsatadrive += nunit;
		ctlrno++;
		if(head)
			tail->next = sdev;
		else
			head = sdev;
		tail = sdev;
	}
	return head;
}

static int
mv50enable(SDev *sdev)
{
	char name[32];
	Ctlr *ctlr;

	dprint("sd%c: enable\n", sdev->idno);

	ctlr = sdev->ctlr;
	if (ctlr->enabled)
		return 1;
	ctlr->enabled = 1;
	kproc("mvsata", satakproc, 0);
	snprint(name, sizeof name, "%s (%s)", sdev->name, sdev->ifc->name);
	intrenable(ctlr->irq, mv50interrupt, ctlr, ctlr->tbdf, name);
	return 1;
}

static int
mv50disable(SDev *sdev)
{
	char name[32];
	int i;
	Ctlr *ctlr;
	Drive *drive;

	dprint("sd%c: disable\n", sdev->idno);

	ctlr = sdev->ctlr;
	ilock(ctlr);
	for(i=0; i<ctlr->sdev->nunit; i++){
		drive = &ctlr->drive[i];
		ilock(drive);
		disabledrive(drive);
		iunlock(drive);
	}
	iunlock(ctlr);
	snprint(name, sizeof name, "%s (%s)", sdev->name, sdev->ifc->name);
	intrdisable(ctlr->irq, mv50interrupt, ctlr, ctlr->tbdf, name);
	return 0;
}

/*
 * Check that there is a disk or at least a hot swap bay in the drive.
 */
static int
mv50verify(SDunit *unit)
{
	Ctlr *ctlr;
	Drive *drive;
	int i;

	dprint("%s: verify\n", unit->name);
	ctlr = unit->dev->ctlr;
	drive = &ctlr->drive[unit->subno];
	ilock(ctlr);
	ilock(drive);
	i = configdrive(ctlr, drive, unit);
	iunlock(drive);
	iunlock(ctlr);

	/*
	 * If ctlr->type == 1, then the drives spin up whenever
	 * the controller feels like it; if ctlr->type == 2, then
	 * they spin up as a result of configdrive.
	 *
	 * If there is a drive in the slot, give it 1.4s to spin up
	 * before returning.  There is a noticeable drag on the
	 * power supply when spinning up fifteen drives
	 * all at once (like in the Coraid enclosures).
	 */
	if(ctlr->type == 2 && i == 0)
		if(!waserror()){
			tsleep(&up->sleep, return0, 0, 1400);
			poperror();
		}
	return 1;
}

/*
 * Check whether the disk is online.
 */
static int
mv50online(SDunit *unit)
{
	Ctlr *ctlr;
	Drive *d;
	int r, s0;
	static int once;

	ctlr = unit->dev->ctlr;
	d = &ctlr->drive[unit->subno];
	r = 0;
	ilock(d);
	s0 = d->state;
	USED(s0);
	if(d->state == Dnew)
		identifydrive(d);
	if(d->drivechange){
		idprint("%s: online: %s -> %s\n", unit->name, diskstates[s0], diskstates[d->state]);
		r = 2;
		unit->sectors = d->sectors;
		unit->secsize = d->secsize;
		d->drivechange = 0;
	} else if(d->state == Dready)
		r = 1;
	iunlock(d);
	return r;
}

/*
 * Register dumps
 */
typedef struct Regs Regs;
struct Regs
{
	ulong offset;
	char *name;
};

static Regs regsctlr[] =
{
	0x0C28, "pci serr# mask",
	0x1D40, "pci err addr low",
	0x1D44, "pci err addr hi",
	0x1D48, "pci err attr",
	0x1D50, "pci err cmd",
	0x1D58, "pci intr cause",
	0x1D5C, "pci mask cause",
	0x1D60, "device micr",
	0x1D64, "device mimr",
};

static Regs regsarb[] =
{
	0x0004,	"arb rqop",
	0x0008,	"arb rqip",
	0x000C,	"arb ict",
	0x0010,	"arb itt",
	0x0014,	"arb ic",
	0x0018,	"arb btc",
	0x001C,	"arb bts",
	0x0020,	"arb bpc",
};

static Regs regsbridge[] =
{
	0x0000,	"bridge status",
	0x0004,	"bridge serror",
	0x0008,	"bridge sctrl",
	0x000C,	"bridge phyctrl",
	0x003C,	"bridge ctrl",
	0x0074,	"bridge phymode",
};

static Regs regsedma[] =
{
	0x0000,	"edma config",
	0x0004,	"edma timer",
	0x0008,	"edma iec",
	0x000C,	"edma iem",
	0x0010,	"edma txbasehi",
	0x0014,	"edma txi",
	0x0018,	"edma txo",
	0x001C,	"edma rxbasehi",
	0x0020,	"edma rxi",
	0x0024,	"edma rxo",
	0x0028,	"edma c",
	0x002C,	"edma tc",
	0x0030,	"edma status",
	0x0034,	"edma iordyto",
/*	0x0100,	"edma pio",
	0x0104,	"edma err",
	0x0108,	"edma sectors",
	0x010C,	"edma lba0",
	0x0110,	"edma lba1",
	0x0114,	"edma lba2",
	0x0118,	"edma lba3",
	0x011C,	"edma cmdstat",
	0x0120,	"edma altstat",
*/
};

static char*
rdregs(char *p, char *e, void *base, Regs *r, int n, char *prefix)
{
	int i;

	for(i=0; i<n; i++)
		p = seprint(p, e, "%s%s%-19s %.8ux\n",
			prefix ? prefix : "", prefix ? ": " : "",
			r[i].name, *(u32int*)((uchar*)base+r[i].offset));
	return p;
}

static int
mv50rctl(SDunit *unit, char *p, int l)
{
	char *e, *op;
	Ctlr *ctlr;
	Drive *drive;

	if((ctlr = unit->dev->ctlr) == nil)
		return 0;
	drive = &ctlr->drive[unit->subno];

	e = p+l;
	op = p;
	if(drive->state == Dready){
		p = seprint(p, e, "model    %s\n", drive->model);
		p = seprint(p, e, "serial   %s\n", drive->serial);
		p = seprint(p, e, "firmware %s\n", drive->firmware);
		p = seprint(p, e, "wwn\t%llux\n", drive->wwn);
		p = seprint(p, e, "flag\t");
		p = pflag(p, e, drive);
	}else
		p = seprint(p, e, "no disk present\n");
	p = seprint(p, e, "geometry %llud %ud\n", drive->sectors, drive->secsize);
	p = rdregs(p, e, drive->bridge, regsbridge, nelem(regsbridge), nil);
	if(0){
		p = rdregs(p, e, drive->chip->arb, regsarb, nelem(regsarb), nil);
		p = rdregs(p, e, drive->bridge, regsbridge, nelem(regsbridge), nil);
		p = rdregs(p, e, drive->edma, regsedma, nelem(regsedma), nil);
	}
	return p-op;
}

static int
mv50wctl(SDunit *unit, Cmdbuf *cb)
{
	Ctlr *ctlr;
	Drive *drive;

	if(strcmp(cb->f[0], "reset") == 0){
		ctlr = unit->dev->ctlr;
		drive = &ctlr->drive[unit->subno];
		ilock(drive);
		drive->state = Dreset;
		iunlock(drive);
		return 0;
	}
	cmderror(cb, Ebadctl);
	return -1;
}

static int
waitready(Drive *d)
{
	ulong s, i;

	for(i = 0; i < 120; i++){
		ilock(d);
		s = d->bridge->status;
		iunlock(d);
		if(s == 0)
			return SDeio;
		if(d->state == Dready)
			return SDok;
		if((i+1)%60 == 0){
			ilock(d);
			resetdisk(d);
			iunlock(d);
		}
		if(!waserror()){
			tsleep(&up->sleep, return0, 0, 1000);
			poperror();
		}
	}
	print("%s: not responding; error\n", dnam(d));
	return SDeio;
}

static long
mv50bio(SDunit *u, int /*lun*/, int write, void *a, long count, uvlong lba)
{
	int n, try, flag;
	uchar *data;
	Ctlr *ctlr;
	Drive *d;
	Srb *srb;

	ctlr = u->dev->ctlr;
	d = ctlr->drive + u->subno;
	try = 0;
	data = a;
retry:
	if(waitready(d) != SDok)
		return -1;
	while(count > 0){
		/*
		 * Max is 128 sectors (64kB) because prd->count is 16 bits.
		 */
		n = count;
		if(n > 128)
			n = 128;
		ilock(d);
		if((d->edma->ctl&eEnEDMA) == 0 && edmaenable(d) == -1){
			iunlock(d);
			goto tryagain;
		}
		srb = srbrw(write, d, data, n, lba);
		startsrb(d, srb);
		iunlock(d);

		while(waserror())
			;
		sleep(srb, srbdone, srb);
		poperror();

		flag = srb->flag;
		freesrb(srb);
		if(flag == 0){
	tryagain:		if(++try == 10){
				print("%s: bad disk\n", dnam(d));
				return -1;
			}
			dprint("%s: retry\n", dnam(d));
			goto retry;
		}
		if(flag & SFerror){
			print("%s: i/o error\n", dnam(d));
			return -1;
		}
		count -= n;
		lba += n;
		data += n*u->secsize;
	}
	return data - (uchar*)a;
}

static int
mv50rio(SDreq *r)
{
	int count, n, status, rw;
	uvlong lba;
	Ctlr *ctlr;
	Drive *d;
	SDunit *unit;

	unit = r->unit;
	ctlr = unit->dev->ctlr;
	d = &ctlr->drive[unit->subno];

	if((status = sdfakescsi(r)) != SDnostatus)
		return r->status = status;
	if((status = sdfakescsirw(r, &lba, &count, &rw)) == SDcheck)
		return status;
	n = mv50bio(r->unit, r->lun, rw, r->data, count, lba);
	if(n == -1)
		return SDeio;
	r->rlen = n;
	return SDok;
}

static void
mkrfis(SDreq *r, Drive *d, Edma *e)
{
	uchar *u;

	u = r->cmd;
	u[Ftype] = 0x34;
	u[Fioport] = 0;
	if((d->feat & Dllba) && (r->ataproto & P28) == 0){
		u[Frerror] = e->err;
		u[Fsc8] = e->seccnt;
		u[Fsc] = e->seccnt;
		u[Flba24] = e->lba0;
		u[Flba0] = e->lba0;
		u[Flba32] = e->lba1;
		u[Flba8] = e->lba1;
		u[Flba40] = e->lba2;
		u[Flba16] = e->lba2;
		u[Fdev] = e->lba3;
		u[Fstatus] = e->cmdstat;
	}else{
		u[Frerror] = e->err;
		u[Fsc] = e->seccnt;
		u[Flba0] = e->lba0;
		u[Flba8] = e->lba1;
		u[Flba16] = e->lba2;
		u[Fdev] = e->lba3;
		u[Fstatus] = e->cmdstat;
	}
}

static int
piocmd(SDreq *r, Drive *d)
{
	uchar *p, *c;
	int n, nsec, i, err;
	Edma *e;
	SDunit *u;

	u = r->unit;

	if(waitready(d) != SDok)
		return SDeio;
	nsec = 0;
	if(u->secsize != 0)
		nsec = r->dlen / u->secsize;
	if(r->dlen < nsec*u->secsize)
		nsec = r->dlen/u->secsize;
	if(nsec > 256)
		error("can't do more than 256 sectors");

	ilock(d);
	e = d->edma;
	if(edmadisable(d, 0) == -1) {
		iunlock(d);
		error("can't disable edma");
	}
	n = satawait(&e->cmdstat, ATAdrdy|ATAbusy, ATAdrdy, 3*1000);
	if(n == 0) {
print("piocmd: notready %.2ux\n", e->cmdstat);
		iunlock(d);
		return sdsetsense(r, SDcheck, 4, 8, 0);
	}
	c = r->cmd;
	if(r->ataproto & P28){
		e->altstat = ATAeIEN;
		e->seccnt = c[Fsc];
		e->err = c[Ffeat];
		e->lba0 = c[Flba0];
		e->lba1 = c[Flba8];
		e->lba2 = c[Flba16];
		e->lba3 = c[Fdev];
		e->cmdstat = c[Fcmd];
	}else{
		e->altstat = ATAeIEN;
		e->seccnt = c[Fsc8];
		e->seccnt = c[Fsc];
		e->err = c[Ffeat];
		e->lba0 = c[Flba24];
		e->lba0 = c[Flba0];
		e->lba1 = c[Flba32];
		e->lba1 = c[Flba8];
		e->lba1 = c[Flba40];
		e->lba2 = c[Flba16];
		e->lba3 = c[Fdev];
		e->cmdstat = c[Fcmd];
	}
	err = 0;

	if((r->ataproto & Pdatam) == Pnd)
		n = satawait(&e->cmdstat, ATAbusy, 0, 3*1000);
	else
		n = satawait(&e->cmdstat, ATAbusy|ATAdrq, ATAdrq, 3*1000);
	if(n == 0 || e->cmdstat & ATAerr){
		err = 1;
		goto lose;
	}
	p = r->data;
	for(; nsec > 0; nsec--)
		for (i = 0; i < u->secsize; i += 2) {
			n = satawait(&e->cmdstat, ATAbusy|ATAdrq, ATAdrq, 300);
			if (n == 0) {
				d->state = Dreset;
				err = 1;
				goto lose;
			}
			if(r->ataproto & Pout){
				n = (ushort)p[i + 1] << 8;
				e->pio = n | p[i];
			} else {
				n = e->pio;
				p[i] = n;
				p[i + 1] = n >> 8;
			}
			microdelay(1);
		}
lose:
	if(nsec == 0)
		r->rlen = r->dlen;
	mkrfis(r, d, e);
	iunlock(d);
	if(err)
		return sdsetsense(r, SDcheck, 4, 8, 0);
	else
		return sdsetsense(r, SDok, 0, 0, 0);
}

/*
 * hack to allow udma mode to be set or unset
 * via direct ata command.  it would be better
 * to move the assumptions about dma mode out
 * of some of the helper functions.
 */
static int
isudm(SDreq *r)
{
	uchar *c;

	c = r->cmd;
	if(c[Fcmd] == 0xef && c[Ffeat] == 0x03){
		if(c[Fsc]&0x40)
			return 1;
		return -1;
	}
	return 0;
}
static int
fisreqchk(Sfis *f, SDreq *r)
{
	if((r->ataproto & Pprotom) == Ppkt)
		return SDnostatus;
	/*
	 * handle oob requests;
	 *    restrict & sanitize commands
	 */
	if(r->clen != 16)
		error(Eio);
	if(r->cmd[0] == 0xf0){
		sigtofis(f, r->cmd);
		r->status = SDok;
		return SDok;
	}
	r->cmd[0] = 0x27;
	r->cmd[1] = 0x80;
	r->cmd[7] |= 0xa0;
	return SDnostatus;
}

static int
badf(SDreq *r, Drive*)
{
print("badf %.2ux %2ux\n", r->cmd[2], r->ataproto);
	return sdsetsense(r, SDcheck, 2, 24, 0);
}

static int
ataio0(SDreq *r, Drive *d)
{
	int (*f)(SDreq*, Drive*);

	f = badf;
	switch(r->ataproto & Pprotom){
	default:
		break;
	case Ppio:
	case Pnd:
		f = piocmd;
		break;
	}
	return f(r, d);
}

static int
mv50ata(SDreq *r)
{
	int status, udm;
	Ctlr *c;
	Drive *d;
	SDunit *u;

	u = r->unit;
	c = u->dev->ctlr;
	d = c->drive + u->subno;
	if((status = fisreqchk(d, r)) != SDnostatus)
		return status;
	udm = isudm(r);
	USED(udm);		/* botch */

//	qlock(d);
	if(waserror()){
//		qunlock(d);
		nexterror();
	}
retry:
	switch(status = ataio0(r, d)){
	default:
		dprint("%s: status %d\n", dnam(d), status);
		break;
	case SDretry:
		dprint("%s: retry\n", dnam(d));
		goto retry;
	case SDok:
		sdsetsense(r, SDok, 0, 0, 0);
		break;
	}
	poperror();
//	qunlock(d);
	return r->status = status;
}


SDifc sdmv50xxifc = {
	"mv50xx",			/* name */

	mv50pnp,			/* pnp */
	nil,				/* legacy */
	mv50enable,			/* enable */
	mv50disable,			/* disable */

	mv50verify,			/* verify */
	mv50online,			/* online */
	mv50rio,				/* rio */
	mv50rctl,				/* rctl */
	mv50wctl,			/* wctl */

	mv50bio,				/* bio */
	nil,				/* probe */
	nil,				/* clear */
	nil,				/* rtopctl */
	nil,
	mv50ata,
};

/*
 * The original driver on which this one is based came with the
 * following notice:
 *
 * Copyright 2005
 * Coraid, Inc.
 *
 * This software is provided `as-is,' without any express or implied
 * warranty.  In no event will the author be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1.  The origin of this software must not be misrepresented; you must
 * not claim that you wrote the original software.  If you use this
 * software in a product, an acknowledgment in the product documentation
 * would be appreciated but is not required.
 *
 * 2.  Altered source versions must be plainly marked as such, and must
 * not be misrepresented as being the original software.
 *
 * 3.  This notice may not be removed or altered from any source
 * distribution.
 */
