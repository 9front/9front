/*
 * Realtek RTL8110S/8169S Gigabit Ethernet Controllers.
 * Mostly there. There are some magic register values used
 * which are not described in any datasheet or driver but seem
 * to be necessary.
 * No tuning has been done. Only tested on an RTL8110S, there
 * are slight differences between the chips in the series so some
 * tweaks may be needed.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/netif.h"
#include "../port/etherif.h"
#include "../port/ethermii.h"

enum {					/* registers */
	Idr0		= 0x00,		/* MAC address */
	Mar0		= 0x08,		/* Multicast address */
	Dtccr		= 0x10,		/* Dump Tally Counter Command */
	Tnpds		= 0x20,		/* Transmit Normal Priority Descriptors */
	Thpds		= 0x28,		/* Transmit High Priority Descriptors */
	Flash		= 0x30,		/* Flash Memory Read/Write */
	Erbcr		= 0x34,		/* Early Receive Byte Count */
	Ersr		= 0x36,		/* Early Receive Status */
	Cr		= 0x37,		/* Command Register */
	Tppoll		= 0x38,		/* Transmit Priority Polling */
	Imr		= 0x3C,		/* Interrupt Mask */
	Isr		= 0x3E,		/* Interrupt Status */
	Tcr		= 0x40,		/* Transmit Configuration */
	Rcr		= 0x44,		/* Receive Configuration */
	Tctr		= 0x48,		/* Timer Count */
	Mpc		= 0x4C,		/* Missed Packet Counter */
	Cr9346		= 0x50,		/* 9346 Command Register */
	Config0		= 0x51,		/* Configuration Register 0 */
	Config1		= 0x52,		/* Configuration Register 1 */
	Config2		= 0x53,		/* Configuration Register 2 */
	Config3		= 0x54,		/* Configuration Register 3 */
	Config4		= 0x55,		/* Configuration Register 4 */
	Config5		= 0x56,		/* Configuration Register 5 */
	Timerint	= 0x58,		/* Timer Interrupt */
	Mulint		= 0x5C,		/* Multiple Interrupt Select */
	Phyar		= 0x60,		/* PHY Access */
	Tbicsr0		= 0x64,		/* TBI Control and Status */
	Tbianar		= 0x68,		/* TBI Auto-Negotiation Advertisment */
	Tbilpar		= 0x6A,		/* TBI Auto-Negotiation Link Partner */
	Phystatus	= 0x6C,		/* PHY Status */
	Pmch		= 0x6F,		/* power management */
	Ldps		= 0x82,		/* link down power saving */

	Rms		= 0xDA,		/* Receive Packet Maximum Size */
	Cplusc		= 0xE0,		/* C+ Command */
	Coal		= 0xE2,		/* Interrupt Mitigation (Coalesce) */
	Rdsar		= 0xE4,		/* Receive Descriptor Start Address */
	Etx		= 0xEC,		/* Early Transmit Threshold */
};

enum {					/* Dtccr */
	Cmd		= 0x00000008,	/* Command */
};

enum {					/* Cr */
	Te		= 0x04,		/* Transmitter Enable */
	Re		= 0x08,		/* Receiver Enable */
	Rst		= 0x10,		/* Software Reset */
};

enum {					/* Tppoll */
	Fswint		= 0x01,		/* Forced Software Interrupt */
	Npq		= 0x40,		/* Normal Priority Queue polling */
	Hpq		= 0x80,		/* High Priority Queue polling */
};

enum {					/* Imr/Isr */
	Rok		= 0x0001,	/* Receive OK */
	Rer		= 0x0002,	/* Receive Error */
	Tok		= 0x0004,	/* Transmit OK */
	Ter		= 0x0008,	/* Transmit Error */
	Rdu		= 0x0010,	/* Receive Descriptor Unavailable */
	Punlc		= 0x0020,	/* Packet Underrun or Link Change */
	Fovw		= 0x0040,	/* Receive FIFO Overflow */
	Tdu		= 0x0080,	/* Transmit Descriptor Unavailable */
	Swint		= 0x0100,	/* Software Interrupt */
	Timeout		= 0x4000,	/* Timer */
	Serr		= 0x8000,	/* System Error */
};

