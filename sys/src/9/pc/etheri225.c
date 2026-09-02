/*
 * intel i225
 * intel i226 ethernet driver
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "../port/netif.h"
#include "../port/etherif.h"
#include "../port/ethermii.h"

enum {
	/* device */
	Rdevctrl    = 0x0000, /* device control */
	Rdevctrlext = 0x0018, /* device control (extended) */
	Rdevstatus  = 0x0004, /* device status */

	Rdevmulti  = 0x5200, /* multicast table */
	Rdevaddr   = 0x5400, /* mac address */
	Rdevaddrhi = 0x5404, /* mac address (high) */

	Rmdi      = 0x0020, /* mdi control */
	Rmdipwr   = 0x0e14, /* mdi power management */

	/* receive */
	Rrxctrl     = 0x0100, /* rx control */
	Rrxraddr    = 0xc000, /* rx ring address */
	Rrxraddrhi  = 0xc004, /* rx ring address (high) */
	Rrxrlen     = 0xc008, /* rx ring length */
	Rrxrsr      = 0xc00c, /* rx ring split replication */
	Rrxrptr     = 0xc010, /* rx ring pointer */
	Rrxrptrtail = 0xc018, /* rx ring pointer tail */
	Rrxrctrl    = 0xc028, /* rx ring control */
	Rrxcsum     = 0x5000, /* rx checksum control */

	/* transmit */
	Rtxctrl     = 0x0400, /* tx control */
	Rtxctrlext  = 0x0404, /* tx control (extended) */
	Rtxraddr    = 0xe000, /* tx ring address */
	Rtxraddrhi  = 0xe004, /* tx ring address (high) */
	Rtxrlen     = 0xe008, /* tx ring length */
	Rtxrptr     = 0xe010, /* tx ring pointer */
	Rtxrptrtail = 0xe018, /* tx ring pointer tail */
	Rtxrctrl    = 0xe028, /* tx ring control */

	/* flow control */
	Rflowaddr   = 0x00028, /* flow control address */
	Rflowaddrhi = 0x0002c, /* flow control address (high) */
	Rflowtype   = 0x00030, /* flow control type */
	Rflowtt     = 0x00170, /* flow control transmit timer */
	Rflowrt     = 0x02160, /* flow control rx threshold */
	Rflowrthi   = 0x02168, /* flow control rx threshold (high) */

	/* eeprom */
	Reeprom      = 0x0010,  /* eeprom control */
	Reepromread  = 0x12014, /* eeprom read */
	Reepromwrite = 0x12018, /* eeprom write */

	/* interrupts */
	Rintrcauseclr = 0x1500, /* interrupt cause clear */
	Rintrcause    = 0x1504, /* interrupt cause */
	Rintrmask     = 0x1508, /* interrupt mask */
	Rintrmaskclr  = 0x150c, /* interrupt mask clear */
	Rintrmaskauto = 0x1510, /* interrupt mask clear auto */
	Rintrrate     = 0x00c4, /* interrupt throttling rate */

	Rintrecauseclr = 0x1580, /* extended interrupt cause clear */
	Rintrecause    = 0x1520, /* extended interrupt cause */
	Rintremask     = 0x1524, /* extended interrupt mask */
	Rintremaskclr  = 0x1528, /* extended interrupt mask clear */
	Rintremaskauto = 0x152c, /* extended interrupt mask clear auto */
	Rintrerate     = 0x1680, /* extended interrupt throttling rate */

	Rgpie = 0x1514, /* general purpose interrupt enable */

	Rivar0    = 0x1700, /* interrupt vector allocation 0 */
	Rivar1    = 0x1704, /* interrupt vector allocation 1 */
	Rivarmisc = 0x1740, /* interrupt vector allocation misc */

	/* semaphore */
	Rsem     = 0x5b50, /* semaphore */
	Rsemsync = 0x5b5c, /* semaphore for sync */

	/* statistics */
	Rstatrx          = 0x40d0, /* rx frames */
	Rstatrx0040      = 0x405c, /* rx frames < 0x0040 */
	Rstatrx0080      = 0x4060, /* rx frames < 0x0080 */
	Rstatrx0100      = 0x4064, /* rx frames < 0x0100 */
	Rstatrx0200      = 0x4068, /* rx frames < 0x0200 */
	Rstatrx0400      = 0x406c, /* rx frames < 0x0400 */
	Rstatrxffff      = 0x4070, /* rx frames < 0xffff */
	Rstatrxok        = 0x4074, /* rx ok */
	Rstatrxokbcast   = 0x4078, /* rx ok broadcast */
	Rstatrxokmcast   = 0x407c, /* rx ok multicast */
	Rstatrxbytes     = 0x40c0, /* rx bytes */
	Rstatrxbytesgood = 0x4088, /* rx bytes good */
	Rstattx          = 0x40d4, /* tx frames */
	Rstattx0040      = 0x40d8, /* tx frames < 0x0040 */
	Rstattx0080      = 0x40dc, /* tx frames < 0x0080 */
	Rstattx0100      = 0x40e0, /* tx frames < 0x0100 */
	Rstattx0200      = 0x40e4, /* tx frames < 0x0200 */
	Rstattx0400      = 0x40e8, /* tx frames < 0x0400 */
	Rstattxffff      = 0x40ec, /* tx frames < 0xffff */
	Rstattxok        = 0x4080, /* tx ok */
	Rstattxokbcast   = 0x40f4, /* tx ok broadcast */
	Rstattxokmcast   = 0x40f0, /* tx ok multicast */
	Rstattxbytes     = 0x40c8, /* tx bytes */
	Rstattxbytesgood = 0x4090, /* tx bytes good */

	/* energy efficient ethernet */
	Reee    = 0x0e30, /* eee configuration */
	Reeephy = 0x0e38, /* eee phy configuration */
};

