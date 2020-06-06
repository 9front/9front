/*
 * marvell odin ii 88se64xx sata/sas controller
 * copyright © 2009 erik quanstrom
 * coraid, inc.
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/sd.h"
#include <fis.h>
#include "../port/led.h"

#define	dprint(...)	if(debug)	print(__VA_ARGS__); else USED(debug)
#define	idprint(...)	if(idebug)	print(__VA_ARGS__); else USED(idebug)
#define	aprint(...)	if(adebug)	print(__VA_ARGS__); else USED(adebug)
#define	Pciwaddrh(a)	0
#define	Pciw64(x)	(uvlong)PCIWADDR(x)
#define	Ticks		MACHP(0)->ticks

/* copied from sdiahci */
enum {
	Dnull		= 0,
	Dmissing		= 1<<0,
	Dnopower	= 1<<1,
	Dnew		= 1<<2,
	Dready		= 1<<3,
	Derror		= 1<<4,
	Dreset		= 1<<5,
	Doffline		= 1<<6,
	Dportreset	= 1<<7,
	Dlast		= 9,
};

static char *diskstates[Dlast] = {
	"null",
	"missing",
	"nopower",
	"new",
	"ready",
	"error",
	"reset",
	"offline",
	"portreset",
};

static char *type[] = {
	"offline",
	"sas",
	"sata",
};

enum{
	Nctlr		= 4,
	Nctlrdrv		= 8,
	Ndrive		= Nctlr*Nctlrdrv,
	Mbar		= 2,
	Mebar		= 4,
	Nqueue		= 32,		/* cmd queue size */
	Qmask		= Nqueue - 1,
	Nregset		= 8,
	Rmask		= 0xffff,
	Nms		= 256,		/* drive check rate */

	Sas		= 1,
	Sata,

	/* cmd bits */
	Error		= 1<<31,
	Done		= 1<<30,
	Noverdict	= 1<<29,
	Creset		= 1<<28,
	Atareset		= 1<<27,
	Sense		= 1<<26,
	Timeout		= 1<<25,
	Response	= 1<<24,
	Active		= 1<<23,

	/* pci registers */
	Phy0		= 0x40,
	Gpio		= 0x44,
	Phy1		= 0x90,
	Gpio1		= 0x94,
	Dctl		= 0xe8,

	/* phy offests */
	Phydisable	= 1<<12,
	Phyrst		= 1<<16,
	Phypdwn		= 1<<20,
	Phyen		= 1<<24,

	/* bar4 registers */
	Gctl		= 0x004/4,
	Gis		= 0x008/4,	/* global interrupt status */
	Pi		= 0x00c/4,	/* ports implemented */
	Flashctl		= 0x030/4,	/* spi flash control */
	Flashcmd	= 0x034/4,	/* flash wormhole */
	Flashdata	= 0x038/4,
	I²cctl		= 0x040/4,	/* i²c control */
	I²ccmd		= 0x044/4,
	I²cdata		= 0x048/4,
	Ptype		= 0x0a0/4,	/* 15:8 auto detect enable; 7:0 sas=1. sata=0 */
	Portcfg0		= 0x100/4,	/* 31:16 register sets 31:16 */
	Portcfg1		= 0x104/4,	/* 31:16 register sets 15:8 tx enable; 7 rx enable */
	Clbase		= 0x108/4,	/* cmd list base; 64 bits */
	Fisbase		= 0x110/4,	/* 64 bits */
	Dqcfg		= 0x120/4,	/* bits 11:0 specify size */
	Dqbase		= 0x124/4,
	Dqwp		= 0x12c/4,	/* delivery queue write pointer */
	Dqrp		= 0x130/4,
	Cqcfg		= 0x134/4,
	Cqbase		= 0x138/4,
	Cqwp		= 0x140/4,	/* hw */
	Coal		= 0x148/4,
	Coalto		= 0x14c/4,	/* coal timeout µs */
	Cis		= 0x150/4,	/* centeral irq status */
	Cie		= 0x154/4,	/* centeral irq enable */
	Csis		= 0x158/4,	/* cmd set irq status */
	Csie		= 0x15c/4,
	Cmda		= 0x1b8/4,
	Cmdd		= 0x1bc/4,
	Gpioa		= 0x270/4,
	Gpiod		= 0x274/4,
	Gpiooff		= 0x100,		/* second gpio offset */

	/* port conf registers; mapped through wormhole */
	Pinfo		= 0x000,
	Paddr		= 0x004,
	Painfo		= 0x00c,		/* attached device info */
	Pawwn		= 0x010,
	Psatactl		= 0x018,
	Pphysts		= 0x01c,
	Psig		= 0x020,		/* 16 bytes */
	Perr		= 0x030,
	Pcrcerr		= 0x034,
	Pwidecfg		= 0x038,
	Pwwn		= 0x080,		/* 12 wwn + ict */

	/* port cmd registers; mapped through “cmd” wormhole */
	Ci		= 0x040,		/* cmd active (16) */
	Task		= 0x080,
	Rassoc		= 0x0c0,
	Pfifo0		= 0x1a8,
	Pfifo1		= 0x1c4,
	Pwdtimer	= 0x13c,

	/* “vendor specific” wormhole */
	Phymode	= 0x001,

	/* gpio wormhole */
	Sgconf0		= 0x000,
	Sgconf1		= 0x004,
	Sgclk		= 0x008,
	Sgconf3		= 0x00c,
	Sgis		= 0x010,		/* interrupt set */
	Sgie		= 0x014,		/* interrupt enable */
	Drivesrc		= 0x020,		/* 4 drives/register; 4 bits/drive */
	Drivectl		= 0x038,		/* same deal */

	/* Gctl bits */
	Reset		= 1<<0,
	Intenable	= 1<<1,

	/* Portcfg0/1 bits */
	Regen		= 1<<16,	/* enable sata regsets 31:16 or 15:0 */
	Xmten		= 1<<8,	/* enable port n transmission */
	Dataunke	= 1<<3,
	Rsple		= 1<<2,	/* response frames in le format */
	Oabe		= 1<<1,	/* oa frame in big endian format */
	Framele		= 1<<0,	/* frame contents in le format */

	Allresrx		= 1<<7,	/* receive all responses */
	Stpretry		= 1<<6,
	Cmdirq		= 1<<5,	/* 1 == self clearing */
	Fisen		= 1<<4,	/* enable fis rx */
	Errstop		= 1<<3,	/* set -> stop on ssp/smp error */
	Resetiss		= 1<<1,	/* reset cmd issue; self clearing */
	Issueen		= 1<<0,

	/* Dqcfg bits */
	Dqen		= 1<<16,

	/* Cqcfg bits */
	Noattn		= 1<<17,	/* don't post entries with attn bit */
	Cqen		= 1<<16,

	/* Cis bits */
	I2cirq		= 1<<31,
	Swirq1		= 1<<30,
	Swirq0		= 1<<29,
	Prderr		= 1<<28,
	Dmato		= 1<<27,
	Parity		= 1<<28,	/* parity error; fatal */
	Slavei2c		= 1<<25,
	Portstop		= 1<<16,	/* bitmapped */
	Portirq		= 1<<8,	/* bitmapped */
	Srsirq		= 1<<3,
	Issstop		= 1<<1,
	Cdone		= 1<<0,
	Iclr		= Swirq1 | Swirq0,

	/* Pis bits */
	Caf		= 1<<29,	/* clear affiliation fail */
	Sync		= 1<<25,	/* sync during fis rx */
	Phyerr		= 1<<24,
	Stperr		= 1<<23,
	Crcerr		= 1<<22,
	Linktx		= 1<<21,
	Linkrx		= 1<<20,
	Martianfis	= 1<<19,
	Anot		= 1<<18,	/* async notification */
	Bist		= 1<<17,
	Sigrx		= 1<<16,	/* native sata signature rx */
	Phyunrdy	= 1<<12,	/* phy went offline*/
	Uilong		= 1<<11,
	Uishort		= 1<<10,
	Martiantag	= 1<<9,
	Bnot		= 1<<8,	/* broadcast noticication */
	Comw		= 1<<7,
	Portsel		= 1<<6,
	Hreset		= 1<<5,
	Phyidto		= 1<<4,
	Phyidfail		= 1<<3,
	Phyidok		= 1<<2,
	Hresetok		= 1<<1,
	Phyrdy		= 1<<0,

	Pisataup		= Phyrdy | Comw | Sigrx,
	Pisasup		= Phyrdy | Comw | Phyidok,
	Piburp		= Sync | Phyerr | Stperr | Crcerr | Linktx |
				Linkrx | Martiantag,
	Pireset		= Phyidfail | Bnot | Phyunrdy | Bist |
				Anot | Martianfis | Bist | Phyidto |
				Hreset,
	Piunsupp	= Portsel,

	/* Psc bits */
	Sphyrdy		= 1<<20,
	Linkrate		= 1<<18,	/* 4 bits */
	Maxrate		= 1<<12,
	Minrate		= 1<<8,
	Sreset		= 1<<3,
	Sbnote		= 1<<2,
	Shreset		= 1<<1,
	Sphyrst		= 1<<0,

	/* Painfo bits */
	Issp		= 1<<19,
	Ismp		= 1<<18,
	Istp		= 1<<17,
	Itype		= 1<<0,	/* two bits */

	/* Psatactl bits */
	Powerctl		= 1<<30,	/* 00 wake; 10 partial 01 slumb */
	Srst		= 1<<29,	/* soft reset */
	Power		= 1<<28,
	Sportsel		= 1<<24,
	Dmahon		= 1<<22,
	Srsten		= 1<<20,
	Dmaxfr		= 1<<18,

	/* phy status bits */
	Phylock		= 1<<9,
	Nspeed		= 1<<4,
	Psphyrdy		= 1<<2,

	/* Task bits; modeled after ahci */
	Eestatus		= 0xff<<24,
	Asdbs		= 1<<18,
	Apio		= 1<<17,
	Adhrs		= 1<<16,
	Eerror		= 0xff<<8,
	Estatus		= 0xff,