enum {					/* Tcr */
	MtxdmaSHIFT	= 8,		/* Max. DMA Burst Size */
	MtxdmaMASK	= 0x00000700,
	Mtxdmaunlimited	= 0x00000700,
	Acrc		= 0x00010000,	/* Append CRC (not) */
	Lbk0		= 0x00020000,	/* Loopback Test 0 */
	Lbk1		= 0x00040000,	/* Loopback Test 1 */
	Ifg2		= 0x00080000,	/* Interframe Gap 2 */
	HwveridSHIFT	= 23,		/* Hardware Version ID */
	HwveridMASK	= 0x7C800000,
	Macv01		= 0x00000000,	/* RTL8169 */
	Macv02		= 0x00800000,	/* RTL8169S/8110S */
	Macv03		= 0x04000000,	/* RTL8169S/8110S */
	Macv04		= 0x10000000,	/* RTL8169SB/8110SB */
	Macv05		= 0x18000000,	/* RTL8169SC/8110SC */
	Macv07		= 0x24800000,	/* RTL8102e */
	Macv07a		= 0x34800000,	/* RTL8102e */
	Macv11		= 0x30000000,	/* RTL8168B/8111B */
	Macv12		= 0x38000000,	/* RTL8169B/8111B */
	Macv12a		= 0x3c000000,	/* RTL8169C/8111C */
	Macv13		= 0x34000000,	/* RTL8101E */
	Macv14		= 0x30800000,	/* RTL8100E */
	Macv15		= 0x38800000,	/* RTL8100E */
//	Macv19		= 0x3c000000,	/* dup Macv12a: RTL8111c-gr */
	Macv25		= 0x28000000,	/* RTL8168D */
	Macv26		= 0x48000000,	/* RTL8111/8168B */
	Macv27		= 0x2c800000,	/* RTL8111e */
	Macv28		= 0x2c000000,	/* RTL8111/8168B */
	Macv29		= 0x40800000,	/* RTL8101/8102E */
	Macv30		= 0x24000000,	/* RTL8101E? (untested) */
	Macv39		= 0x44800000,	/* RTL8106E */
	Macv40		= 0x4c000000,	/* RTL8168G */
	Macv42		= 0x50800000,	/* RTL8168GU */
	Macv44		= 0x5c800000,	/* RTL8411B */
	Macv45		= 0x54000000,	/* RTL8111HN */

	Ifg0		= 0x01000000,	/* Interframe Gap 0 */
	Ifg1		= 0x02000000,	/* Interframe Gap 1 */
};

enum {					/* Rcr */
	Aap		= 0x00000001,	/* Accept All Packets */
	Apm		= 0x00000002,	/* Accept Physical Match */
	Am		= 0x00000004,	/* Accept Multicast */
	Ab		= 0x00000008,	/* Accept Broadcast */
	Ar		= 0x00000010,	/* Accept Runt */
	Aer		= 0x00000020,	/* Accept Error */
	Sel9356		= 0x00000040,	/* 9356 EEPROM used */
	MrxdmaSHIFT	= 8,		/* Max. DMA Burst Size */
	MrxdmaMASK	= 0x00000700,
	Mrxdmaunlimited	= 0x00000700,
	RxfthSHIFT	= 13,		/* Receive Buffer Length */
	RxfthMASK	= 0x0000E000,
	Rxfth256	= 0x00008000,
	Rxfthnone	= 0x0000E000,
	Rer8		= 0x00010000,	/* Accept Error Packets > 8 bytes */
	MulERINT	= 0x01000000,	/* Multiple Early Interrupt Select */
};

enum {					/* Cr9346 */
	Eedo		= 0x01,		/* */
	Eedi		= 0x02,		/* */
	Eesk		= 0x04,		/* */
	Eecs		= 0x08,		/* */
	Eem0		= 0x40,		/* Operating Mode */
	Eem1		= 0x80,
};

enum {					/* Phyar */
	DataMASK	= 0x0000FFFF,	/* 16-bit GMII/MII Register Data */
	DataSHIFT	= 0,
	RegaddrMASK	= 0x001F0000,	/* 5-bit GMII/MII Register Address */
	RegaddrSHIFT	= 16,
	Flag		= 0x80000000,	/* */
};

enum {					/* Phystatus */
	Fd		= 0x01,		/* Full Duplex */
	Linksts		= 0x02,		/* Link Status */
	Speed10		= 0x04,		/* */
	Speed100	= 0x08,		/* */
	Speed1000	= 0x10,		/* */
	Rxflow		= 0x20,		/* */
	Txflow		= 0x40,		/* */
	Entbi		= 0x80,		/* */
};

enum {					/* Cplusc */
	Txenb		= 0x0001,	/* enable C+ transmit mode */
	Rxenb		= 0x0002,	/* enable C+ receive mode */
	Mulrw		= 0x0008,	/* PCI Multiple R/W Enable */
	Dac		= 0x0010,	/* PCI Dual Address Cycle Enable */
	Rxchksum	= 0x0020,	/* Receive Checksum Offload Enable */
	Rxvlan		= 0x0040,	/* Receive VLAN De-tagging Enable */
	Macstatdis	= 0x0080,	/* Disable Mac Statistics */
	Endian		= 0x0200,	/* Endian Mode */
};

typedef struct D D;			/* Transmit/Receive Descriptor */
struct D {
	u32int	control;
	u32int	vlan;
	u32int	addrlo;
	u32int	addrhi;
};

enum {					/* Transmit Descriptor control */
	TxflMASK	= 0x0000FFFF,	/* Transmit Frame Length */
	TxflSHIFT	= 0,
	Tcps		= 0x00010000,	/* TCP Checksum Offload */
	Udpcs		= 0x00020000,	/* UDP Checksum Offload */
	Ipcs		= 0x00040000,	/* IP Checksum Offload */
	Lgsen		= 0x08000000,	/* TSO; WARNING: contains lark's vomit */
};