enum {
	/* device control */
	DClinkauto  = 1<<5,  /* link autonegotiate */
	DClink      = 1<<6,  /* link up */
	DCreset     = 1<<29, /* reset */
	DCresetphy  = 1<<31, /* reset phy */

	/* device control (extended) */
	DCEdriver = 1<<28, /* driver active */

	/* mdi control */
	MDIopwrite   = 1<<26, /* mdi write operation */
	MDIopread    = 1<<27, /* mdi read operation */
	MDIstatready = 1<<28, /* mdi ready */
	MDIstaterror = 1<<30, /* mdi error */

	MDImaskdata = 0xffff<<0,  /* mdi data mask */
	MDImaskreg  = 0x001f<<16, /* mdi register mask */
	MDImaskphy  = 0x001f<<21, /* mdi phy mask */
	MDIshiftreg = 16,         /* mdi register shift */
	MDIshiftphy = 21,         /* mdi phy shift */

	/* mdi power management */
	PPMdisable2500d3 = 1<<11, /* disable 2500BASE-T in D3 */
	PPMdisable2500   = 1<<12, /* disable 2500BASE-T */
	PPMdisable1000d3 = 1<<3,  /* disable 1000BASE-T in D3 */
	PPMdisable1000   = 1<<6,  /* disable 1000BASE-T */
	PPMdisable100d3  = 1<<9,  /* disable 100BASE-T in D3 */
	PPMdisable       = 1<<5,  /* disable */
	PPMreset         = 1<<8,  /* reset complete */
};

enum {
	/* receive control */
	RXCenable    = 1<<1, /* rx enable */
	RXCsbp       = 1<<2, /* rx store bad packets */
	RXCpromisc   = 1<<3, /* rx promiscuous */
	RXCpromiscm  = 1<<4, /* rx promiscuous multicast */
	RXClong      = 1<<5, /* rx long packets */
	RXCmultimask = 3<<12, /* rx multicast filter mask */
	RXCmulti     = 0<<12, /* rx multicast filter */
	RXCbroad     = 1<<14, /* rx broadcast */
	RXCvfe       = 1<<18, /* rx vlan filter enable */
	RXCsize      = 0<<16, /* rx size 2048 */
	RXCsizeex    = 1<<25, /* rx size extension */
	RXCcrc       = 1<<26, /* rx strip crc */

	/* receive ring split replication */
	RXSdesc     = 0x1<<25, /* rx descriptor type */
	RXSdescmask = 0x7<<25, /* rx descriptor type mask */
	RXSsize     = 0x2,     /* rx packet buffer len */
	RXSsizemask = 0x7f,    /* rx packet buffer len mask */

	/* receive ring control */
	RXRthresh  = 0xffffff, /* rx ring threshold mask */
	RXRthreshp = 0<<0,     /* rx ring threshold p? */
	RXRthreshh = 0<<8,     /* rx ring threshold h? */
	RXRthreshw = 0<<16,    /* rx ring threshold w? */
	RXRenable  = 1<<25,    /* rx ring enable */
	RXRflush   = 1<<26,    /* rx ring flush */

	/* receive checksum control */
	RXKip  = 1<<8,  /* rx checksum ip offload */
	RXKtcp = 1<<9,  /* rx checksum tcp / udp offload */
	RXKcrc = 1<<11, /* rx checksum crc offload */
};

enum {
	/* transmit control */
	TXCenable  = 1<<1,      /* tx enable */
	TXCpsp     = 1<<3,      /* tx pad short packets */
	TXCcthresh = 0xff<<4,   /* tx collision threshold */
	TXCcdist   = 0xf<<12,   /* tx collision distance */
	TXCcrt     = 1<<28,     /* tx collision late re-transmit */

	/* transmit ring control */
	TXRthresh  = 0xffffff, /* tx ring threshold mask */
	TXRthreshp = 31<<0,    /* tx ring threshold p? */
	TXRthreshh = 1<<8,     /* tx ring threshold h? */
	TXRthreshw = 1<<16,    /* tx ring threshold w? */
	TXRcount   = 1<<22,    /* tx ring count descriptors */
	TXRgran    = 1<<24,    /* tx ring granularity */
	TXRenable  = 1<<25,    /* tx ring enable */
};

enum {
	/* eeprom control */
	EEauto = 1<<9, /* eeprom auto read complete */
};

enum {
	/* interrupts */
	Ilink   = 1<<2,  /* interrupt link */

	/* interrupts (extended) */
	Irx0 = 1<<0, /* rx queue 0 */
	Irx1 = 1<<1, /* rx queue 1 */
	Irx2 = 1<<2, /* rx queue 2 */
	Irx3 = 1<<3, /* rx queue 3 */
	Itx0 = 1<<4, /* tx queue 0 */
	Itx1 = 1<<5, /* tx queue 1 */
	Itx2 = 1<<6, /* tx queue 2 */
	Itx3 = 1<<7, /* tx queue 3 */

	/* interrupt vector allocation */
	IVvalid     = 1<<7, /* valid */
	IVshift     = 8,    /* shift to tx */  
	IVshiftnext = 16,   /* shift to next pair */
};

enum {
	/* semaphore */
	SMbit   = 1<<0, /* bit for software */
	SMbitfw = 1<<1, /* bit for firmware */

	/* semaphore sync */
	SMSeeprom = 1<<0, /* exclusive access to eeprom */
	SMSphy0   = 1<<1, /* exclusive access to phy 0 */
	SMSphy1   = 1<<2, /* exclusive access to phy 1 */
};

enum {
	/* eee config */
	EEElpitx = 1<<16, /* eee lpi on tx */
	EEElpirx = 1<<17, /* eee lpi on rx */
	EEElpifc = 1<<18, /* eee lpi on flow control */