	/* Phymode bits */
	Pmnotify		= 1<<24,
	Pmnotifyen	= 1<<23,

	/* Sgconf0 bits */
	Autolen		= 1<<24,	/* 8 bits */
	Manlen		= 1<<16,	/* 8 bits */
	Sdincapt		= 1<<8,	/* capture sdatain on + edge */
	Sdoutch		= 1<<7,	/* change sdataout on - edge
	Sldch		= 1<<6,	/* change sload on - edge
	Sdoutivt		= 1<<5,	/* invert sdataout polarity */
	Ldivt		= 1<<4,
	Sclkivt		= 1<<3,
	Blinkben		= 1<<2,	/* enable blink b */
	Blinkaen		= 1<<1,	/* enable blink a */
	Sgpioen		= 1<<0,

	/* Sgconf1 bits; 4 bits each */
	Sactoff		= 1<<28,	/* stretch activity off; 0/64 - 15/64 */
	Sacton		= 1<<24,	/* 1/64th - 16/64 */
	Factoff		= 1<<20,	/* 0/8 - 15/8; default 1 */
	Facton		= 1<<16,	/* 0/4 - 15/4; default 2 */
	Bhi		= 1<<12,	/* 1/8 - 16/8 */
	Blo		= 1<<8,	/* 1/8 - 16/8 */
	Ahi		= 1<<4,	/* 1/8 - 16/8 */
	Alo		= 1<<0,	/* 1/8 - 16/8 */

	/* Sgconf3 bits */
	Autopat		= 1<<20,	/* 4 bits of start pattern */
	Manpat		= 1<<16,
	Manrep		= 1<<4,	/* repeats; 7ff ≡ ∞ */
	Sdouthalt	= 0<<2,
	Sdoutman	= 1<<2,
	Sdoutauto	= 2<<2,
	Sdoutma		= 3<<2,
	Sdincapoff	= 0<<0,
	Sdinoneshot	= 1<<0,
	Sdinrep		= 2<<0,

	/* Sgie Sgis bits */
	Sgreprem	= 1<<8,	/* 12 bits; not irq related */
	Manrep0		= 1<<1,	/* write 1 to clear */
	Capdone	= 1<<0,	/* capture done */

	/* drive control bits (repeated 4x per drive) */
	Aled		= 1<<5,	/* 3 bits */
	Locled		= 1<<3,	/* 2 bits */
	Errled		= 1<<0,	/* 3 bits */
	Llow		= 0,
	Lhigh		= 1,
	Lblinka		= 2,
	Lblinkaneg	= 3,
	Lsof		= 4,
	Leof		= 5,
	Lblinkb		= 6,
	Lblinkbneg	= 7,

	/* cmd queue bits */
	Dssp		= 1<<29,
	Dsmp		= 2<<29,
	Dsata		= 3<<29,	/* also stp */
	Ditor		= 1<<28,	/* initiator */
	Dsatareg		= 1<<20,
	Dphyno		= 1<<12,
	Dcslot		= 1,

	/* completion queue bits */
	Cgood		= 1<<23,	/* ssp only */
	Cresetdn		= 1<<21,
	Crx		= 1<<20,	/* target mode */
	Cattn		= 1<<19,
	Crxfr		= 1<<18,
	Cerr		= 1<<17,
	Cqdone		= 1<<16,
	Cslot		= 1<<0,	/* 12 bits */

	/* error bits — first word */
	Eissuestp	= 1<<31,	/* cmd issue stopped */
	Epi		= 1<<30,	/* protection info error */
	Eoflow		= 1<<29,	/* buffer overflow */
	Eretry		= 1<<28,	/* retry limit exceeded */
	Eufis		= 1<<27,
	Edmat		= 1<<26,	/* dma terminate */
	Esync		= 1<<25,	/* sync rx during tx */
	Etask		= 1<<24,
	Ererr		= 1<<23,	/* r error received */

	Eroff		= 1<<20,	/* read data offset error */
	Exoff		= 1<<19,	/* xfer rdy offset error */
	Euxr		= 1<<18,	/* unexpected xfer rdy */
	Exflow		= 1<<16,	/* buffer over/underflow */
	Elock		= 1<<15,	/* interlock error */
	Enak		= 1<<14,
	Enakto		= 1<<13,
	Enoak		= 1<<12,	/* conn closed wo nak */
	Eopento		= 1<<11,	/* open conn timeout */
	Epath		= 1<<10,	/* open reject - path blocked */
	Enodst		= 1<<9,	/* open reject - no dest */
	Estpbsy		= 1<<8,	/* stp resource busy */
	Ebreak		= 1<<7,	/* break while sending */
	Ebaddst		= 1<<6,	/* open reject - bad dest */
	Ebadprot	= 1<<5,	/* open reject - proto not supp */
	Erate		= 1<<4,	/* open reject - rate not supp */
	Ewdest		= 1<<3,	/* open reject - wrong dest */
	Ecreditto	= 1<<2,	/* credit timeout */
	Edog		= 1<<1,	/* watchdog timeout */
	Eparity		= 1<<0,	/* buffer parity error */

	/* sas ctl cmd header bits */
	Ssptype		= 1<<5,	/* 3 bits */
	Ssppt		= 1<<4,	/* build your own header *.
	Firstburst	= 1<<3,	/* first burst */
	Vrfylen		= 1<<2,	/* verify length */
	Tlretry		= 1<<1,	/* transport layer retry */
	Piren		= 1<<0,	/* pir present */

	/* sata ctl cmd header bits */
	Lreset		= 1<<7,
	Lfpdma		= 1<<6,	/* first-party dma.  (what's that?) */
	Latapi		= 1<<5,
	Lpm		= 1<<0,	/* 4 bits */

	Sspcmd		= 0*Ssptype,
	Ssptask		= 1*Ssptype,
	Sspxfrdy		= 4*Ssptype,
	Ssprsp		= 5*Ssptype,
	Sspread		= 6*Ssptype,
	Sspwrite		= 7*Ssptype,
};

/* following ahci */
typedef struct {
	ulong	dba;
	ulong	dbahi;
	ulong	pad;
	ulong	count;
} Aprdt;

typedef struct {
	union{
		struct{
			uchar	cfis[0x40];
			uchar	atapi[0x20];
		};
		struct{
			uchar	mfis[0x40];
		};
		struct{
			uchar	sspfh[0x18];
			uchar	sasiu[0x40];
		};
	};
} Ctab;

/* protection information record */
typedef struct {
	uchar	ctl;
	uchar	pad;
	uchar	size[2];
	uchar	rtag[4];
	uchar	atag[2];
	uchar	mask[2];
} Pir;

/* open address frame */
typedef struct {
	uchar	oaf[0x28];
	uchar	fb[4];
} Oaf;

/* status buffer */
typedef struct {
	uchar	error[8];
	uchar	rsp[0x400];
} Statb;

typedef struct {
	uchar	satactl;
	uchar	sasctl;
	uchar	len[2];

	uchar	fislen[2];
	uchar	maxrsp;
	uchar	d0;

	uchar	tag[2];
	uchar	ttag[2];

	uchar	dlen[4];
	uchar	ctab[8];
	uchar	oaf[8];
	uchar	statb[8];
	uchar	prd[8];

	uchar	d3[16];
} Cmdh;

typedef struct Cmd Cmd;
struct Cmd {
	Rendez;
	uint	cflag;

	Cmdh	*cmdh;
	Ctab;
	Oaf;
	Statb;
	Aprdt;
};

typedef struct Drive Drive;
typedef struct Ctlr Ctlr;

struct Drive {
	Lock;
	QLock;
	Ctlr	*ctlr;
	SDunit	*unit;
	char	name[16];

	Cmd	*cmd;

	/* sdscsi doesn't differentiate drivechange/mediachange */
	uchar	drivechange;
	uchar	state;
	uchar	type;
	ushort	info[0x100];

	Sfis;	/* sata and media info*/
	Cfis;	/* sas and media info */
	Ledport;	/* led */

	/* hotplug info */
	uint	lastseen;
	uint	intick;
	uint	wait;

	char	serial[20+1];
	char	firmware[8+1];
	char	model[40+1];
	uvlong	wwn;
	uvlong	sectors;
	uint	secsize;

	uint	driveno;
};

struct Ctlr {
	Lock;
	uchar	enabled;
	SDev	*sdev;
	Pcidev	*pci;
	uint	*reg;

	uint	dq[Nqueue];
	uint	dqwp;
	uint	cq[Nqueue + 1];
	uint	cqrp;
	Cmdh	*cl;
	uchar	*fis;
	Cmd	*cmdtab;

	Drive	drive[Nctlrdrv];
	uint	ndrive;
};

static	Ctlr	msctlr[Nctlr];
static	SDev	sdevs[Nctlr];
static	uint	nmsctlr;
static	Drive	*msdrive[Ndrive];
static	uint	nmsdrive;
static	int	debug=0;
static	int	idebug=1;
static	int	adebug;
static	uint	 olds[Nctlr*Nctlrdrv];
	SDifc	sdodinifc;

/* a good register is hard to find */
static	int	pis[] = {
	0x160/4, 0x168/4, 0x170/4, 0x178/4,
	0x200/4, 0x208/4, 0x210/4, 0x218/4,
};
static	int	pcfg[] = {
	0x1c0/4, 0x1c8/4, 0x1d0/4, 0x1d8/4,
	0x230/4, 0x238/4, 0x240/4, 0x248/4,
};
static	int	psc[] = {
	0x180/4, 0x184/4, 0x188/4, 0x18c/4,
	0x220/4, 0x224/4, 0x228/4, 0x22c/4,
};
static	int	vscfg[] = {
	0x1e0/4, 0x1e8/4, 0x1f0/4, 0x1f8/4,
	0x250/4, 0x258/4, 0x260/4, 0x268/4,
};
#define	sstatus(d)	(d)->ctlr->reg[psc[(d)->driveno]]

static char*
dstate(uint s)
{
	int i;

	for(i = 0; s; i++)
		s >>= 1;
	return diskstates[i];
}