enum {					/* Receive Descriptor control */
	RxflMASK	= 0x00001FFF,	/* Receive Frame Length */
	Tcpf		= 0x00004000,	/* TCP Checksum Failure */
	Udpf		= 0x00008000,	/* UDP Checksum Failure */
	Ipf		= 0x00010000,	/* IP Checksum Failure */
	Pid0		= 0x00020000,	/* Protocol ID0 */
	Pid1		= 0x00040000,	/* Protocol ID1 */
	Crce		= 0x00080000,	/* CRC Error */
	Runt		= 0x00100000,	/* Runt Packet */
	Res		= 0x00200000,	/* Receive Error Summary */
	Rwt		= 0x00400000,	/* Receive Watchdog Timer Expired */
	Fovf		= 0x00800000,	/* FIFO Overflow */
	Bovf		= 0x01000000,	/* Buffer Overflow */
	Bar		= 0x02000000,	/* Broadcast Address Received */
	Pam		= 0x04000000,	/* Physical Address Matched */
	Mar		= 0x08000000,	/* Multicast Address Received */
};

enum {					/* General Descriptor control */
	Ls		= 0x10000000,	/* Last Segment Descriptor */
	Fs		= 0x20000000,	/* First Segment Descriptor */
	Eor		= 0x40000000,	/* End of Descriptor Ring */
	Own		= 0x80000000,	/* Ownership */
};

/*
 */
enum {					/* Ring sizes  (<= 1024) */
	Ntd		= 64,		/* Transmit Ring */
	Nrd		= 256,		/* Receive Ring */

	Mtu		= ETHERMAXTU,
	Mps		= ROUNDUP(ETHERMAXTU+4, 128),
};

typedef struct Dtcc Dtcc;
struct Dtcc {
	u64int	txok;
	u64int	rxok;
	u64int	txer;
	u32int	rxer;
	u16int	misspkt;
	u16int	fae;
	u32int	tx1col;
	u32int	txmcol;
	u64int	rxokph;
	u64int	rxokbrd;
	u32int	rxokmu;
	u16int	txabt;
	u16int	txundrn;
};

enum {						/* Variants */
	Rtl8100e	= (0x8136<<16)|0x10EC,	/* RTL810[01]E: pci -e */
	Rtl8169c	= (0x0116<<16)|0x16EC,	/* RTL8169C+ (USR997902) */
	Rtl8169sc	= (0x8167<<16)|0x10EC,	/* RTL8169SC */
	Rtl8168b	= (0x8168<<16)|0x10EC,	/* RTL8168B: pci-e */
	Rtl8169		= (0x8169<<16)|0x10EC,	/* RTL8169 */
};

typedef struct Ctlr Ctlr;
typedef struct Ctlr {
	Lock;

	int	port;
	Pcidev*	pcidev;
	Ctlr*	next;
	int	active;

	QLock	alock;			/* attach */
	int	init;			/*  */
	Rendez	reset;

	int	pciv;			/*  */
	int	macv;			/* MAC version */
	int	phyv;			/* PHY version */
	int	pcie;			/* flag: pci-express device? */

	uvlong	mchash;			/* multicast hash */

	Mii*	mii;

	D*	td;			/* descriptor ring */
	Block**	tb;			/* transmit buffers */
	int	ntd;

	int	tdh;			/* head - producer index (host) */
	int	tdt;			/* tail - consumer index (NIC) */
	int	ntq;

	D*	rd;			/* descriptor ring */
	Block**	rb;			/* receive buffers */
	int	nrd;

	int	rdh;			/* head - producer index (NIC) */
	int	rdt;			/* tail - consumer index (host) */
	int	nrq;

	int	tcr;			/* transmit configuration register */
	int	rcr;			/* receive configuration register */
	int	imr;

	QLock	slock;			/* statistics */
	Dtcc*	dtcc;
	uint	txdu;
	uint	tcpf;
	uint	udpf;
	uint	ipf;
	uint	fovf;
	uint	rer;
	uint	rdu;
	uint	punlc;
	uint	serr;
	uint	fovw;
	uint	mcast;
	uint	frag;			/* partial packets; rb was too small */
} Ctlr;

static Ctlr* rtl8169ctlrhead;
static Ctlr* rtl8169ctlrtail;

#define csr8r(c, r)	(inb((c)->port+(r)))
#define csr16r(c, r)	(ins((c)->port+(r)))
#define csr32r(c, r)	(inl((c)->port+(r)))
#define csr8w(c, r, b)	(outb((c)->port+(r), (u8int)(b)))
#define csr16w(c, r, w)	(outs((c)->port+(r), (u16int)(w)))
#define csr32w(c, r, l)	(outl((c)->port+(r), (u32int)(l)))

static int
rtl8169miimir(Mii* mii, int pa, int ra)
{
	uint r;
	int timeo;
	Ctlr *ctlr;

	if(pa != 1)
		return -1;
	ctlr = mii->ctlr;

	r = (ra<<16) & RegaddrMASK;
	csr32w(ctlr, Phyar, r);
	delay(1);
	for(timeo = 0; timeo < 2000; timeo++){
		if((r = csr32r(ctlr, Phyar)) & Flag)
			break;
		microdelay(100);
	}
	if(!(r & Flag))
		return -1;

	return (r & DataMASK)>>DataSHIFT;
}