	/* eee phy config */
	EEEPenable100M = 1<<2, /* eee enable 100M */
	EEEPenable1G   = 1<<3, /* eee enable 1G */
	EEEPenable2G   = 1<<4, /* eee enable 2.5G */
};

typedef struct TX TX;
typedef struct TXq TXq;

struct TX {
	/* transmit descriptor */
	u64int addr;
	u32int ctl;
	u32int status;
};

struct TXq {
	/* transmit queue */
	Rendez;

	u32int len;
	u32int ptr;
	u32int ptrtail;
	u32int ptrtailn;

	uchar *desc;

	Block **descb;
};

enum {
	/* transmit descriptor control */
	TDtypectx  = 2<<20, /* type = context */
	TDtypedat  = 3<<20, /* type = data */
	TDpkt      = 1<<24, /* packet done */
	TDpktcrc   = 1<<25, /* packet done, insert crc */
	TDstat     = 1<<27, /* report status */
	TDlen      = 0xffff, /* length mask */
	TDlenshift = 0,      /* length shift */

	/* transmit descriptor status */
	TDSlen = 0xffffc000, /* length mask */
	TDSlenshift = 14,    /* length shift */
};

typedef struct RX RX;
typedef struct RXd RXd;
typedef struct RXq RXq;

struct RX {
	/* receive descriptor */
	u64int addr;
	u64int addrhdr;
};

struct RXd {
	/* receive descriptor done */
	u32int rssq;
	u32int rss;
	u32int status;
	u16int length;
};

struct RXq {
	/* receive queue */
	Rendez;

	Bpool pool;

	u32int len;
	u32int ptr;
	u32int ptrtail;
	u32int ptrtailn;

	uchar *desc;

	Block **descb;
};

enum {
	/* receive descriptor done status */
	RDdone    = 1<<0, /* descriptor done */
	RDdonepkt = 1<<1, /* descriptor done, end of packet */
	RDcrcudp  = 1<<4, /* udp checksum calculated */
	RDcrctcp  = 1<<5, /* tcp checksum calculated */
	RDcrcip   = 1<<6, /* ip checksum calculated */
	RDerr     = 1<<31, /* error */
};

typedef struct Ctlrstat Ctlrstat;
typedef struct Ctlr Ctlr;

static struct Ctlrstat {
	u32int statreg;
	u64int stat;
	char  *name;
};

struct Ctlr {
	Lock;
	QLock;
	Rendez;

	Ctlr     *link;
	Ctlrstat *stat;
	u32int    stats;

	Ether  *edev;
	Pcidev *pdev;
	u32int  pdevfunc;
	u64int  port;
	u32int *portmmio;

	Mii *mii;

	RXq rx;
	TXq tx;
};

static Ctlr *i225ctlr;

#define csr32r(c, r)    (*((c)->portmmio+((r)/4)))
#define csr32w(c, r, v)	(*((c)->portmmio+((r)/4)) = (v))
#define csr32f(c)       USED(csr32r((c), Rdevstatus), coherence())

static void
i225semunlock(Ctlr *c)
{
	u32int r;

	/* release semaphore */
	r = csr32r(c, Rsem);
	r &= ~(SMbit | SMbitfw);

	csr32w(c, Rsem, r);
}

static int
i225semlock(Ctlr *c)
{
	u32int r;
	u32int rt;

	/* acquire semaphore bit */
	rt = 16;
	while (rt) {
		r = csr32r(c, Rsem);
		if (!(r & SMbit))
			break;

		microdelay(50);
		rt--;
	}

	if (rt == 0)
		return -1;

	/* acquire semaphore firmware bit */
	rt = 16;
	while (rt) {
		r = csr32r(c, Rsem);
		if (r & SMbitfw)
			break;

		csr32w(c, Rsem, csr32r(c, Rsem) | SMbitfw);
		microdelay(50);
		rt--;
	}

	if(rt == 0) {
		i225semunlock(c);
		return -1;
	}

	return 0;
}

static int
i225syncunlock(Ctlr *c, u16int m)
{
	if(i225semlock(c) < 0)
		return -1;

	/* release sync */
	csr32w(c, Rsemsync, csr32r(c, Rsemsync) & ~m);
	i225semunlock(c);
	return 0;
}

static int
i225synclock(Ctlr *c, u16int m)
{
	u32int r;
	u32int rt;
	u32int mw = m << 16 | m;

	/* aquire sync */
	rt = 200;
	while (rt) {
		if(i225semlock(c) < 0)
			return -1;

		r = csr32r(c, Rsemsync);
		if (!(r & mw))
			break;

		i225semunlock(c);
		microdelay(5);
		rt--;
	}

	if (rt == 0)
		return -1;

	csr32w(c, Rsemsync, csr32r(c, Rsemsync) | m);
	i225semunlock(c);
	return 0;
}

static int
i225miir(Mii *mii, int pa, int ra)
{
	Ctlr *c;
	u32int r;
	u32int rt;

	/* acquire control of the phy */
	c = mii->ctlr;
	if (i225synclock(c, c->pdevfunc ? SMSphy1 : SMSphy0) < 0)
		return -1;

	/* configure the mdi operation */
	rt = 2000;
	r = (ra << MDIshiftreg) & MDImaskreg;
	r |= (pa << MDIshiftphy) & MDImaskphy;
	r |= MDIopread;

	csr32w(c, Rmdi, r);
	while (rt) {
		/* wait for completion or failure */
		r = csr32r(c, Rmdi);
		if (r & (MDIstaterror | MDIstatready))
			break;

		microdelay(50);
		rt--;
	}

	/* release control of the phy */
	i225syncunlock(c, c->pdevfunc ? SMSphy1 : SMSphy0);
	if (r & MDIstaterror || rt == 0) {
		print("#l%d: mii read: error pa=%04ux ra=%04ux\n", c->edev->ctlrno, pa, ra);
		return -1;
	}

	return r & MDImaskdata;
}