static char*
dnam(Drive *d)
{
	if(d->unit)
		return d->unit->name;
	return d->name;
}

static uvlong border = 0x0001020304050607ull;
static uvlong lorder = 0x0706050403020100ull;

static uvlong
getle(uchar *t, int w)
{
	uint i;
	uvlong r;

	r = 0;
	for(i = w; i != 0; )
		r = r<<8 | t[--i];
	return r;
}

static void
putle(uchar *t, uvlong r, int w)
{
	uchar *o, *f;
	uint i;

	f = (uchar*)&r;
	o = (uchar*)&lorder;
	for(i = 0; i < w; i++)
		t[o[i]] = f[i];
}

static uvlong
getbe(uchar *t, int w)
{
	uint i;
	uvlong r;

	r = 0;
	for(i = 0; i < w; i++)
		r = r<<8 | t[i];
	return r;
}

static void
putbe(uchar *t, uvlong r, int w)
{
	uchar *o, *f;
	uint i;

	f = (uchar*)&r;
	o = (uchar*)&border + (sizeof border-w);
	for(i = 0; i < w; i++)
		t[i] = f[o[i]];
}

static int phyrtab[] = {Phy0, Phy1};
static void
phyenable(Ctlr *c, Drive *d)
{
	uint i, u, reg, m;

	i = d->driveno;
	reg = phyrtab[i > 3];
	i &= 3;
	i = 1<<i;
	u = pcicfgr32(c->pci, reg);
	m = i*(Phypdwn | Phydisable | Phyen);
	if((u & m) == Phyen)
		return;
	m = i*(Phypdwn | Phydisable);
	u &= ~m;
	u |= i*Phyen;
	pcicfgw32(c->pci, reg, u);
}

static void
regtxreset(Drive *d)
{
	uint i, u, m;
	Ctlr *c = d->ctlr;

	i = d->driveno;
	u = c->reg[Portcfg1];
	m = (Regen|Xmten)<<i;
	u &= ~m;
	c->reg[Portcfg1] = u;
	delay(1);
	c->reg[Portcfg1] = u | m;
}

/* aka comreset? */
static void
phyreset(Drive *d)
{
	uint i, u, reg;
	Ctlr *c;

	c = d->ctlr;
	phyenable(c, d);

	i = d->driveno;
	reg = phyrtab[i > 3];
	i &= 3;
	i = 1<<i;
	u = pcicfgr32(c->pci, reg);
	pcicfgw32(c->pci, reg, u | i*Phyrst);
	delay(5);
	pcicfgw32(c->pci, reg, u);

	sstatus(d) |= Shreset;
	while(sstatus(d) & Shreset);
		;
}

static void
reset(Drive *d)
{
	regtxreset(d);
	phyreset(d);
}

/*
 * sata/sas register reads through wormhole
 */
static uint
ssread(Ctlr *c, int port, uint r)
{
	c->reg[Cmda] = r + 4*port;
	return c->reg[Cmdd];
}

static void
sswrite(Ctlr *c, int port, int r, uint u)
{
	c->reg[Cmda] = r + 4*port;
	c->reg[Cmdd] = u;
}

/*
 * port configuration r/w through wormhole
 */
static uint
pcread(Ctlr *c, uint port, uint r)
{
	c->reg[pcfg[port]] = r;
	return c->reg[pcfg[port] + 1];
}

static void
pcwrite(Ctlr *c, uint port, uint r, uint u)
{
	c->reg[pcfg[port] + 0] = r;
	c->reg[pcfg[port] + 1] = u;
}

/*
 * vendor specific r/w through wormhole
 */
static uint
vsread(Ctlr *c, uint port, uint r)
{
	c->reg[vscfg[port]] = r;
	return c->reg[vscfg[port] + 1];
}

static void
vswrite(Ctlr *c, uint port, uint r, uint u)
{
	c->reg[vscfg[port] + 0] = r;
	c->reg[vscfg[port] + 1] = u;
}

/*
 * gpio wormhole
 */
static uint
gpread(Ctlr *c, uint r)
{
	c->reg[Gpioa] = r;
	return c->reg[Gpiod];
}

static void
gpwrite(Ctlr *c, uint r, uint u)
{
	c->reg[Gpioa] = r;
	c->reg[Gpiod] = u;
}

static uint*
getsigfis(Drive *d, uint *fis)
{
	uint i;

	for(i = 0; i < 4; i++)
		fis[i] = pcread(d->ctlr, d->driveno, Psig + 4*i);
	return fis;
}

static uint
getsig(Drive *d)
{
	uint fis[4];

	return fistosig((uchar*)getsigfis(d, fis));
}

static uint
ci(Drive *d)
{
	return ssread(d->ctlr, d->driveno, Ci);
}

static void
unsetci(Drive *d)
{
	uint i;

	i = 1<<d->driveno;
	sswrite(d->ctlr, d->driveno, Ci, i);
	while(ci(d) & i)
		microdelay(1);
}

static uint
gettask(Drive *d)
{
	return ssread(d->ctlr, d->driveno, Task);
}

static void
tprint(Drive *d, uint t)
{
	uint s;

	s = sstatus(d);
	dprint("%s: err task %ux sstat %ux\n", dnam(d), t, s);
}

static int
cmdactive(void *v)
{
	Cmd *x;

	x = v;
	return (x->cflag & Done) != 0;
}

static int
mswait(Cmd *x, int ms)
{
	uint u, tk0;

	if(up){
		tk0 = Ticks;
		while(waserror())
			;
		tsleep(x, cmdactive, x, ms);
		poperror();
		ms -= TK2MS(Ticks - tk0);
	}else
		while(ms-- && cmdactive(x))
			delay(1);
//	ilock(cmd->d);
	u = x->cflag;
	x->cflag = 0;
//	iunlock(cmd->d)

	if(u == (Done | Active))
		return 0;
	if((u & Done) == 0){
		u |= Noverdict | Creset | Timeout;
		print("cmd timeout ms:%d %ux\n", ms, u);
	}
	return u;
}

static void
setstate(Drive *d, int state)
{
	ilock(d);
	d->state = state;
	iunlock(d);
}

static void
esleep(int ms)
{
	if(waserror())
		return;
	tsleep(&up->sleep, return0, 0, ms);
	poperror();
}

static int
waitready(Drive *d)
{
	ulong s, i, δ;

	for(i = 0; i < 15000; i += 250){
		if(d->state & (Dreset | Dportreset | Dnew))
			return 1;
		δ = Ticks - d->lastseen;
		if(d->state == Dnull || δ > 10*1000)
			return -1;
		ilock(d);
		s = sstatus(d);
		iunlock(d);
		if((s & Sphyrdy) == 0 && δ > 1500)
			return -1;
		if(d->state == Dready && (s & Sphyrdy))
			return 0;
		esleep(250);
	}
	print("%s: not responding; offline: %.8ux\n", dnam(d), sstatus(d));
	setstate(d, Doffline);
	return -1;
}

static int
lockready(Drive *d)
{
	int i, r;

	for(i = 0; ; i++){
		qlock(d);
		if((r = waitready(d)) != 1)
			return r;
		qunlock(d);
		if(i == Nms*10)
			break;
		esleep(1);
	}
	return -1;
}

static int
command(Drive *d, uint cmd, int ms)
{
	uint s, n, m, i;
	Ctlr *c;

	c = d->ctlr;
	i = d->driveno;
	m = 1<<i;
	n = cmd | Ditor | i*Dsatareg | m*Dphyno | i*Dcslot;
//	print("cqwp\t%.8ux : n %ux : d%d; \n", c->cq[0], n, i);
	d->cmd->cflag = Active;
	ilock(c);
	s = c->dqwp++;
	c->dq[s&Qmask] = n;
	c->reg[Dqwp] = s&Qmask;
	iunlock(c);
//	print("	dq slot %d\n", s);
	d->intick = Ticks;		/* move to mswait? */
	return mswait(d->cmd, ms);
}

static int
buildfis(Drive *d, SDreq *r, void *data, int n)
{
	Aprdt *p;
	Cmd *x;
	Cmdh *h;

	x = d->cmd;
	memmove(x->cfis, r->cmd, r->clen);

	h = x->cmdh;
	memset(h, 0, 16);
	h->fislen[0] = 5;
	h->len[0] = 0;

	if(data != nil){
		h->len[0] = 1;
		p = x;
		p->dba = PCIWADDR(data);
		p->dbahi = Pciwaddrh(data);
		p->count = n;
	}
	return command(d, Dsata, 10*1000);
}

static int
build(Drive *d, int rw, void *data, int n, vlong lba)
{
	Aprdt *p;
	Cmd *x;
	Cmdh *h;

	x = d->cmd;
	rwfis(d, x->cfis, rw, n, lba);

	h = x->cmdh;
	memset(h, 0, 16);
	h->fislen[0] = 5;
	h->len[0] = 1;			/* one prdt entry */

	p = x;
	p->dba = PCIWADDR(data);
	p->dbahi = Pciwaddrh(data);
	p->count = d->secsize*n;

	return command(d, Dsata, 10*1000);
}

enum{
	Rnone	= 1,
	Rdma	= 0x00,		/* dma setup; length 0x1b */
	Rpio	= 0x20,		/* pio setup; length 0x13 */
	Rd2h	= 0x40,		/* d2h register;length 0x13 */
	Rsdb	= 0x58,		/* set device bits; length 0x08 */
};

static uint fisotab[8] = {
[0]	Rnone,
[1]	Rd2h,
[2]	Rpio,
[3]	Rnone,
[4]	Rsdb,
[5]	Rnone,
[6]	Rnone,
[7]	Rnone,
};

static uint
fisoffset(Drive *d, int mustbe)
{
	uint t, r;

	t = gettask(d) & 0x70000;
	r = fisotab[t >> 16];
	if(r == Rnone || (mustbe != 0 && r != mustbe))
		return 0;
	return 0x800 + 0x100*d->driveno + r;
}