static int
rtl8169miimiw(Mii* mii, int pa, int ra, int data)
{
	uint r;
	int timeo;
	Ctlr *ctlr;

	if(pa != 1)
		return -1;
	ctlr = mii->ctlr;

	r = Flag|((ra<<16) & RegaddrMASK)|((data<<DataSHIFT) & DataMASK);
	csr32w(ctlr, Phyar, r);
	delay(1);
	for(timeo = 0; timeo < 2000; timeo++){
		if(!((r = csr32r(ctlr, Phyar)) & Flag))
			break;
		microdelay(100);
	}
	if(r & Flag)
		return -1;

	return 0;
}

static int
rtl8169mii(Ctlr* ctlr)
{
	MiiPhy *phy;

	/*
	 * Link management.
	 */
	if((ctlr->mii = malloc(sizeof(Mii))) == nil)
		return -1;
	ctlr->mii->mir = rtl8169miimir;
	ctlr->mii->miw = rtl8169miimiw;
	ctlr->mii->ctlr = ctlr;

	/*
	 * PHY wakeup
	 */
	switch(ctlr->macv){
	case Macv25:
	case Macv28:
	case Macv29:
	case Macv30:
		csr8w(ctlr, Pmch, csr8r(ctlr, Pmch) | 0x80);
		break;
	}
	rtl8169miimiw(ctlr->mii, 1, 0x1f, 0);
	rtl8169miimiw(ctlr->mii, 1, 0x0e, 0);

	/*
	 * Get rev number out of Phyidr2 so can config properly.
	 * There's probably more special stuff for Macv0[234] needed here.
	 */
	ctlr->phyv = rtl8169miimir(ctlr->mii, 1, Phyidr2) & 0x0F;
	if(ctlr->macv == Macv02){
		csr8w(ctlr, Ldps, 1);				/* magic */
		rtl8169miimiw(ctlr->mii, 1, 0x0B, 0x0000);	/* magic */
	}

	if(mii(ctlr->mii, (1<<1)) == 0 || (phy = ctlr->mii->curphy) == nil){
		free(ctlr->mii);
		ctlr->mii = nil;
		return -1;
	}
	print("rtl8169: oui %#ux phyno %d, macv = %#8.8ux phyv = %#4.4ux\n",
		phy->oui, phy->phyno, ctlr->macv, ctlr->phyv);

	miireset(ctlr->mii);

	microdelay(100);

	miiane(ctlr->mii, ~0, ~0, ~0);

	return 0;
}

static void
rtl8169promiscuous(void* arg, int on)
{
	Ether *edev;
	Ctlr * ctlr;

	edev = arg;
	ctlr = edev->ctlr;
	ilock(ctlr);
	if(on)
		ctlr->rcr |= Aap;
	else
		ctlr->rcr &= ~Aap;
	csr32w(ctlr, Rcr, ctlr->rcr);
	iunlock(ctlr);
}

enum {
	/* everyone else uses 0x04c11db7, but they both produce the same crc */
	Etherpolybe = 0x04c11db6,
	Bytemask = (1<<8) - 1,
};

static ulong
ethercrcbe(uchar *addr, long len)
{
	int i, j;
	ulong c, crc, carry;

	crc = ~0UL;
	for (i = 0; i < len; i++) {
		c = addr[i];
		for (j = 0; j < 8; j++) {
			carry = ((crc & (1UL << 31))? 1: 0) ^ (c & 1);
			crc <<= 1;
			c >>= 1;
			if (carry)
				crc = (crc ^ Etherpolybe) | carry;
		}
	}
	return crc;
}

static ulong
swabl(ulong l)
{
	return l>>24 | (l>>8) & (Bytemask<<8) |
		(l<<8) & (Bytemask<<16) | l<<24;
}

static void
rtl8169multicast(void* ether, uchar *eaddr, int add)
{
	Ether *edev;
	Ctlr *ctlr;

	if (!add)
		return;	/* ok to keep receiving on old mcast addrs */

	edev = ether;
	ctlr = edev->ctlr;
	ilock(ctlr);

	ctlr->mchash |= 1ULL << (ethercrcbe(eaddr, Eaddrlen) >> 26);

	ctlr->rcr |= Am;
	csr32w(ctlr, Rcr, ctlr->rcr);

	/* pci-e variants reverse the order of the hash byte registers */
	if (ctlr->pcie) {
		csr32w(ctlr, Mar0,   swabl(ctlr->mchash>>32));
		csr32w(ctlr, Mar0+4, swabl(ctlr->mchash));
	} else {
		csr32w(ctlr, Mar0,   ctlr->mchash);
		csr32w(ctlr, Mar0+4, ctlr->mchash>>32);
	}

	iunlock(ctlr);
}