static int
i225miiw(Mii *mii, int pa, int ra, int w)
{
	Ctlr *c;
	u32int r;
	u32int rt;

	/* acquire control of the phy */
	c = mii->ctlr;
	if (i225synclock(c, c->pdevfunc ? SMSphy1 : SMSphy0) < 0)
		return -1;

	/* configure the mdi operation */
	rt = 2000;
	r = (ra << MDIshiftreg) & MDImaskreg;
	r |= (pa << MDIshiftphy) & MDImaskphy;
	r |= w & MDImaskdata;
	r |= MDIopwrite;

	csr32w(c, Rmdi, r);
	while (rt) {
		/* wait for completion or failure */
		r = csr32r(c, Rmdi);
		if (r & (MDIstaterror | MDIstatready))
			break;

		microdelay(50);
		rt--;
	}

	/* release control of the phy */
	i225syncunlock(c, c->pdevfunc ? SMSphy1 : SMSphy0);
	if (r & MDIstaterror || rt == 0) {
		print("#l%d: mii write: error pa=%04ux ra=%04ux\n", c->edev->ctlrno, pa, ra);
		return -1;
	}

	return 0;
}

static void
i225mii(Ctlr *c)
{
	u32int r;
	u32int rt;
	MiiPhy *phy;

	/* acquire control of the phy */
	if (i225synclock(c, c->pdevfunc ? SMSphy1 : SMSphy0) < 0)
		error("phy sync timeout");

	r = csr32r(c, Rmdipwr);
	rt = 10000;
	USED(r);

	/* perform a hardware reset of the phy from the mac */
	csr32w(c, Rdevctrl, csr32r(c, Rdevctrl) | DCresetphy); csr32f(c); microdelay(100);
	csr32w(c, Rdevctrl, csr32r(c, Rdevctrl) & ~DCresetphy); csr32f(c); microdelay(150);
	while (rt) {
		/* wait for it to come back on */
		r = csr32r(c, Rmdipwr);
		if (r & PPMreset)
			break;

		microdelay(1);
		rt--;
	}

	/* release control of the phy */
	i225syncunlock(c, c->pdevfunc ? SMSphy1 : SMSphy0);

	c->mii = malloc(sizeof(*c->mii));
	if (c->mii == nil)
		error(Enomem);

	c->mii->ctlr = c;
	c->mii->mir = i225miir;
	c->mii->miw = i225miiw;
	c->mii->name = c->edev->name;
	if (mii(c->mii, ~0) == 0 || (phy = c->mii->curphy) == nil) {
		free(c->mii); c->mii = nil;
		error("phy");
	}
	addmiibus(c->mii);

	/* configure for auto-negotiated link */
	csr32w(c, Rdevctrl, csr32r(c, Rdevctrl) | DClink | DClinkauto);
	miimiw(phy, Bmcr, miimir(phy, Bmcr) & ~BmcrPd); microdelay(300);
	miiane(phy, ~0, ~0, ~0);
	miianec45(phy, ~0);
}

static void
i225intr(Ureg *, void *a)
{
	Ether *e;
	Ctlr *c;
	u32int i, ie;
	u32int im, ime;

	e = a;
	c = e->ctlr;

	/*
	 * push the current interrupt masks.
	 * read the cause register, clearing it, then wake
	 * the correct process.
	 */
	ilock(c);
	im = csr32r(c, Rintrmask); csr32w(c, Rintrmaskclr, ~0);
	ime = csr32r(c, Rintremask); csr32w(c, Rintremaskclr, ~0); csr32f(c);
	i = csr32r(c, Rintrcauseclr);
	ie = csr32r(c, Rintrecauseclr); csr32f(c);

	if (i & Ilink) {
		wakeup(c);
		goto out;
	}

	if (ie & Irx0) {
		wakeup(&c->rx);
		goto out;
	}

	if (ie & Itx0) {
		wakeup(&c->tx);
		goto out;
	}

out:
	/* pop the previous interrupt mask */
	csr32w(c, Rintrmask, im);
	csr32w(c, Rintremask, ime);
	iunlock(c);
}

static void
i225txattach(Ctlr *c)
{
	uvlong pa;

	/* disable transmit and the transmit ring during configuration */
	csr32w(c, Rtxctrl, csr32r(c, Rtxctrl) & ~TXCenable);
	csr32w(c, Rtxrctrl, 0);

	c->tx.len = 1024;
	c->tx.ptr = 0;
	c->tx.ptrtail = 0;
	c->tx.ptrtailn = 1;

	/* allocate transmit block and transmit descriptor arrays */
	c->tx.desc = mallocalign(c->tx.len * 16, 128, 0, 0);
	c->tx.descb = malloc(c->tx.len * sizeof(Block*));
	if (c->tx.desc == nil || c->tx.descb == nil)
		error(Enomem);

	csr32w(c, Rtxrptr, c->tx.ptr);
	csr32w(c, Rtxrptrtail, c->tx.ptrtail);
	csr32w(c, Rtxrlen, c->tx.len * 16);

	pa = PCIWADDR(c->tx.desc);
	csr32w(c, Rtxraddr, pa);
	csr32w(c, Rtxraddrhi, pa >> 32);

	/* enable transmit and the transmit ring */
	csr32w(c, Rtxrctrl, TXRthreshp | TXRthreshh | TXRthreshw | TXRcount | TXRgran | TXRenable);
	csr32w(c, Rtxctrl, csr32r(c, Rtxctrl) & ~TXCcthresh);
	csr32w(c, Rtxctrl, csr32r(c, Rtxctrl) | TXCenable | TXCpsp | TXCcdist | TXCcrt);
}