/* need to find a non-atapi-specific way of doing this */
static uint
atapixfer(Drive *d, uint n)
{
	uchar *u;
	uint i, x;

	if((i = fisoffset(d, Rd2h)) == 0)
		return 0;
	u = d->ctlr->fis + i;
	x = u[Flba16]<<8 | u[Flba8];
	if(x > n){
		x = n;
		print("%s: atapixfer %ux %ux\n", dnam(d), x, n);
	}
	return x;
}

static int
buildpkt(Drive *d, SDreq *r, void *data, int n)
{
	int rv;
	Aprdt *p;
	Cmd *x;
	Cmdh *h;

	x = d->cmd;
	atapirwfis(d, x->cfis, r->cmd, r->clen, n);

	h = x->cmdh;
	memset(h, 0, 16);
	h->satactl = Latapi;
	h->fislen[0] = 5;
	h->len[0] = 1;		/* one prdt entry */

	if(data != nil){
		p = x;
		p->dba = PCIWADDR(data);
		p->dbahi = Pciwaddrh(data);
		p->count = n;
	}
	rv = command(d, Dsata, 10*1000);
	if(rv == 0)
		r->rlen = atapixfer(d, n);
	return rv;
}

/*
 * ata 7, required for sata, requires that all devices "support"
 * udma mode 5,   however sata:pata bridges allow older devices
 * which may not.  the innodisk satadom, for example allows
 * only udma mode 2.  on the assumption that actual udma is
 * taking place on these bridges, we set the highest udma mode
 * available, or pio if there is no udma mode available.
 */
static int
settxmode(Drive *d, uchar f)
{
	Cmd *x;
	Cmdh *h;

	x = d->cmd;
	if(txmodefis(d, x->cfis, f) == -1)
		return 0;

	h = x->cmdh;
	memset(h, 0, 16);
	h->fislen[0] = 5;

	return command(d, Dsata, 3*1000);
}

static int
setfeatures(Drive *d, uchar f, uint w)
{
	Cmd *x;
	Cmdh *h;

	x = d->cmd;
	featfis(d, x->cfis, f);

	h = x->cmdh;
	memset(h, 0, 16);
	h->fislen[0] = 5;

	return command(d, Dsata, w);
}

static int
mvflushcache(Drive *d)
{
	Cmd *x;
	Cmdh *h;

	x = d->cmd;
	flushcachefis(d, x->cfis);

	h = x->cmdh;
	memset(h, 0, 16);
	h->fislen[0] = 5;

	return command(d, Dsata, 60*1000);
}

static int
identify0(Drive *d, void *id)
{
	Aprdt *p;
	Cmd *x;
	Cmdh *h;

	x = d->cmd;
	identifyfis(d, x->cfis);

	h = x->cmdh;
	memset(h, 0, 16);
	h->fislen[0] = 5;
	h->len[0] = 1;		/* one prdt entry */

	memset(id, 0, 0x200);
	p = x;
	p->dba = PCIWADDR(id);
	p->dbahi = Pciwaddrh(id);
	p->count = 0x200;

	return command(d, Dsata, 3*1000);
}

static int
identify(Drive *d)
{
	int i, n;
	vlong osectors, s;
	uchar oserial[21];
	ushort *id;
	SDunit *u;

	id = d->info;
	for(i = 0;; i++){
		if(i > 5 || identify0(d, id) != 0)
			return -1;
		n = idpuis(id);
		if(n & Pspinup && setfeatures(d, 7, 20*1000) == -1)
			dprint("%s: puis spinup fail\n", dnam(d));
		if(n & Pidready)
			break;
	}

	s = idfeat(d, id);
	if(s == -1)
		return -1;
	if((d->feat&Dlba) == 0){
		dprint("%s: no lba support\n", dnam(d));
		return -1;
	}
	osectors = d->sectors;
	memmove(oserial, d->serial, sizeof d->serial);

	d->sectors = s;
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
	memmove(u->inquiry+8, d->model, 40);

	if(osectors != s || memcmp(oserial, d->serial, sizeof oserial)){
		d->drivechange = 1;
		u->sectors = 0;
	}
	return 0;
}

/* open address fises */
enum{
	Initiator		= 0x80,
	Openaddr	= 1,
	Awms		= 0x8000,
	Smp		= 0,
	Ssp		= 1,
	Stp		= 2,
	Spd15		= 8,
	Spd30		= 9,
};

static void
oafis(Cfis *f, uchar *c, int type)
{
	c[0] = Initiator | type<<4 | Openaddr;
	c[1] = Spd30;				/* botch; just try 3gbps */
	if(type == Smp)
		memset(c + 2, 0xff, 2);
	else
		memmove(c + 2, f->ict, 2);
	memmove(c + 4, f->tsasaddr, 8);		/* dest "port identifier" §4.2.6 */
	memmove(c + 12, f->ssasaddr, 8);
}

/* sas fises */
static int
sasfis(Cfis*, uchar *c, SDreq *r)
{
	memmove(c, r->cmd, r->clen);
	if(r->clen < 16)
		memset(c + r->clen, 0, 16 - r->clen);
	return 0;
}

/* sam3 §4.9.4 single-level lun structure */
static void
scsilun8(uchar *c, int l)
{
	memset(c, 0, 8);
	if(l < 255)
		c[1] = l;
	else if(l < 16384){
		c[0] = 1<<6 | l>>8;
		c[1] = l;
	}else
		print("bad lun %d\n", l);
}

static void
iuhdr(SDreq *r, uchar *c, int fburst)
{
	scsilun8(c, r->lun);
	c[8] = 0;
	c[9] = 0;
	if(fburst)
		c[9] = 0x80;
}

static void
ssphdr(Cfis *x, uchar *c, int ftype)
{
	memset(c, 0, 0x18);
	c[0] = ftype;
	sasbhash(c + 1, x->tsasaddr);
	sasbhash(c + 5, x->ssasaddr);
}

/* debugging */
static void
dump(uchar *u, uint n)
{
	uint i;

	if(n > 100)
		n = 100;
	for(i = 0; i < n; i += 4){
		print("%.2d  %.2ux%.2ux%.2ux%.2ux", i, u[i], u[i + 1], u[i + 2], u[i + 3]);
		print("\n");
	}
}

static void
prsense(uchar *u, uint n)
{
	print("sense data %d: \n", n);
	dump(u, n);
}

static void
priu(uchar *u, uint n)
{
	print("iu %d: \n", n);
	dump(u, n);
}

/*
 * other suspects:
 * key	asc/q
 * 02	0401	becoming ready
 * 	040b	target port in standby state
 * 	0b01	overtemp
 * 	0b0[345]	background *
 * 	0c01	write error - recovered with auto reallocation
 * 	0c02	write error - auto reallocation failed
 * 	0c03	write error - recommend reassignment
 * 	17*	recovered data
 * 	18*	recovered data
 * 	5d*	smart-style reporting (disk/smart handles)
 * 	5e*	power state change
 */

static int
classifykey(int asckey)
{
	if(asckey == 0x062901 || asckey == 0x062900){
		/* power on */
		dprint("power on sense\n");
		return SDretry;
	}
	return SDcheck;
}

/* spc3 §4.5 */
static int
sasrspck(Drive *d, SDreq *r, int min)
{
	char *p;
	int rv;
	uchar *u, *s;
	uint l, fmt, n, keyasc;

	u = d->cmd->rsp;
	s = u + 24;
	dprint("status %d datapres %d\n", u[11], u[10]);
	switch(u[10]){
	case 1:
		l = getbe(u + 20, 4);
		/*
		 * this is always a bug because we don't do
		 * task mgmt
		 */
		print("%s: bug: task data %d min %d\n", dnam(d), l, min);
		return SDcheck;
	case 2:
		l = getbe(u + 16, 4);
		n = sizeof r->sense;
		if(l < n)
			n = l;
		memmove(r->sense, s, n);
		fmt = s[0] & 0x7f;
		keyasc = (s[2] & 0xf)<<16 | s[12]<<8 | s[13];
		rv = SDcheck;
		/* spc3 §4.5.3; 0x71 is deferred. */
		if(n >= 18 && (fmt == 0x70 || fmt == 0x71)){
			rv = classifykey(keyasc);
			p = "";
			if(rv == SDcheck){
				r->flags |= SDvalidsense;
				p = "valid";
			}
			dprint("sense %.6ux %s\n", keyasc, p);
		}else
			prsense(s, l);
		return rv;
	default:
		print("%s: sasrspck: spurious\n", dnam(d));
		priu(u, 24);
		prsense(s, 0x30);
		return SDcheck;
	}
}

static int
buildsas(Drive *d, SDreq *r, uchar *data, int n)
{
	int w, try, fburst;
	Aprdt *p;
	Cmd *x;
	Cmdh *h;

	try = 0;
top:
	fburst = 0;		/* Firstburst? */
	x = d->cmd;
	/* ssphdr(d, x->sspfh, 6); */
	iuhdr(r, x->sasiu, fburst);
	w = 0;
	if(r->clen > 16)
		w = r->clen - 16 + 3>> 2;
	x->sasiu[11] = w;
	sasfis(d, x->sasiu + 12, r);

	h = x->cmdh;
	memset(h, 0, 16);
	h->sasctl = Tlretry | /*Vrfylen |*/ Sspcmd | fburst;
	h->fislen[0] = sizeof x->sspfh + 12 + 16 + 4*w >> 2;
	h->maxrsp = 0xff;
	if(n)
		h->len[0] = 1;
	h->ttag[0] = 1;
	*(uint*)h->dlen = n;

	if(n){
		p = x;
		p->dba = PCIWADDR(data);
		p->dbahi = Pciwaddrh(data);
		p->count = n;
	}
	switch(w = command(d, Dssp, 10*1000)){
	case 0:
		r->status = sdsetsense(r, SDok, 0, 0, 0);
		return 0;
	case Response | Done | Active:
		r->status = sasrspck(d, r, 0);
		if(r->status == SDok)
			return 0;
		if(r->status == SDretry){
			if(try++ < 2)
				goto top;
			r->status |= SDvalidsense;
		}
		return w | Sense;
	default:
		r->status = sdsetsense(r, SDcheck, 4, 24, 0);
		return w;
	}
}