static long
rtl8169ifstat(Ether* edev, void* a, long n, ulong offset)
{
	char *p;
	Ctlr *ctlr;
	Dtcc *dtcc;
	int i, l, r, timeo;

	p = smalloc(READSTR);

	ctlr = edev->ctlr;
	qlock(&ctlr->slock);

	if(waserror()){
		qunlock(&ctlr->slock);
		free(p);
		nexterror();
	}

	csr32w(ctlr, Dtccr+4, 0);
	csr32w(ctlr, Dtccr, PCIWADDR(ctlr->dtcc)|Cmd);
	for(timeo = 0; timeo < 1000; timeo++){
		if(!(csr32r(ctlr, Dtccr) & Cmd))
			break;
		delay(1);
	}
	if(csr32r(ctlr, Dtccr) & Cmd)
		error(Eio);
	dtcc = ctlr->dtcc;

	edev->oerrs = dtcc->txer;
	edev->crcs = dtcc->rxer;
	edev->frames = dtcc->fae;
	edev->buffs = dtcc->misspkt;
	edev->overflows = ctlr->txdu+ctlr->rdu;

	if(n == 0){
		qunlock(&ctlr->slock);
		poperror();
		free(p);
		return 0;
	}

	l = snprint(p, READSTR, "TxOk: %llud\n", dtcc->txok);
	l += snprint(p+l, READSTR-l, "RxOk: %llud\n", dtcc->rxok);
	l += snprint(p+l, READSTR-l, "TxEr: %llud\n", dtcc->txer);
	l += snprint(p+l, READSTR-l, "RxEr: %ud\n", dtcc->rxer);
	l += snprint(p+l, READSTR-l, "MissPkt: %ud\n", dtcc->misspkt);
	l += snprint(p+l, READSTR-l, "FAE: %ud\n", dtcc->fae);
	l += snprint(p+l, READSTR-l, "Tx1Col: %ud\n", dtcc->tx1col);
	l += snprint(p+l, READSTR-l, "TxMCol: %ud\n", dtcc->txmcol);
	l += snprint(p+l, READSTR-l, "RxOkPh: %llud\n", dtcc->rxokph);
	l += snprint(p+l, READSTR-l, "RxOkBrd: %llud\n", dtcc->rxokbrd);
	l += snprint(p+l, READSTR-l, "RxOkMu: %ud\n", dtcc->rxokmu);
	l += snprint(p+l, READSTR-l, "TxAbt: %ud\n", dtcc->txabt);
	l += snprint(p+l, READSTR-l, "TxUndrn: %ud\n", dtcc->txundrn);

	l += snprint(p+l, READSTR-l, "serr: %ud\n", ctlr->serr);
	l += snprint(p+l, READSTR-l, "fovw: %ud\n", ctlr->fovw);

	l += snprint(p+l, READSTR-l, "txdu: %ud\n", ctlr->txdu);
	l += snprint(p+l, READSTR-l, "tcpf: %ud\n", ctlr->tcpf);
	l += snprint(p+l, READSTR-l, "udpf: %ud\n", ctlr->udpf);
	l += snprint(p+l, READSTR-l, "ipf: %ud\n", ctlr->ipf);
	l += snprint(p+l, READSTR-l, "fovf: %ud\n", ctlr->fovf);
	l += snprint(p+l, READSTR-l, "rer: %ud\n", ctlr->rer);
	l += snprint(p+l, READSTR-l, "rdu: %ud\n", ctlr->rdu);
	l += snprint(p+l, READSTR-l, "punlc: %ud\n", ctlr->punlc);

	l += snprint(p+l, READSTR-l, "tcr: %#8.8ux\n", ctlr->tcr);
	l += snprint(p+l, READSTR-l, "rcr: %#8.8ux\n", ctlr->rcr);
	l += snprint(p+l, READSTR-l, "multicast: %ud\n", ctlr->mcast);

	if(ctlr->mii != nil && ctlr->mii->curphy != nil){
		l += snprint(p+l, READSTR-l, "phy:   ");
		for(i = 0; i < NMiiPhyr; i++){
			if(i && ((i & 0x07) == 0))
				l += snprint(p+l, READSTR-l, "\n       ");
			r = miimir(ctlr->mii, i);
			l += snprint(p+l, READSTR-l, " %4.4ux", r);
		}
		snprint(p+l, READSTR-l, "\n");
	}

	n = readstr(offset, a, n, p);

	qunlock(&ctlr->slock);
	poperror();
	free(p);

	return n;
}

static void
rtl8169halt(Ctlr* ctlr)
{
	csr8w(ctlr, Cr, 0);
	csr16w(ctlr, Imr, 0);
	csr16w(ctlr, Isr, ~0);
}

static int
rtl8169reset(Ctlr* ctlr)
{
	u32int r;
	int timeo;

	/*
	 * Soft reset the controller.
	 */
	csr8w(ctlr, Cr, Rst);
	for(r = timeo = 0; timeo < 1000; timeo++){
		r = csr8r(ctlr, Cr);
		if(!(r & Rst))
			break;
		delay(1);
	}
	rtl8169halt(ctlr);

	if(r & Rst)
		return -1;
	return 0;
}

static void
rtl8169replenish(Ctlr* ctlr)
{
	D *d;
	int x;
	Block *bp;

	x = ctlr->rdt;
	while(NEXT(x, ctlr->nrd) != ctlr->rdh){
		bp = iallocb(Mps);
		if(bp == nil){
			iprint("rtl8169: no available buffers\n");
			break;
		}
		ctlr->rb[x] = bp;
		ctlr->nrq++;
		d = &ctlr->rd[x];
		d->addrlo = PCIWADDR(bp->rp);
		d->addrhi = 0;
		coherence();
		d->control = (d->control & Eor) | Own | BALLOC(bp);
		x = NEXT(x, ctlr->nrd);
		ctlr->rdt = x;
	}
}