static void
i225tx(Ctlr *c, u32int ptr)
{
	Block *b;

	/*
	 * read tx done descriptors and clean blocks
	 * until the software head pointer catches up to the hardware
	 */
	while (c->tx.ptr != ptr) {
		b = c->tx.descb[c->tx.ptr];
		if (b != nil) {
			c->tx.descb[c->tx.ptr] = nil;
			freeb(b);
		}

		c->tx.ptr++;
		if (c->tx.ptr >= c->tx.len)
			c->tx.ptr = 0;
	}
}

static uchar *
i225txfilldesc(uchar *b, TX *d)
{
	*b++ = d->addr >> 0; *b++ = d->addr >> 8;
	*b++ = d->addr >> 16; *b++ = d->addr >> 24;
	*b++ = d->addr >> 32; *b++ = d->addr >> 40;
	*b++ = d->addr >> 48; *b++ = d->addr >> 56;

	*b++ = d->ctl >> 0; *b++ = d->ctl >> 8;
	*b++ = d->ctl >> 16; *b++ = d->ctl >> 24;

	*b++ = d->status >> 0; *b++ = d->status >> 8;
	*b++ = d->status >> 16; *b++ = d->status >> 24;

	return b;
}

static void
i225txfill(Ctlr *c)
{
	Block *b;
	TX d;

	/*
	 * write transmit descriptors
	 * until the tail pointer catches up to the head pointer,
	 * or there are no blocks left to send
	 */
	while (c->tx.ptrtailn != c->tx.ptr) {
		b = qget(c->edev->oq);
		if (b == nil)
			break;

		c->tx.descb[c->tx.ptrtail] = b;

		d.addr = PCIWADDR(b->rp);
		d.ctl = TDtypedat|TDpkt|TDpktcrc|TDstat|BLEN(b) << TDlenshift;
		d.status = BLEN(b) << TDSlenshift;
		i225txfilldesc(&c->tx.desc[c->tx.ptrtail * 16], &d);
		dmaflush(1, &c->tx.desc[c->tx.ptrtail * 16], 16);
		coherence();

		c->tx.ptrtail++;
		c->tx.ptrtailn++;
		if (c->tx.ptrtail >= c->tx.len)
			c->tx.ptrtail = 0;
		if (c->tx.ptrtailn >= c->tx.len)
			c->tx.ptrtailn = 0;
	}

	csr32w(c, Rtxrptrtail, c->tx.ptrtail);
}

static void
i225transmit(Ether *e)
{
	Ctlr *c;

	c = e->ctlr;

	wakeup(&c->tx);
}

static uchar *
i225rxfilldesc(uchar *b, RX *d)
{
	*b++ = d->addr >> 0; *b++ = d->addr >> 8;
	*b++ = d->addr >> 16; *b++ = d->addr >> 24;
	*b++ = d->addr >> 32; *b++ = d->addr >> 40;
	*b++ = d->addr >> 48; *b++ = d->addr >> 56;

	*b++ = d->addrhdr >> 0; *b++ = d->addrhdr >> 8;
	*b++ = d->addrhdr >> 16; *b++ = d->addrhdr >> 24;
	*b++ = d->addrhdr >> 32; *b++ = d->addrhdr >> 40;
	*b++ = d->addrhdr >> 48; *b++ = d->addrhdr >> 56;

	return b;
}

static void
i225rxfill(Ctlr *c)
{
	Block *b;
	RX d;

	/*
	 * write receive descriptors and allocate blocks
	 * until the tail pointer catches up to the head pointer
	 */
	while (c->rx.ptrtailn != c->rx.ptr) {
		while ((b = iallocbp(&c->rx.pool)) == nil)
			resrcwait("out of i225 rx buffers");
		c->rx.descb[c->rx.ptrtail] = b;

		d.addr = PCIWADDR(b->rp);
		d.addrhdr = PCIWADDR(b->rp);
		i225rxfilldesc(&c->rx.desc[c->rx.ptrtail * 16], &d);
		dmaflush(1, &c->rx.desc[c->rx.ptrtail * 16], 16);
		coherence();

		c->rx.ptrtail++;
		c->rx.ptrtailn++;
		if (c->rx.ptrtail >= c->rx.len)
			c->rx.ptrtail = 0;
		if (c->rx.ptrtailn >= c->rx.len)
			c->rx.ptrtailn = 0;
	}

	csr32w(c, Rrxrptrtail, c->rx.ptrtail);
}

static uchar *
i225rxdesc(uchar *b, RXd *d)
{
	d->rssq = *b++;
	d->rssq |= *b++ << 8;
	d->rssq |= *b++ << 16;
	d->rssq |= *b++ << 24;

	d->rss = *b++;
	d->rss |= *b++ << 8;
	d->rss |= *b++ << 16;
	d->rss |= *b++ << 24;

	d->status = *b++;
	d->status |= *b++ << 8;
	d->status |= *b++ << 16;
	d->status |= *b++ << 24;

	d->length = *b++;
	d->length |= *b++ << 8;

	return b;
}