static uint
analyze(Drive *d, Statb *b)
{
	uint u, r, t;

	r = 0;
	u = *(uint*)b->error;
	if(u & Eissuestp){
		r |= Error;
		unsetci(d);
	}
	if(u & Etask && (d->feat & Datapi) == 0){
		t = gettask(d);
		if(t & 1)
			tprint(d, t);
		if(t & Efatal<<8 || t & (ASbsy|ASdrq))
			r |= Noverdict|Atareset;
		if(t&Adhrs && t&33)
			r |= Noverdict|Atareset;
		else
			r |= Error;
	}
	if(u & (Ererr | Ebadprot)){
		/* sas thing */
		print("%s: sas error %.8ux\n", dnam(d), u);
		r |= Error;
	}
	if(u & ~(Ebadprot | Ererr | Etask | Eissuestp))
		print("%s: analyze %.8ux\n", dnam(d), u);

	return r;
}

static void
updatedone(Ctlr *c)
{
	uint a, e, i, u, slot;
	Cmd *x;
	Drive *d;

	e = c->cq[0];
	if(e == 0xfff)
		return;
	if(e > Qmask)
		print("sdodin: bug: bad cqrp %ux\n", e);
	e = e+1 & Qmask;
	for(i = c->cqrp; i != e; i = i+1 & Qmask){
		u = c->cq[1 + i];
		c->cq[1 + i] = 0;
		slot = u & 0xfff;
		u &= ~slot;
		d = c->drive + slot;
  		x = d->cmd;
		if(u & Cqdone){
			x->cflag |= Done;
			u &= ~Cqdone;
		}
		if(u & (Crxfr | Cgood)){
			if((u & Cgood) == 0)
				x->cflag |= Response;
			u &= ~(Crxfr | Cgood);
		}
		if(u & Cerr){
			dprint("%s: Cerr ..\n", dnam(d));
			a = analyze(d, x);
			x->cflag |= Done | a;
			u &= ~Cerr;
		}
		if(x->cflag & Done)
			wakeup(x);
		if(u)
			print("%s: odd bits %.8ux\n", dnam(d), u);
	}
if(i == c->cqrp)print("odin: spur done\n");
	c->cqrp = i;
}

static void
updatedrive(Drive *d)
{
	uint cause, s0, ewake;
	char *name;
	Cmd *x;
	static uint last, tk;

	ewake = 0;
	cause = d->ctlr->reg[pis[d->driveno]];
	d->ctlr->reg[pis[d->driveno]] = cause;
	x = d->cmd;
	name = dnam(d);

	if(last != cause || Ticks - tk > 5*1000){
		dprint("%s: ca %ux ta %ux\n", name, cause, gettask(d));
		tk = Ticks;
	}
	if(cause & (Phyunrdy | Phyidto | Pisataup | Pisasup)){
		s0 = d->state;
		if(cause == (Phyrdy | Comw)){
			d->type = 0;
			d->state = Dnopower;
		}
		switch(cause & (Phyunrdy | Phyidto | Phyidok | Sigrx)){
		case Phyunrdy:
			d->state = Dmissing;
			if(sstatus(d) & Sphyrdy){
				if(d->type != 0)
					d->state = Dnew;
				else
					d->state = Dreset;
			}
			break;
		case Phyidto:
			d->type = 0;
			d->state = Dmissing;
			break;
		case Phyidok:
			d->type = Sas;
			d->state = Dnew;
			break;
		case Sigrx:
			d->type = Sata;
			d->state = Dnew;
			break;
		}
		dprint("%s: %s → %s [Apcrs] %s %ux\n", name, dstate(s0),
			dstate(d->state), type[d->type], sstatus(d));
		if(s0 == Dready && d->state != Dready)
			idprint("%s: pulled\n", name);
		if(d->state != Dready || ci(d))
			ewake |= Done | Noverdict;
	}else if(cause & Piburp)
		ewake |= Done | Noverdict;
	else if(cause & Pireset)
		ewake |= Done | Noverdict | Creset;
	else if(cause & Piunsupp){
		print("%s: unsupported h/w: %.8ux\n", name, cause);
		ewake |= Done | Error;
		d->type = 0;
		d->state = Doffline;
	}
	if(ewake){
		dprint("%s: ewake %.8ux\n", name, ewake);
		unsetci(d);
		x->cflag |= ewake;
		wakeup(x);
	}
	last = cause;
}

static int
satareset(Drive *d)
{
	ilock(d->ctlr);
	unsetci(d);
	iunlock(d->ctlr);
	if(gettask(d) & (ASdrq|ASbsy))
		return -1;
	if(settxmode(d, d->udma) != 0)
		return -1;
	return 0;
}

static int
msriopkt(SDreq *r, Drive *d)
{
	int n, count, try, max, flag, task;
	uchar *cmd;

	cmd = r->cmd;
	aprint("%02ux %02ux %c %d %p\n", cmd[0], cmd[2], "rw"[r->write],
		r->dlen, r->data);
	r->rlen = 0;
	count = r->dlen;
	max = 65536;

	for(try = 0; try < 10; try++){
		n = count;
		if(n > max)
			n = max;
		if(lockready(d) == -1)
			return SDeio;
		flag = buildpkt(d, r, r->data, n);
		task = gettask(d);
		if(flag & Atareset && satareset(d) == -1)
			setstate(d, Dreset);
		qunlock(d);
		if(flag & Noverdict){
			if(flag & Creset)
				setstate(d, Dreset);
			print("%s: retry\n", dnam(d));
			continue;
		}
		if(flag & Error){
			if((task & Eidnf) == 0)
				print("%s: i/o error %ux\n", dnam(d), task);
			return r->status = SDcheck;
		}
		return r->status = SDok;
	}
	print("%s: bad disk\n", dnam(d));
	return r->status = SDcheck;
}

static int
msriosas(SDreq *r, Drive *d)
{
	int try, flag;

	for(try = 0; try < 10; try++){
		if(lockready(d) == -1)
			return SDeio;
		flag = buildsas(d, r, r->data, r->dlen);
		qunlock(d);
		if(flag & Noverdict){
			if(flag & Creset)
				setstate(d, Dreset);
			print("%s: retry\n", dnam(d));
			continue;
		}
		if(flag & Error){
			print("%s: i/o error\n", dnam(d));
			return r->status = SDcheck;
		}
		r->rlen = r->dlen;	/* fishy */
		return r->status;		/* set in sasrspck */

	}
	print("%s: bad disk\n", dnam(d));
	sdsetsense(r, SDcheck, 3, r->write? 0xc00: 0x11, 0);
	return r->status = SDcheck;
}

static int
flushcache(Drive *d)
{
	int i;

	i = -1;
	if(lockready(d) == 0)
		i = mvflushcache(d);
	qunlock(d);
	return i;
}

static int
msriosata(SDreq *r, Drive *d)
{
	char *name;
	int i, n, count, try, max, flag, task;
	uvlong lba;
	uchar *cmd, *data;
	SDunit *unit;

	unit = r->unit;
	cmd = r->cmd;
	name = dnam(d);

	if(cmd[0] == 0x35 || cmd[0] == 0x91){
		if(flushcache(d) == 0)
			return sdsetsense(r, SDok, 0, 0, 0);
		return sdsetsense(r, SDcheck, 3, 0xc, 2);
	}
	if((i = sdfakescsi(r)) != SDnostatus){
		r->status = i;
		return i;
	}
	if((i = sdfakescsirw(r, &lba, &count, nil)) != SDnostatus)
		return i;
	max = 128;
	if(d->feat & Dllba)
		max = 65536;
	try = 0;
	data = r->data;
	while(count > 0){
		n = count;
		if(n > max)
			n = max;
		if(lockready(d) == -1)
			return SDeio;
		flag = build(d, r->write, data, n, lba);
		task = gettask(d);
		if(flag & Atareset && satareset(d) == -1)
			setstate(d, Dreset);
		qunlock(d);
		if(flag & Noverdict){
			if(flag & Creset)
				setstate(d, Dreset);
			if(++try == 2){
				print("%s: bad disk\n", name);
				return r->status = SDeio;
			}
			iprint("%s: retry %lld [%.8ux]\n", name, lba, task);
			continue;
		}
		if(flag & Error){
			iprint("%s: i/o error %ux @%,lld\n", name, task, lba);
			return r->status = SDeio;
		}
		count -= n;
		lba   += n;
		data += n*unit->secsize;
	}
	r->rlen = data - (uchar*)r->data;
	r->status = SDok;
	return SDok;
}

static int
msrio(SDreq *r)
{
	Ctlr *c;
	Drive *d;
	SDunit *u;

	u = r->unit;
	c = u->dev->ctlr;
	d = c->drive + u->subno;
	if(d->feat & Datapi)
		return msriopkt(r, d);
	if(d->type == Sas)
		return msriosas(r, d);
	if(d->type == Sata)
		return msriosata(r, d);
	return sdsetsense(r, SDcheck, 3, 0x04, 0x24);
}

/*
 * §6.1.9.5
 * not clear that this is necessary
 * we should know that it's a d2h from the status.
 * pio returns pio setup fises.  hw bug?
 */
static int
sdr(SDreq *r, Drive *d, int st)
{
	uint i;

	if(i = fisoffset(d, 0/*Rd2h*/))
		memmove(r->cmd, d->ctlr->fis + i, 16);
	else
		memset(r->cmd, 0xff, 16);
	r->status = st;
	return st;
}

/*
 * handle oob requests;
 *    restrict & sanitize commands
 */
static int
fisreqchk(Sfis *f, SDreq *r)
{
	uchar *c;

	if((r->ataproto & Pprotom) == Ppkt)
		return SDnostatus;
	if(r->clen != 16)
		error("bad command length"); //error(Eio);
	c = r->cmd;
	if(c[0] == 0xf0){
		sigtofis(f, r->cmd);
		return r->status = SDok;
	}
	c[0] = H2dev;
	c[1] = Fiscmd;
	c[7] |= Ataobs;
	return SDnostatus;
}