static int
rtl8169init(Ether* edev)
{
	int i;
	u32int r;
	Block *bp;
	Ctlr *ctlr;
	u16int cplusc;

	ctlr = edev->ctlr;
	ilock(ctlr);

	rtl8169reset(ctlr);

	memset(ctlr->td, 0, sizeof(D)*ctlr->ntd);
	ctlr->tdh = ctlr->tdt = ctlr->ntq = 0;
	ctlr->td[ctlr->ntd-1].control = Eor;
	for(i = 0; i < ctlr->ntd; i++)
		if(bp = ctlr->tb[i]){
			ctlr->tb[i] = nil;
			freeb(bp);
		}

	memset(ctlr->rd, 0, sizeof(D)*ctlr->nrd);
	ctlr->rdh = ctlr->rdt = ctlr->nrq = 0;
	ctlr->rd[ctlr->nrd-1].control = Eor;
	for(i = 0; i < ctlr->nrd; i++)
		if(bp = ctlr->rb[i]){
			ctlr->rb[i] = nil;
			freeb(bp);
		}

	rtl8169replenish(ctlr);

	cplusc = csr16r(ctlr, Cplusc);
	cplusc &= ~(Endian|Rxchksum);
	cplusc |= Txenb|Mulrw;
	switch(ctlr->macv){
	case Macv40:
	case Macv44:
		cplusc |= Macstatdis;
		break;
	default:
		cplusc |= Rxenb;
		break;
	}
	csr16w(ctlr, Cplusc, cplusc);

	csr32w(ctlr, Tnpds+4, 0);
	csr32w(ctlr, Tnpds, PCIWADDR(ctlr->td));
	csr32w(ctlr, Rdsar+4, 0);
	csr32w(ctlr, Rdsar, PCIWADDR(ctlr->rd));

	csr8w(ctlr, Cr, Te|Re);

	csr32w(ctlr, Tcr, Ifg1|Ifg0|Mtxdmaunlimited);
	ctlr->tcr = csr32r(ctlr, Tcr);
	switch(ctlr->macv){
	case Macv42:
	case Macv45:
		ctlr->rcr = Rxfth256|Mrxdmaunlimited|Ab|Am|Apm;
		break;
	default:
		ctlr->rcr = Rxfthnone|Mrxdmaunlimited|Ab|Am|Apm;
		break;
	}
	ctlr->mchash = 0;
	csr32w(ctlr, Mar0,   0);
	csr32w(ctlr, Mar0+4, 0);
	csr32w(ctlr, Rcr, ctlr->rcr);

	/* maximum packet sizes, unlimited */
	csr8w(ctlr, Etx, 0x3f);
	csr16w(ctlr, Rms, 0x3fff);

	csr16w(ctlr, Coal, 0);

	/* no early rx interrupts */
	r = csr16r(ctlr, Mulint) & 0xF000;
	csr16w(ctlr, Mulint, r);

	ctlr->imr = Serr|Fovw|Punlc|Rdu|Ter|Rer|Rok|Tdu;
	csr16w(ctlr, Imr, ctlr->imr);

	csr32w(ctlr, Mpc, 0);

	iunlock(ctlr);

	return 0;
}

static void
rtl8169reseter(void *arg)
{
	Ether *edev;
	Ctlr *ctlr;

	edev = arg;

	for(;;){
		rtl8169init(edev);

		ctlr = edev->ctlr;
		qunlock(&ctlr->alock);

		while(waserror())
			;
		sleep(&ctlr->reset, return0, nil);
		poperror();

		qlock(&ctlr->alock);
	}
}

static void
rtl8169attach(Ether* edev)
{
	int timeo;
	Ctlr *ctlr;

	ctlr = edev->ctlr;
	qlock(&ctlr->alock);
	if(!ctlr->init){
		ctlr->ntd = Ntd;
		ctlr->nrd = Nrd;
		ctlr->tb = malloc(ctlr->ntd*sizeof(Block*));
		ctlr->rb = malloc(ctlr->nrd*sizeof(Block*));
		ctlr->td = mallocalign(sizeof(D)*ctlr->ntd, 256, 0, 0);
		ctlr->rd = mallocalign(sizeof(D)*ctlr->nrd, 256, 0, 0);
		ctlr->dtcc = mallocalign(sizeof(Dtcc), 64, 0, 0);
		if(ctlr->rb == nil || ctlr->rb == nil || 
		   ctlr->rd == nil || ctlr->rd == nil || ctlr->dtcc == nil){
			free(ctlr->tb);
			ctlr->tb = nil;
			free(ctlr->rb);
			ctlr->rb = nil;
			free(ctlr->td);
			ctlr->td = nil;
			free(ctlr->rd);
			ctlr->rd = nil;
			free(ctlr->dtcc);
			ctlr->dtcc = nil;
			qunlock(&ctlr->alock);
			error(Enomem);
		}
		ctlr->init = 1;
		kproc("rtl8169", rtl8169reseter, edev);

		/* rtl8169reseter() does qunlock(&ctlr->alock) when complete */
		qlock(&ctlr->alock);
	}
	qunlock(&ctlr->alock);

	/*
	 * Wait for link to be ready.
	 */
	for(timeo = 0; timeo < 35; timeo++){
		if(miistatus(ctlr->mii) == 0)
			break;
		delay(100);		/* print fewer miistatus messages */
	}
}