static void
i225rx(Ctlr *c, u32int ptr)
{
	Block *b, *bl, *blt;
	RXd d;

	/*
	 * read rx done descriptors
	 * until the software head pointer catches up to the hardware
	 */
	bl = nil;
	blt = nil;
	while (c->rx.ptr != ptr) {
		i225rxdesc(&c->rx.desc[c->rx.ptr * 16], &d);
		b = c->rx.descb[c->rx.ptr];
		b->wp = b->rp + d.length;

		/* if there is an ongoing list, append */
		if (blt != nil) {
			blt->next = b;
			blt = b;
		}

		/* if not, create */
		if (bl == nil) {
			bl = b;
			blt = b;
		}

		/* if this is an error, concatenate and free */
		if (d.status & RDerr) {
			freeblist(bl);
			bl = nil;
			blt = nil;
			goto next;
		}

		/* if this is the end of a packet, concatenate and pass up the queue */
		if (d.status & RDdonepkt) {
			b = concatblock(bl);
			bl = nil;
			blt = nil;

			if (d.status & RDcrcudp)
				b->flag |= Budpck;
			if (d.status & RDcrctcp)
				b->flag |= Btcpck;
			if (d.status & RDcrcip)
				b->flag |= Bipck;

			etheriq(c->edev, b);
		}

next:
		c->rx.ptr++;
		if (c->rx.ptr >= c->rx.len)
			c->rx.ptr = 0;
	}
}

static void
i225rxattach(Ctlr *c)
{
	uvlong pa;

	/* disable receive and the receive ring during configuration */
	csr32w(c, Rrxctrl, csr32r(c, Rrxctrl) & ~(RXCmultimask | RXCsizeex | RXCenable));
	csr32w(c, Rrxrctrl, 0);

	c->rx.len = 1024;
	c->rx.ptr = 0;
	c->rx.ptrtail = 0;
	c->rx.ptrtailn = 1;
	if (c->rx.pool.size == 0) {
		c->rx.pool.size = 4096;
		c->rx.pool.align = 4096;
		growbp(&c->rx.pool, c->rx.len * 2);
	}

	/* allocate receive block and receive descriptor arrays */
	c->rx.desc = mallocalign(c->rx.len * 16, 128, 0, 0);
	c->rx.descb = malloc(c->rx.len * sizeof(Block*));
	if (c->rx.desc == nil || c->rx.descb == nil)
		error(Enomem);

	/* configure the receive ring */
	csr32w(c, Rrxrptr, c->rx.ptr);
	csr32w(c, Rrxrptrtail, c->rx.ptrtail);
	csr32w(c, Rrxrlen, c->rx.len * 16);

	pa = PCIWADDR(c->rx.desc);
	csr32w(c, Rrxraddr, pa);
	csr32w(c, Rrxraddrhi, pa >> 32);

	/* configure the receive descriptor format */
	csr32w(c, Rrxrsr, (csr32r(c, Rrxrsr) & ~RXSdescmask) | RXSdesc);
	csr32w(c, Rrxrsr, (csr32r(c, Rrxrsr) & ~RXSsizemask) | RXSsize);
	csr32w(c, Rrxcsum, ETHERHDRSIZE | RXKip | RXKtcp | RXKcrc);

	/* enable receive and the receive ring */
	csr32w(c, Rrxrctrl, csr32r(c, Rrxrctrl) | RXRthreshp | RXRthreshh | RXRthreshp | RXRenable);
	csr32w(c, Rrxctrl, csr32r(c, Rrxctrl) | RXCmulti | RXCbroad | RXCsize | RXCcrc | RXClong | RXCenable);
}

static void
i225statattach(Ctlr *c)
{
	static Ctlrstat stat[] = {
		Rstatrx,          0, "rx",
		Rstatrx0040,      0, "rx 0040",
		Rstatrx0080,      0, "rx 0080",
		Rstatrx0100,      0, "rx 0100",
		Rstatrx0200,      0, "rx 0200",
		Rstatrx0400,      0, "rx 0400",
		Rstatrxffff,      0, "rx ffff",
		Rstatrxok,        0, "rx ok",
		Rstatrxokbcast,   0, "rx ok bcast",
		Rstatrxokmcast,   0, "rx ok mcast",
		Rstatrxbytes,     0, "rx bytes",
		Rstatrxbytesgood, 0, "rx bytes good",
		Rstattx,          0, "tx",
		Rstattx0040,      0, "tx 0040",
		Rstattx0080,      0, "tx 0080",
		Rstattx0100,      0, "tx 0100",
		Rstattx0200,      0, "tx 0200",
		Rstattx0400,      0, "tx 0400",
		Rstattxffff,      0, "tx ffff",
		Rstattxok,        0, "tx ok",
		Rstattxokbcast,   0, "tx ok bcast",
		Rstattxokmcast,   0, "tx ok mcast",
		Rstattxbytes,     0, "tx bytes",
		Rstattxbytesgood, 0, "tx bytes good",
	};

	c->stats = nelem(stat);
	c->stat = malloc(sizeof(stat));
	if (c->stat == nil)
		error(Enomem);

	memmove(c->stat, stat, sizeof(stat));
}

static void
i225proctx(void *a)
{
	Ctlr *c;

	c = a;
	while (waserror())
		;

	/* process the transmit queue */
	for (;; sleep(&c->tx, return0, nil)) {
		i225tx(c, csr32r(c, Rtxrptr));
		i225txfill(c);
	}
}

static void
i225procrx(void *a)
{
	Ctlr *c;

	c = a;
	while (waserror())
		;

	/* process the receive queue */
	for(;; sleep(&c->rx, return0, nil)) {
		i225rx(c, csr32r(c, Rrxrptr));
		i225rxfill(c);
	}
}

static void
i225proclink(void *a)
{
	Ctlr *c;
	MiiPhy *phy;

	c = a;
	while (waserror())
		;
	/* process link status */
	for (;; sleep(c, return0, nil)) {
		phy = c->mii->curphy;
		if (phy == nil) {
			/* phy missing? */
			continue;
		}

		if (miistatus(phy) == 0)
			miistatusc45(phy);
		if (phy->speed == 0) {
			/* phy errata: rinse and repeat, should only happen once */
			miireset(phy);
			continue;
		}

		/* report status */
		ethersetspeed(c->edev, phy->speed);
		ethersetlink(c->edev, phy->link);
	}
}