static int
msataio(SDreq *r)
{
	char *name;
	int try, flag, task;
	Ctlr *c;
	Drive *d;
	SDunit *u;
	int (*build)(Drive*, SDreq*, void*, int);

	u = r->unit;
	c = u->dev->ctlr;
	d = c->drive + u->subno;
	name = dnam(d);

	if(d->type != Sata)
		error("not sata");
	if(r->cmd[0] == 0xf1){
		d->state = Dreset;
		return r->status = SDok;
	}
	if((r->status = fisreqchk(d, r)) != SDnostatus)
		return r->status;
	build = buildfis;
	if((r->ataproto & Pprotom) == Ppkt)
		build = buildpkt;

	for(try = 0; try < 10; try++){
		if(lockready(d) == -1)
			return SDeio;
		flag = build(d, r, r->data, r->dlen);
		task = gettask(d);
		if(flag & Atareset && satareset(d) == -1)
			setstate(d, Dreset);
		qunlock(d);
		if(flag & Noverdict){
			if(flag & (Timeout | Creset))
				setstate(d, Dreset);
			else if(task & Eabrt<<8){
				/* assume bad cmd */
				r->status = SDeio;
				return SDeio;
			}
			print("%s: retry\n", name);
			continue;
		}
		if(flag & Error){
			print("%s: i/o error %.8ux\n", name, task);
			r->status = SDeio;
			return SDeio;
		}
		if(build != buildpkt)
			r->rlen = r->dlen;
		return sdr(r, d, SDok);
	}
	print("%s: bad disk\n", name);
	return sdr(r, d, SDeio);
}

static void
msinterrupt(Ureg *, void *a)
{
	Ctlr *c;
	uint u, i;
	static uint cnt;

	c = a;
	ilock(c);
	u = c->reg[Cis];
	if(u == 0){
		iunlock(c);
		return;
	}
	c->reg[Cis] = u & ~Iclr;
	if(u != Cdone && cnt++ < 15)
		print("sdodin: irq %s %.8ux\n", c->sdev->ifc->name, u);
	for(i = 0; i < 8; i++)
		if(u & (1<<i)*(Portirq|Portstop))
			updatedrive(c->drive + i);
	if(u & Srsirq){
		u = c->reg[Csis];
		c->reg[Csis] = u;
		for(i = 0; i < 8; i++)
			if(u & 1<<i)
				updatedrive(c->drive + i);
	}
	if(u & Cdone){
		updatedone(c);
		c->reg[Cis] = Cdone;
	}
	iunlock(c);
}

static char*
mc(Drive *d)
{
	char *s;

	s = "";
	if(d->drivechange)
		s = "[newdrive]";
	return s;
}

static int
newsatadrive(Drive *d)
{
	uint task;

	task = gettask(d);
	if((task & 0xffff) == 0x80)
		return SDretry;
	setfissig(d, getsig(d));
	if(identify(d) != 0){
		dprint("%s: identify failure\n", dnam(d));
		return SDeio;
	}
	if(d->feat & Dpower && setfeatures(d, 0x85, 3*1000)  != 0){
		d->feat &= ~Dpower;
		if(satareset(d) == -1)
			return SDeio;
	}
	if(settxmode(d, d->udma)  != 0){
		dprint("%s: can't set tx mode\n", dnam(d));
		return SDeio;
	}
	return SDok;
}

static void
newoaf(Drive *d, int type)
{
	uint ict, i;
	uvlong sa;
	Ctlr *c;

	i = d->driveno;
	c = d->ctlr;

	sa = pcread(c, i, Pawwn + 0);
	sa |= (uvlong)pcread(c, i, Pawwn + 4)<<32;
	putbe(d->tsasaddr, sa, 8);
	memmove(d->ssasaddr, d->ssasaddr, 8);
	ict = pcread(c, i, Pwwn + 8);
	putbe(d->ict, ict, 2);
	oafis(d, d->cmd->oaf, type);
}

static int
sasinquiry(Drive *d)
{
	SDreq r;
	SDunit *u;

	u = d->unit;
	memset(&r, 0, sizeof r);
	r.cmd[0] = 0x12;
	r.cmd[4] = 0xff;
	r.clen = 6;
	r.unit = u;

	return buildsas(d, &r, u->inquiry, sizeof u->inquiry);
}

static int
sastur(Drive *d)
{
	SDreq r;
	SDunit *u;

	u = d->unit;
	memset(&r, 0, sizeof r);
	r.clen = 6;
	r.unit = u;
	return buildsas(d, &r, 0, 0);
}

static int
sasvpd(Drive *d, uchar *buf, int l)
{
	SDreq r;
	SDunit *u;

	u = d->unit;
	memset(&r, 0, sizeof r);
	r.cmd[0] = 0x12;
	r.cmd[1] = 1;
	r.cmd[2] = 0x80;
	r.cmd[4] = l;
	r.clen = 6;
	r.unit = u;
	return buildsas(d, &r, buf, l);
}

static int
sascapacity10(Drive *d, uchar *buf, int l)
{
	SDreq r;
	SDunit *u;

	u = d->unit;
	memset(&r, 0, sizeof r);
	r.cmd[0] = 0x25;
	r.clen = 10;
	r.unit = u;
	return buildsas(d, &r, buf, l);
}

static int
sascapacity16(Drive *d, uchar *buf, int l)
{
	SDreq r;
	SDunit *u;

	u = d->unit;
	memset(&r, 0, sizeof r);
	r.cmd[0] = 0x9e;
	r.cmd[1] = 0x10;
	r.cmd[13] = l;
	r.clen = 16;
	r.unit = u;
	return buildsas(d, &r, buf, l);
}

static void
frmove(char *p, uchar *c, int n)
{
	char *op, *e;

	memmove(p, c, n);
	op = p;
	p[n] = 0;
	for(p = p + n - 1; p > op && *p == ' '; p--)
		*p = 0;
	e = p;
	p = op;
	while(*p == ' ')
		p++;
	memmove(op, p, n - (e - p));
}

static void
chkinquiry(Drive *d, uchar *c)
{
	char buf[32], buf2[32], omod[sizeof d->model];

	memmove(omod, d->model, sizeof d->model);
	frmove(buf, c + 8, 8);
	frmove(buf2, c + 16, 16);
	snprint(d->model, sizeof d->model, "%s %s", buf, buf2);
	frmove(d->firmware, c + 23, 4);
	if(memcmp(omod, d->model, sizeof omod) != 0)
		d->drivechange = 1;
}

static void
chkvpd(Drive *d, uchar *c, int n)
{
	char buf[sizeof d->serial];
	int l;

	l = c[3];
	if(l > n)
		l = n;
	frmove(buf, c + 4, l);
	if(strcmp(buf, d->serial) != 0)
		d->drivechange = 1;
	memmove(d->serial, buf, sizeof buf);
}

static int
adjcapacity(Drive *d, uvlong ns, uint nss)
{
	if(ns != 0)
		ns++;
	if(nss == 2352)
		nss = 2048;
	if(d->sectors != ns || d->secsize != nss){
		d->drivechange = 1;
		d->sectors = ns;
		d->secsize = nss;
	}
	return 0;
}

static int
chkcapacity10(uchar *p, uvlong *ns, uint *nss)
{
	*ns = getbe(p, 4);
	*nss = getbe(p + 4, 4);
	return 0;
}

static int
chkcapacity16(uchar *p, uvlong *ns, uint *nss)
{
	*ns = getbe(p, 8);
	*nss = getbe(p + 8, 4);
	return 0;
}

static int
sasprobe(Drive *d)
{
	uchar buf[0x40];
	int r;
	uint nss;
	uvlong ns;

	if((r = sastur(d)) != 0)
		return r;
	if((r = sasinquiry(d)) != 0)
		return r;
	chkinquiry(d, d->unit->inquiry);
	/* vpd 0x80 (unit serial) is not mandatory */
	if((r = sasvpd(d, buf, sizeof buf)) == 0)
		chkvpd(d, buf, sizeof buf);
	else if(r & (Error | Timeout))
		return r;
	else{
		if(d->serial[0])
			d->drivechange = 1;
		d->serial[0] = 0;
	}
	if((r = sascapacity10(d, buf, sizeof buf)) != 0)
		return r;
	chkcapacity10(buf, &ns, &nss);
	if(ns == 0xffffffff){
		if((r = sascapacity16(d, buf, sizeof buf)) != 0)
			return r;
		chkcapacity16(buf, &ns, &nss);
	}
	adjcapacity(d, ns, nss);

	return 0;
}

static int
newsasdrive(Drive *d)
{
	memset(d->cmd->rsp, 0, sizeof d->cmd->rsp);
	newoaf(d, Ssp);
	switch(sasprobe(d) & (Error | Noverdict | Timeout | Sense)){
	case Error:
	case Timeout:
		return SDeio;
	case Sense:
	case Noverdict:
		return SDretry;
	}
	return SDok;
}

static int
newdrive(Drive *d)
{
	char *t;
	int r;

	memset(&d->Sfis, 0, sizeof d->Sfis);
	memset(&d->Cfis, 0, sizeof d->Cfis);
	qlock(d);
	switch(d->type){
	case Sata:
		r = newsatadrive(d);
		break;
	case Sas:
		r = newsasdrive(d);
		break;
	default:
		print("%s: bug: martian drive %d\n", dnam(d), d->type);
		qunlock(d);
		return -1;
	}
	t = type[d->type];
	switch(r){
	case SDok:
		idprint("%s: %s %,lld sectors\n", dnam(d), t, d->sectors);
		idprint("  %s %s %s %s\n", d->model, d->firmware, d->serial, mc(d));
		setstate(d, Dready);
		break;
	case SDeio:
		idprint("%s: %s can't be initialized\n", dnam(d), t);
		setstate(d, Derror);
	case SDretry:
		break;
	}
	qunlock(d);
	return r;
}