static void
rtl8169link(Ether* edev)
{
	uint r;
	int limit;
	Ctlr *ctlr;

	ctlr = edev->ctlr;

	r = csr8r(ctlr, Phystatus);
	/*
	 * Maybe the link changed - do we care very much?
	 * Could stall transmits if no link, maybe?
	 */
	edev->link = (r & Linksts) != 0;

	limit = 256*1024;
	if(r & Speed10){
		edev->mbps = 10;
		limit = 65*1024;
	} else if(r & Speed100)
		edev->mbps = 100;
	else if(r & Speed1000)
		edev->mbps = 1000;

	if(edev->oq != nil)
		qsetlimit(edev->oq, limit);
}

static void
rtl8169transmit(Ether* edev)
{
	D *d;
	Block *bp;
	Ctlr *ctlr;
	int x;

	ctlr = edev->ctlr;

	if(!canlock(ctlr))
		return;
	for(x = ctlr->tdh; ctlr->ntq > 0; x = NEXT(x, ctlr->ntd)){
		d = &ctlr->td[x];
		if(d->control & Own)
			break;

		/*
		 * Free it up.
		 * Need to clean the descriptor here? Not really.
		 * Simple freeb for now (no chain and freeblist).
		 * Use ntq count for now.
		 */
		freeb(ctlr->tb[x]);
		ctlr->tb[x] = nil;
		ctlr->ntq--;
	}
	ctlr->tdh = x;

	x = ctlr->tdt;
	while(ctlr->ntq < (ctlr->ntd-1)){
		if((bp = qget(edev->oq)) == nil)
			break;

		d = &ctlr->td[x];
		d->addrlo = PCIWADDR(bp->rp);
		d->addrhi = 0;
		coherence();
		d->control = (d->control & Eor) | Own | Fs | Ls | BLEN(bp);

		ctlr->tb[x] = bp;
		ctlr->ntq++;

		x = NEXT(x, ctlr->ntd);
	}
	if(x != ctlr->tdt)
		ctlr->tdt = x;
	else if(ctlr->ntq >= (ctlr->ntd-1))
		ctlr->txdu++;

	if(ctlr->ntq > 0){
		coherence();
		csr8w(ctlr, Tppoll, Npq);
	}
	unlock(ctlr);
}

static void
rtl8169receive(Ether* edev)
{
	D *d;
	Block *bp;
	Ctlr *ctlr;
	u32int control;
	int x;

	ctlr = edev->ctlr;
	if(ctlr->nrq < ctlr->nrd/2)
		rtl8169replenish(ctlr);

	for(x = ctlr->rdh; x != ctlr->rdt;){
		d = &ctlr->rd[x];
		if((control = d->control) & Own)
			break;

		bp = ctlr->rb[x];
		ctlr->rb[x] = nil;
		ctlr->nrq--;

		x = NEXT(x, ctlr->nrd);
		ctlr->rdh = x;

		if(ctlr->nrq < ctlr->nrd/2)
			rtl8169replenish(ctlr);

		if((control & (Fs|Ls|Res)) == (Fs|Ls)){
			bp->wp = bp->rp + (control & RxflMASK) - 4;

			if(control & Fovf)
				ctlr->fovf++;
			if(control & Mar)
				ctlr->mcast++;

			switch(control & (Pid1|Pid0)){
			default:
				break;
			case Pid0:
				if(control & Tcpf){
					ctlr->tcpf++;
					break;
				}
				bp->flag |= Btcpck;
				break;
			case Pid1:
				if(control & Udpf){
					ctlr->udpf++;
					break;
				}
				bp->flag |= Budpck;
				break;
			case Pid1|Pid0:
				if(control & Ipf){
					ctlr->ipf++;
					break;
				}
				bp->flag |= Bipck;
				break;
			}
			etheriq(edev, bp);
		}else{
			if(!(control & Res))
				ctlr->frag++;
			freeb(bp);
		}
	}
}

static void
rtl8169restart(Ctlr *ctlr)
{
	ctlr->imr = 0;
	rtl8169halt(ctlr);
	wakeup(&ctlr->reset);
}

static void
rtl8169interrupt(Ureg*, void* arg)
{
	Ctlr *ctlr;
	Ether *edev;
	u32int isr;

	edev = arg;
	ctlr = edev->ctlr;

	while((isr = csr16r(ctlr, Isr)) != 0 && isr != 0xFFFF){
		csr16w(ctlr, Isr, isr);
		if((isr & ctlr->imr) == 0)
			break;

		if(isr & Serr)
			ctlr->serr++;
		if(isr & Fovw)
			ctlr->fovw++;
		if(isr & Rer)
			ctlr->rer++;
		if(isr & Rdu)
			ctlr->rdu++;
		if(isr & Punlc)
			ctlr->punlc++;

		if(isr & (Serr|Fovw)){
			rtl8169restart(ctlr);
			break;
		}

		if(isr & (Punlc|Rdu|Rer|Rok))
			rtl8169receive(edev);

		if(isr & (Tdu|Ter|Tok))
			rtl8169transmit(edev);

		if(isr & Punlc)
			rtl8169link(edev);
	}
}