static void
i225procstat(void *a)
{
	Ctlr *c;
	int i;

	c = a;
	while (waserror())
		;

	/* process statistics */
	for (;; tsleep(&up->sleep, return0, nil, 5000)) {
		qlock(c);
		for (i = 0; i < c->stats; i++) {
			switch(c->stat[i].stat) {
			case Rstatrxbytes:
			case Rstatrxbytesgood:
			case Rstattxbytes:
			case Rstattxbytesgood:
				/* read a 64 bit stat, clears on read of the second register */
				c->stat[i].stat += (u64int) csr32r(c, c->stat[i].statreg);
				c->stat[i].stat += (u64int) csr32r(c, c->stat[i].statreg + 4) << 32;
				break;

			default:
				/* read a 32 bit stat, clears on read */
				c->stat[i].stat += csr32r(c, c->stat[i].statreg);
				break;
			}
		}

		qunlock(c);
	}
}

static void
i225attach(Ether *e)
{
	Ctlr *c;
	char n[KNAMELEN];
	u32int iv0, iv1;

	c = e->ctlr;
	qlock(c);
	if (c->mii != nil) {
		qunlock(c);
		return;
	}

	if (waserror()) {
		/* mask interrupts */
		csr32w(c, Rintrmaskclr, ~0);
		csr32w(c, Rintrmaskauto, 0);
		csr32w(c, Rintrcause, 0);

		/* mask extended interrupts */
		csr32w(c, Rintremaskclr, ~0);
		csr32w(c, Rintremaskauto, 0);
		csr32w(c, Rintrecause, 0);

		qunlock(c);
		nexterror();
	}

	/* configure flow control */
	csr32w(c, Rflowaddr, 0x00c28001);
	csr32w(c, Rflowaddrhi, 0x00000100);
	csr32w(c, Rflowtype, 0x00008808);
	csr32w(c, Rflowtt, 0x00000680);
	csr32w(c, Rflowrt, 0x80007400);
	csr32w(c, Rflowrthi, 0x000073f0);

	/* configure phy, tx, rx */
	i225mii(c);
	i225txattach(c);
	i225rxattach(c);
	i225statattach(c);

	snprint(n, sizeof(n), "#l%d", e->ctlrno); kproc(n, i225proclink, c);
	snprint(n, sizeof(n), "#l%dt", e->ctlrno); kproc(n, i225proctx, c);
	snprint(n, sizeof(n), "#l%dr", e->ctlrno); kproc(n, i225procrx, c);
	snprint(n, sizeof(n), "#l%ds", e->ctlrno); kproc(n, i225procstat, c);

	/* unmask interrupts */
	csr32w(c, Rintrmask, Ilink);
	csr32w(c, Rintremask, Itx0|Irx0);
	csr32f(c);

	/* configure interrupt vector allocation for all four queues */
	iv0 = (IVvalid | 0) | (IVvalid | 4) << IVshift;
	iv0 |= ((IVvalid | 1) | (IVvalid | 5) << IVshift) << IVshiftnext;
	iv1 = (IVvalid | 2) | (IVvalid | 6) << IVshift;
	iv1 |= ((IVvalid | 3) | (IVvalid | 7) << IVshift) << IVshiftnext;

	csr32w(c, Rgpie, 0);
	csr32w(c, Rivar0, iv0);
	csr32w(c, Rivar1, iv1);
	csr32f(c);

	qunlock(c);
	poperror();
}

static char *
i225ifstat(void *a, char *p, char *q)
{
	Ctlr *c;
	int i;

	c = a;
	if (p >= q)
		return p;

	qlock(c);
	for (i = 0; i < c->stats; i++)
		p = seprint(p, q, "%s: %ulld\n", c->stat[i].name, c->stat[i].stat);

	qunlock(c);
	return p;
}

static void
i225promiscuous(void *a, int on)
{
	Ctlr *c;

	c = a;
	if (on)
		csr32w(c, Rrxctrl, csr32r(c, Rrxctrl) | RXCpromisc | RXCpromiscm);
	else
		csr32w(c, Rrxctrl, csr32r(c, Rrxctrl) & ~(RXCpromisc | RXCpromiscm));
}

static void
i225multicast(void *a, uchar *addr, int on)
{
	Ctlr *c;
	u32int hash;
	u32int hashb;
	u32int hashr;

	c = a;
	hash = ((addr[4] >> 4) | (addr[5] << 4)) & 0xfff;
	hashb = hash & 0x1f;
	hashr = (hash >> 5) & 0x7f;
	
	if (on) {
		csr32w(c, Rdevmulti + hashr*4, csr32r(c, Rdevmulti + hashr*4) | 1<<hashb);
		csr32f(c);
	}
}

static void
i225initmac(Ether *e)
{
	Ctlr *c;
	u32int a;
	u32int ah;
	int i;

	/* read the address assigned */
	c = e->ctlr;
	a = e->ea[0] | e->ea[1] << 8 | e->ea[2] << 16 | e->ea[3] << 24;
	ah = e->ea[4] | e->ea[5] << 8;
	if (a == 0 && ah == 0) {
		/* none, read the address out of the card */
		a = csr32r(c, Rdevaddr);
		ah = csr32r(c, Rdevaddrhi) | 1<<31;

		e->ea[0] = a;
		e->ea[1] = a >> 8;
		e->ea[2] = a >> 16;
		e->ea[3] = a >> 24;
		e->ea[4] = ah;
		e->ea[5] = ah >> 8;
	}

	/* clear multicast table */
	for (i = 0; i < 128; i++)
		csr32w(c, Rdevmulti + i*4, 0);

	/* clear address table */
	for (i = 0; i < 16; i++) {
		csr32w(c, Rdevaddr + i*8, 0);
		csr32w(c, Rdevaddrhi + i*8, 0);
	}

	/* program the address */
	csr32w(c, Rdevaddr, a); csr32f(c);
	csr32w(c, Rdevaddrhi, ah); csr32f(c);
}