static void
statechange(Drive *d)
{
	switch(d->state){
	case Dmissing:
	case Dnull:
	case Doffline:
		d->drivechange = 1;
		d->unit->sectors = 0;
		break;
	case Dready:
		d->wait = 0;
		break;
	}
}

/*
 * we don't respect running commands.  botch?
 */
static void
checkdrive(Drive *d, int i)
{
	uint s;

	if(d->unit == nil)
		return;
	ilock(d);
	s = sstatus(d);
	d->wait++;
	if(s & Sphyrdy)
		d->lastseen = Ticks;
	if(s != olds[i]){
		dprint("%s: status: %.6ux -> %.6ux: %s\n",
			dnam(d), olds[i], s, dstate(d->state));
		olds[i] = s;
		statechange(d);
	}
	switch(d->state){
	case Dnull:
	case Dmissing:
		if(d->type != 0 && s & Sphyrdy)
			d->state = Dnew;
		break;
	case Dnopower:
		phyreset(d);	/* spinup */
		break;
	case Dnew:
		if(d->wait % 6 != 0)
			break;
		iunlock(d);
		newdrive(d);
		ilock(d);
		break;
	case Dready:
		d->wait = 0;
		break;
	case Derror:
		d->wait = 0;
		d->state = Dreset;
	case Dreset:
		if(d->wait % 40 != 0)
			break;
		reset(d);
		break;
	case Doffline:
	case Dportreset:
		break;
	}
	iunlock(d);
}

static void
mskproc(void*)
{
	int i;

	while(waserror())
		;
	for(;;){
		tsleep(&up->sleep, return0, 0, Nms);
		for(i = 0; i < nmsdrive; i++)
			checkdrive(msdrive[i], i);
	}
}

static void
ledcfg(Ctlr *c, int port, uint cfg)
{
	uint u, r, s;

	r = Drivectl + (port>>2)*Gpiooff;
	s = 15 - port & 3;
	s *= 8;
	u = gpread(c, r);
	u &= ~(0xff << s);
	u |= cfg<<s;
	gpwrite(c, r, u);
}

static uchar ses2ledstd[Ibpilast] = {
[Ibpinone]	Lhigh*Aled,
[Ibpinormal]	Lsof*Aled | Llow*Locled | Llow*Errled,
[Ibpirebuild]	Lsof*Aled | Llow*Locled | Llow*Errled,
[Ibpilocate]	Lsof*Aled | Lblinka*Locled | Llow*Errled,
[Ibpispare]	Lsof*Aled | Llow*Locled| Lblinka*Errled,
[Ibpipfa]		Lsof*Aled | Lblinkb*Locled | Llow*Errled,
[Ibpifail]		Lsof*Aled | Llow*Locled | Lhigh*Errled,
[Ibpicritarray]	Lsof*Aled,
[Ibpifailarray]	Lsof*Aled,
};

static uchar ses2led[Ibpilast] = {
[Ibpinone]	Lhigh*Aled,
[Ibpinormal]	Lsof*Aled | Llow*Locled | Llow*Errled,
[Ibpirebuild]	Lsof*Aled | Lblinkaneg*Locled | Llow*Errled,
[Ibpilocate]	Lsof*Aled | Lhigh*Locled | Llow*Errled,
[Ibpispare]	Lsof*Aled | Lblinka*Locled| Llow*Errled,
[Ibpipfa]		Lsof*Aled | Lblinkb*Locled | Llow*Errled,
[Ibpifail]		Lsof*Aled | Llow*Locled | Lhigh*Errled,
[Ibpicritarray]	Lsof*Aled,
[Ibpifailarray]	Lsof*Aled,
};

static void
setupled(Ctlr *c)
{
	int i, l, blen;
	pcicfgw32(c->pci, Gpio, pcicfgr32(c->pci, Gpio) | 1<<7);

	/*
	 * configure a for 4hz (1/8s on and 1/8s off)
	 * configure b for 1hz (2/8s on and 6/8s off)
	 */
	l = 3 + c->ndrive >> 2;
	blen = 3*24 - 1;
	for(i = 0; i < l*Gpiooff; i += Gpiooff){
		gpwrite(c, Sgconf0 + i, blen*Autolen | Blinkben | Blinkaen | Sgpioen);
		gpwrite(c, Sgconf1 + i, 1*Bhi | 1*Blo | 1*Ahi | 7*Alo);
		gpwrite(c, Sgconf3 + i, 7<<20 | Sdoutauto);
	}
}

static void
trebuild(Ctlr *c, Drive *d, int dno, uint i)
{
	uchar bits;

	if(0 && d->led == Ibpirebuild){
		switch(i%19){
		case 0:
			bits = 0;
			break;
		case 1:
			bits = ses2led[Ibpirebuild] | Lblinka*Locled;
			break;
		case 3:
			bits = ses2led[Ibpirebuild] | Lblinkb*Locled;
			break;
		}
	}else
		bits =  ses2led[d->led];
	if(d->ledbits != bits)
		ledcfg(c, dno, bits);
}

static long
odinledr(SDunit *u, Chan *ch, void *a, long n, vlong off)
{
	Ctlr *c;
	Drive *d;

	c = u->dev->ctlr;
	d = c->drive + u->subno;
	return ledr(d, ch, a, n, off);
}

static long
odinledw(SDunit *u, Chan *ch, void *a, long n, vlong off)
{
	Ctlr *c;
	Drive *d;

	c = u->dev->ctlr;
	d = c->drive + u->subno;
	return ledw(d, ch, a, n, off);
}

/*
 * this kproc can probablly go when i figure out
 * how to program the manual blinker
 */
static void
ledkproc(void*)
{
	uint i, j;
	Drive *d;

	for(i = 0; i < nmsdrive; i++){
		d = msdrive[i];
		d->nled = 2;		/* how to know? */
	}
	for(i = 0; i < nmsctlr; i++)
		pcicfgw32(msctlr[i].pci, Gpio, pcicfgr32(msctlr[i].pci, Gpio) | 1<<7);
	for(i = 0; i < nmsctlr; i++)
		setupled(msctlr + i);
	for(i = 0; ; i++){
		esleep(Nms);
		for(j = 0; j < nmsdrive; j++){
			d = msdrive[j];
			trebuild(d->ctlr, d, j, i);
		}
	}
}

static int
msenable(SDev *s)
{
	char buf[32];
	Ctlr *c;
	static int once;

	c = s->ctlr;
	ilock(c);
	if(c->enabled){
		iunlock(c);
		return 1;
	}
	pcisetbme(c->pci);
	snprint(buf, sizeof buf, "%s (%s)", s->name, s->ifc->name);
	intrenable(c->pci->intl, msinterrupt, c, c->pci->tbdf, buf);
//	c->reg[Cis] |= Swirq1;		/* force initial interrupt. */
	c->enabled = 1;
	iunlock(c);

	if(once++ == 0)
		kproc("odin", mskproc, 0);

	return 1;
}

static int
msdisable(SDev *s)
{
	char buf[32];
	Ctlr *c;

	c = s->ctlr;
	ilock(c);
//	disable(c->hba);
	snprint(buf, sizeof buf, "%s (%s)", s->name, s->ifc->name);
	intrdisable(c->pci->intl, msinterrupt, c, c->pci->tbdf, buf);
	pciclrbme(c->pci);
	c->enabled = 0;
	iunlock(c);
	return 1;
}

static int
scsiish(Drive *d)
{
	return d->type == Sas || d->feat & Datapi;
}

static int
msonline(SDunit *u)
{
	int r;
	Ctlr *c;
	Drive *d;

	c = u->dev->ctlr;
	d = c->drive + u->subno;
	r = 0;

	if(scsiish(d)){
		if(!d->drivechange)
			return r;
		r = scsionline(u);
		if(r > 0)
			d->drivechange = 0;
		return r;
	}
	ilock(d);
	if(d->drivechange){
		r = 2;
		d->drivechange = 0;
		u->sectors = d->sectors;
		u->secsize = d->secsize;
	} else if(d->state == Dready)
		r = 1;
	iunlock(d);
	return r;
}

static void
verifychk(Drive *d)
{
	int w;

	if(!up)
		checkdrive(d, d->driveno);
	for(w = 0; w < 12000; w += 210){
		if(d->state == Dready)
			break;
		if(w > 2000 && d->state != Dnew)
			break;
		if((sstatus(d) & Sphyrdy) == 0)
			break;
		if(!up)
			checkdrive(d, d->driveno);
		esleep(210);
	}
}

static int
msverify(SDunit *u)
{
	int chk;
	Ctlr *c;
	Drive *d;
	static int once;

	c = u->dev->ctlr;
	d = c->drive + u->subno;
	ilock(c);
	ilock(d);
	chk = 0;
	if(d->unit == nil){
		d->unit = u;
		sdaddfile(u, "led", 0644, eve, odinledr, odinledw);
		chk = 1;
	}
	iunlock(d);
	iunlock(c);

	/*
	 * since devsd doesn't know much about hot-plug drives,
	 * we need to give detected drives a chance.
	 */
	if(chk){
		if(++once == nmsctlr)
			kproc("mvled", ledkproc, 0);
		reset(d);
		verifychk(d);
	}
	return 1;
}

static uint*
map(Pcidev *p, int bar)
{
	if(p->mem[bar].size == 0 || (p->mem[bar].bar & 1) != 0)
		return nil;
	return (uint*)vmap(p->mem[bar].bar & ~0xf, p->mem[bar].size);
}

/* §5.1.3 */
static void
initmem(Ctlr *c)
{
	c->fis = malloc(0x800 + 0x100*16);	/* §6.1.9.3 */
	c->cl = malloc(nelem(c->cq)*sizeof *c->cl);
	c->cmdtab = malloc(Nctlrdrv*sizeof *c->cmdtab);
	if(c->fis == nil || c->cl == nil || c->cmdtab == nil)
		panic("sdodin: no memory");
	c->reg[Fisbase + 0] = PCIWADDR(c->fis);
	c->reg[Fisbase + 1] = Pciwaddrh(c->fis);
	c->reg[Cqbase + 0] = PCIWADDR(c->cq);
	c->reg[Cqbase + 1] = Pciwaddrh(c->cq);
	c->reg[Cqcfg] = Cqen | Noattn | nelem(c->cq) - 1;
	c->reg[Dqbase + 0] = PCIWADDR(c->dq);
	c->reg[Dqbase + 1] = Pciwaddrh(c->dq);
	c->reg[Dqcfg] = Dqen | nelem(c->dq);
	c->reg[Clbase + 0] = PCIWADDR(c->cl);
	c->reg[Clbase + 1] = Pciwaddrh(c->cl);
}