int
vetmacv(Ctlr *ctlr, uint *macv)
{
	*macv = csr32r(ctlr, Tcr) & HwveridMASK;
	switch(*macv){
	default:
		return -1;
	case Macv01:
	case Macv02:
	case Macv03:
	case Macv04:
	case Macv05:
	case Macv07:
	case Macv07a:
	case Macv11:
	case Macv12:
	case Macv12a:
	case Macv13:
	case Macv14:
	case Macv15:
	case Macv25:
	case Macv26:
	case Macv27:
	case Macv28:
	case Macv29:
	case Macv30:
	case Macv39:
	case Macv40:
	case Macv42:
	case Macv44:
	case Macv45:
		break;
	}
	return 0;
}

static void
rtl8169pci(void)
{
	Pcidev *p;
	Ctlr *ctlr;
	int i, port, pcie;
	uint macv;

	p = nil;
	while(p = pcimatch(p, 0, 0)){
		if(p->ccrb != 0x02 || p->ccru != 0)
			continue;

		pcie = 0;
		switch(i = ((p->did<<16)|p->vid)){
		default:
			continue;
		case Rtl8100e:			/* RTL810[01]E ? */
		case Rtl8168b:			/* RTL8168B */
			pcie = 1;
			break;
		case Rtl8169c:			/* RTL8169C */
		case Rtl8169sc:			/* RTL8169SC */
		case Rtl8169:			/* RTL8169 */
			break;
		case (0xC107<<16)|0x1259:	/* Corega CG-LAPCIGT */
			i = Rtl8169;
			break;
		}

		port = p->mem[0].bar & ~0x01;
		if(ioalloc(port, p->mem[0].size, 0, "rtl8169") < 0){
			print("rtl8169: port %#ux in use\n", port);
			continue;
		}
		ctlr = malloc(sizeof(Ctlr));
		if(ctlr == nil){
			print("rtl8169: can't allocate memory\n");
			iofree(port);
			continue;
		}
		ctlr->port = port;
		ctlr->pcidev = p;
		ctlr->pciv = i;
		ctlr->pcie = pcie;

		pcienable(p);
		if(vetmacv(ctlr, &macv) == -1){
			pcidisable(p);
			iofree(port);
			free(ctlr);
			print("rtl8169: unknown mac %.4ux %.8ux\n", p->did, macv);
			continue;
		}

		if(rtl8169reset(ctlr)){
			pcidisable(p);
			iofree(port);
			free(ctlr);
			print("rtl8169: reset failed\n");
			continue;
		}

		/*
		 * Extract the chip hardware version,
		 * needed to configure each properly.
		 */
		ctlr->macv = macv;

		rtl8169mii(ctlr);

		pcisetbme(p);

		if(rtl8169ctlrhead != nil)
			rtl8169ctlrtail->next = ctlr;
		else
			rtl8169ctlrhead = ctlr;
		rtl8169ctlrtail = ctlr;
	}
}

static int
rtl8169pnp(Ether* edev)
{
	u32int r;
	Ctlr *ctlr;
	uchar ea[Eaddrlen];
	static int once;

	if(once == 0){
		once = 1;
		rtl8169pci();
	}

	/*
	 * Any adapter matches if no edev->port is supplied,
	 * otherwise the ports must match.
	 */
	for(ctlr = rtl8169ctlrhead; ctlr != nil; ctlr = ctlr->next){
		if(ctlr->active)
			continue;
		if(edev->port == 0 || edev->port == ctlr->port){
			ctlr->active = 1;
			break;
		}
	}
	if(ctlr == nil)
		return -1;

	edev->ctlr = ctlr;
	edev->port = ctlr->port;
	edev->irq = ctlr->pcidev->intl;
	edev->tbdf = ctlr->pcidev->tbdf;
	edev->mbps = 100;
	edev->maxmtu = Mtu;

	/*
	 * Check if the adapter's station address is to be overridden.
	 * If not, read it from the device and set in edev->ea.
	 */
	memset(ea, 0, Eaddrlen);
	if(memcmp(ea, edev->ea, Eaddrlen) == 0){
		r = csr32r(ctlr, Idr0);
		edev->ea[0] = r;
		edev->ea[1] = r>>8;
		edev->ea[2] = r>>16;
		edev->ea[3] = r>>24;
		r = csr32r(ctlr, Idr0+4);
		edev->ea[4] = r;
		edev->ea[5] = r>>8;
	}

	edev->attach = rtl8169attach;
	edev->transmit = rtl8169transmit;
	edev->ifstat = rtl8169ifstat;

	edev->arg = edev;
	edev->promiscuous = rtl8169promiscuous;
	edev->multicast = rtl8169multicast;

	rtl8169link(edev);

	intrenable(edev->irq, rtl8169interrupt, edev, edev->tbdf, edev->name);

	return 0;
}

void
ether8169link(void)
{
	addethercard("rtl8169", rtl8169pnp);
}