static int
i225init(Ether *e)
{
	Ctlr *c;
	u32int r;
	u32int rt;

	c = e->ctlr;
	r = csr32r(c, Rdevctrl) | DCreset;
	rt = 1000;

	/* reset the card and wait for it to load from eeprom */
	csr32w(c, Rdevctrl, r);
	csr32f(c);
	while (rt) {
		r = csr32r(c, Reeprom);
		if (r & EEauto)
			break;

		microdelay(1000);
		rt--;
	}

	if (rt == 0) {
		print("#l%d: i225: reset timed out\n", e->ctlrno);
		return -1;
	}

	/* mask interrupts */
	csr32w(c, Rintrmaskclr, ~0);
	csr32w(c, Rintrmaskauto, 0);
	csr32w(c, Rintrcauseclr, ~0);
	csr32w(c, Rintrrate, 488);

	/* mask extended interrupts */
	csr32w(c, Rintremaskclr, ~0);
	csr32w(c, Rintremaskauto, 0);
	csr32w(c, Rintrecauseclr, ~0);
	csr32w(c, Rintrerate, 200);

	/* configure driver active */
	csr32w(c, Rdevctrlext, csr32r(c, Rdevctrlext) | DCEdriver);

	/* configure energy efficient ethernet off */
	csr32w(c, Reee, csr32r(c, Reee) & ~(EEElpitx|EEElpirx|EEElpifc));
	csr32w(c, Reeephy, csr32r(c, Reeephy) & ~(EEEPenable100M|EEEPenable1G|EEEPenable2G));

	i225initmac(e);
	return 0;
}

static void
i225shutdown(Ether *e)
{
	Ctlr *c;
	u32int r;
	u32int rt;

	c = e->ctlr;
	r = csr32r(c, Rdevctrl) | DCreset;
	rt = 1000;

	/* reset the card and wait for it to load from eeprom */
	csr32w(c, Rdevctrl, r);
	csr32f(c);
	while (rt) {
		r = csr32r(c, Reeprom);
		if (r & EEauto)
			break;

		microdelay(1000);
		rt--;
	}

	/* mask interrupts */
	csr32w(c, Rintrmaskclr, ~0);
	csr32w(c, Rintrmaskauto, 0);
	csr32w(c, Rintrcauseclr, ~0);
	csr32w(c, Rintrrate, 488);

	/* mask extended interrupts */
	csr32w(c, Rintremaskclr, ~0);
	csr32w(c, Rintremaskauto, 0);
	csr32w(c, Rintrecauseclr, ~0);
	csr32w(c, Rintrerate, 200);

	/* configure driver inactive */
	csr32w(c, Rdevctrlext, csr32r(c, Rdevctrlext) &~ DCEdriver);
}

static void
i225pci(void)
{
	Ctlr *c, **pc;
	Pcidev *p;

	pc = &i225ctlr;
	for (p = nil; p = pcimatch(p, 0x8086, 0);) {
		if (p->ccrb != 2 || p->ccru != 0)
			continue;
		if (p->mem[0].bar & 1)
			continue;

		switch (p->did) {
		case 0x0d9f: /* i225-it */
		case 0x125b: /* i226-lm */
		case 0x125c: /* i226-v */
		case 0x125d: /* i226-it */
		case 0x125f: /* i225 (blank nvm) */
		case 0x15f2: /* i225-lm */
		case 0x15f3: /* i225-v */
		case 0x15fd: /* i225 (blank nvm) */
		case 0x15f8: /* i225-i */
		case 0x3100: /* i225-k */
		case 0x3101: /* i225-k */
		case 0x3102: /* i226-k */
		case 0x5502: /* i225-lmvp */
		case 0x5503: /* i226-lmvp */
			break;
		default:
			continue;
		}

		c = malloc(sizeof(*c));
		if (c == nil) {
			print("i225: no memory for ctlr");
			continue;
		}

		c->pdev = p;
		c->pdevfunc = BUSFNO(p->tbdf);
		c->port = p->mem[0].bar & ~0xf;
		c->portmmio = vmap(c->port, p->mem[0].size);
		if (c->portmmio == nil) {
			print("i225: no memory for ctlr mmio");
			vunmap(c->portmmio, p->mem[0].size);
			free(c);
			continue;
		}

		*pc = c;
		pc = &c->link;
	}
}

static int
i225pnp(Ether *e)
{
	Ctlr *c;

	if(i225ctlr == nil)
		i225pci();

loop:
	for (c = i225ctlr; c != nil; c = c->link) {
		if (c->edev)
			continue;
		if (e->port == 0 || e->port == c->port) {
			c->edev = e;
			break;
		}
	}

	if (c == nil)
		return -1;

	pcienable(c->pdev);
	pcisetbme(c->pdev);

	e->ctlr = c;
	e->port = c->port;
	e->irq  = c->pdev->intl;
	e->tbdf = c->pdev->tbdf;
	e->mbps = 2500;
	e->maxmtu = 9216;

	e->arg = c;
	e->attach = i225attach;
	e->ifstat = i225ifstat;
	e->transmit = i225transmit;
	e->promiscuous = i225promiscuous;
	e->multicast = i225multicast;
	e->shutdown = i225shutdown;
	if (i225init(e) < 0) {
		e->ctlr = nil;
		goto loop;
	}

	intrenable(e->irq, i225intr, e, e->tbdf, e->name);
	return 0;
}

void
etheri225link(void)
{
	addethercard("i225", i225pnp);
}