/* §5.1.2 */
static void
startup(Ctlr *c)
{
	c->reg[Gctl] |= Reset;
	while(c->reg[Gctl] & Reset)
		;
	initmem(c);
	c->reg[Cie] = Swirq1 | 0xff*Portstop | 0xff*Portirq | Srsirq | Issstop | Cdone;
	c->reg[Gctl] |= Intenable;
	c->reg[Portcfg0] = Rmask*Regen | Dataunke | Rsple | Framele;
	c->reg[Portcfg1] = Rmask*Regen | 0xff*Xmten | /*Cmdirq |*/ Fisen | Resetiss | Issueen;
	c->reg[Csie] = ~0;
	sswrite(c, 0, Pwdtimer, 0x7fffff);
}

static void
forcetype(Ctlr*)
{
	/*
	 * if we want to force sas/sata, here's where to do it.
	 */
}

static void
setupcmd(Drive *d)
{
	int i;
	Ctlr *c;
	Cmd *cmd;
	Cmdh *h;

	i = d->driveno;
	c = d->ctlr;
	d->cmd = c->cmdtab + i;
	d->cmd->cmdh = c->cl + i;
	cmd = d->cmd;
	h = cmd->cmdh;

	/* prep the precomputable bits in the cmd hdr §6.1.4 */
	putle(h->ctab, Pciw64(&cmd->Ctab), sizeof h->ctab);
	putle(h->oaf, Pciw64(&cmd->Oaf), sizeof h->oaf);
	putle(h->statb, Pciw64(&cmd->Statb), sizeof h->statb);
	putle(h->prd, Pciw64(&cmd->Aprdt), sizeof h->prd);

	/* finally, set up the wide-port participating bit */
	pcwrite(c, i, Pwidecfg, 1<<i);
}

static void
phychk(Ctlr *c, Drive *d)
{
	int i;
	uvlong u;
	static uchar src[8] = {0x50, 0x03, 0x04, 0x80};

	i = d->driveno;
	memmove(d->ssasaddr, src, 8);
	u = getbe(d->ssasaddr, 8);
	pcwrite(c, i, Paddr + 0, u);
	pcwrite(c, i, Paddr + 4, u>>32);
}

static SDev*
mspnp(void)
{
	int i, nunit;
	Ctlr *c;
	Drive *d;
	Pcidev *p;
	SDev **ll, *s, *s0;
	static int done;

	if(done++)
		return nil;
	s0 = nil;
	ll = &s0;
	for(p = nil; (p = pcimatch(p, 0x11ab, 0x6485)) != nil; ){
		if(nmsctlr == Nctlr){
			print("sdodin: too many controllers\n");
			break;
		}
		c = msctlr + nmsctlr;
		s = sdevs + nmsctlr;
		memset(c, 0, sizeof *c);
		memset(s, 0, sizeof *s);
		if((c->reg = map(p, Mebar)) == nil){
			print("sdodin: can't map registers\n");
			continue;
		}
		pcienable(p);
		nunit = p->did>>4 & 0xf;
		s->ifc = &sdodinifc;
		s->idno = 'a' + nmsctlr;
		s->ctlr = c;
		c->sdev = s;
		c->pci = p;
		c->ndrive = s->nunit = nunit;
		i = pcicfgr32(p, Dctl) & ~(7<<12);
		pcicfgw32(p, Dctl, i | 4<<12);

		print("#S/sd%c: odin ii sata/sas with %d ports\n", s->idno, nunit);
		startup(c);
		forcetype(c);
		for(i = 0; i < nunit; i++){
			d = c->drive + i;
			d->driveno = i;
			d->sectors = 0;
			d->ctlr = c;
			setupcmd(d);
			snprint(d->name, sizeof d->name, "odin%d.%d", nmsctlr, i);
			msdrive[nmsdrive + i] = d;
//			phychk(c, d);
			c->reg[pis[i] + 1] =
				Sync | Phyerr | Stperr | Crcerr |
				Linkrx | Martianfis | Anot | Bist | Sigrx |
				Phyunrdy | Martiantag | Bnot | Comw |
				Portsel | Hreset | Phyidto | Phyidok |
				Hresetok | Phyrdy;
		}
		nmsdrive += nunit;
		nmsctlr++;
		*ll = s;
		ll = &s->next;
	}
	return s0;
}

static char*
msrctlsata(Drive *d, char *p, char *e)
{
	p = seprint(p, e, "flag\t");
	p = pflag(p, e, d);
	p = seprint(p, e, "udma\t%d\n", d->udma);
	return p;
}

static char*
rctldebug(char *p, char *e, Ctlr *c, Drive *d)
{
	int i;
	uvlong sasid;

	i = d->driveno;
	p = seprint(p, e, "sstatus\t%.8ux\n", sstatus(d));
//	p = seprint(p, e, "cis\t%.8ux %.8ux\n", c->reg[Cis], c->reg[Cie]);
//	p = seprint(p, e, "gis\t%.8ux\n", c->reg[Gis]);
	p = seprint(p, e, "pis\t%.8ux %.8ux\n", c->reg[pis[i]], c->reg[pis[i] + 1]);
	p = seprint(p, e, "sis\t%.8ux\n", c->reg[Csis]);
	p = seprint(p, e, "cqwp\t%.8ux\n", c->cq[0]);
	p = seprint(p, e, "cerror\t%.8ux %.8ux\n", *(uint*)d->cmd->error, *(uint*)(d->cmd->error+4));
	p = seprint(p, e, "task\t%.8ux\n", gettask(d));
	p = seprint(p, e, "ptype\t%.8ux\n", c->reg[Ptype]);
	p = seprint(p, e, "satactl\t%.8ux\n", pcread(c, i, Psatactl));	/* appears worthless */
	p = seprint(p, e, "info	%.8ux %.8ux\n", pcread(c, i, Pinfo), pcread(c, i, Painfo));
	p = seprint(p, e, "physts	%.8ux\n", pcread(c, i, Pphysts));
	p = seprint(p, e, "widecfg	%.8ux\n", pcread(c, i, Pwidecfg));
	sasid = pcread(c, i, Pwwn + 0);
	sasid |= (uvlong)pcread(c, i, Pwwn + 4)<<32;
	p = seprint(p, e, "wwn	%.16llux %.8ux\n", sasid, pcread(c, i, Pwwn + 8));
	sasid = pcread(c, i, Pawwn + 0);
	sasid |= (uvlong)pcread(c, i, Pawwn + 4)<<32;
	p = seprint(p, e, "awwn	%.16llux\n", sasid);
	sasid = pcread(c, i, Paddr + 0);
	sasid |= (uvlong)pcread(c, i, Paddr + 4)<<32;
	p = seprint(p, e, "sasid	%.16llux\n", sasid);
	return p;
}

static int
msrctl(SDunit *u, char *p, int l)
{
	char *e, *op;
	Ctlr *c;
	Drive *d;

	if((c = u->dev->ctlr) == nil)
		return 0;
	d = c->drive + u->subno;
	e = p + l;
	op = p;
	p = seprint(p, e, "state\t%s\n", dstate(d->state));
	p = seprint(p, e, "type\t%s", type[d->type]);
	if(d->type == Sata)
		p = seprint(p, e, " sig %.8ux", getsig(d));
	p = seprint(p, e, "\n");
	if(d->state == Dready){
		p = seprint(p, e, "model\t%s\n", d->model);
		p = seprint(p, e, "serial\t%s\n", d->serial);
		p = seprint(p, e, "firm\t%s\n", d->firmware);
		p = seprint(p, e, "wwn\t%llux\n", d->wwn);
		p = msrctlsata(d, p, e);
	}
	p = rctldebug(p, e, c, d);
	p = seprint(p, e, "geometry %llud %lud\n", d->sectors, u->secsize);
	return p - op;
}

static void
forcestate(Drive *d, char *state)
{
	int i;

	for(i = 1; i < nelem(diskstates); i++)
		if(strcmp(state, diskstates[i]) == 0)
			break;
	if(i == nelem(diskstates))
		error(Ebadctl);
	ilock(d);
	d->state = 1 << i - 1;
	statechange(d);
	iunlock(d);
}

static int
mswctl(SDunit *u, Cmdbuf *cmd)
{
	char **f;
	Ctlr *c;
	Drive *d;

	c = u->dev->ctlr;
	d = c->drive + u->subno;
	f = cmd->f;
	if(strcmp(f[0], "state") == 0)
		forcestate(d, f[1]? f[1]: "null");
	else
		cmderror(cmd, Ebadctl);
	return 0;
}

static int
mswtopctl(SDev*, Cmdbuf *cmd)
{
	char **f;
	int *v;

	f = cmd->f;
	v = 0;
	if(strcmp(f[0], "debug") == 0)
		v = &debug;
	else if(strcmp(f[0], "idprint") == 0)
		v = &idebug;
	else if(strcmp(f[0], "aprint") == 0)
		v = &adebug;
	else
		cmderror(cmd, Ebadctl);
	if(cmd->nf == 1)
		*v ^= 1;
	else if(cmd->nf == 2)
		*v = strcmp(f[1], "on") == 0;
	else
		cmderror(cmd, Ebadarg);
	return 0;
}

SDifc sdodinifc = {
	"odin",
	mspnp,
	nil,
	msenable,
	msdisable,
	msverify,
	msonline,
	msrio,
	msrctl,
	mswctl,
	scsibio,
	nil,		/* probe */
	nil,		/* clear */
	nil,
	mswtopctl,
	msataio,
};
