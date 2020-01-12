#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/netif.h"
#include "../port/etherif.h"

#define Rbsz ROUNDUP(sizeof(Etherpkt)+16, 64)

enum {
	Linkdelay = 500,
	RXRING = 512,
	TXRING = 512
};

enum {
	PERMODRST_EMAC1 = 1<<1,
	
	SYSMGR_EMAC_CTRL = 0x60/4,
	
	MAC_CONFIG = 0x0/4,
	MAC_FRAME_FILTER = 0x4/4,
	
	GMII_ADDRESS = 0x10/4,
	GMII_DATA = 0x14/4,
	
	INTERRUPT_STATUS = 0x38/4,
	INTERRUPT_MASK = 0x3C/4,
	
	MAC_ADDRESS = 0x40/4,
	HASH_TABLE = 0x500/4,
	
	DMA_BUS_MODE = 0x1000/4,
	DMA_BUS_MODE_SWR = 1<<0,
	DMA_TX_POLL = 0x1004/4,
	DMA_RX_POLL = 0x1008/4,
	DMA_STATUS = 0x1014/4,
	DMA_OPERATION_MODE = 0x1018/4,
	DMA_INTERRUPT_ENABLE = 0x101C/4,
	DMA_AXI_STATUS = 0x102C/4,
	
	RXRING_ADDRESS = 0x100C/4,
	TXRING_ADDRESS = 0x1010/4,

};

enum {
	MDCTRL,
	MDSTATUS,
	MDID1,
	MDID2,
	MDAUTOADV,
	MDAUTOPART,
	MDAUTOEX,
	MDAUTONEXT,
	MDAUTOLINK,
	MDGCTRL,
	MDGSTATUS,
	MDPHYCTRL = 0x1f,

	/* MDCTRL */
	MDRESET = 1<<15,
	AUTONEG = 1<<12,
	FULLDUP = 1<<8,
	/* MDSTATUS */
	LINK = 1<<2,
	/* MDGSTATUS */
	RECVOK = 3<<12,
};


typedef struct Ctlr Ctlr;

struct Ctlr {
	ulong *r;
	ulong *rxr, *txr;
	Block **rxs, **txs;
	int rxprodi, rxconsi, txi;
	int attach;
	Lock txlock;
	uchar (*mc)[6];
	int nmc;
};

static void
mdwrite(Ctlr *c, int r, u16int v)
{
	while((c->r[GMII_ADDRESS] & 1<<0) != 0)
		tsleep(&up->sleep, return0, nil, 1);
	c->r[GMII_DATA] = v;
	c->r[GMII_ADDRESS] = 1<<11 | (r&31)<<6 | 1<<1 | 1<<0;
	while((c->r[GMII_ADDRESS] & 1<<0) != 0)
		tsleep(&up->sleep, return0, nil, 1);
}

static u16int
mdread(Ctlr *c, int r)
{
	while((c->r[GMII_ADDRESS] & 1<<0) != 0)
		tsleep(&up->sleep, return0, nil, 1);
	c->r[GMII_ADDRESS] = 1<<11 | (r&31)<<6 | 1<<0;
	while((c->r[GMII_ADDRESS] & 1<<0) != 0)
		tsleep(&up->sleep, return0, nil, 1);
	return c->r[GMII_DATA];
}

static void
ethproc(void *ved)
{
	Ether *edev;
	Ctlr *c;
	char *sp, *dpl;
	u16int v;
	
	edev = ved;
	c = edev->ctlr;
	
	mdwrite(c, MDCTRL, AUTONEG);
	for(;;){
		if((mdread(c, MDSTATUS) & LINK) == 0){
			edev->link = 0;
			print("eth: no link\n");
			while((mdread(c, MDSTATUS) & LINK) == 0)
				tsleep(&up->sleep, return0, nil, Linkdelay);
		}
		v = mdread(c, MDPHYCTRL);
		if((v & 0x40) != 0){
			sp = "1000BASE-T";
			while((mdread(c, MDGSTATUS) & RECVOK) != RECVOK)
				;
			edev->mbps = 1000;
			c->r[MAC_CONFIG] &= ~(1<<15);

		}else if((v & 0x20) != 0){
			sp = "100BASE-TX";
			edev->mbps = 100;
			c->r[MAC_CONFIG] = c->r[MAC_CONFIG] | (1<<15|1<<14);
		}else if((v & 0x10) != 0){
			sp = "10BASE-T";
			edev->mbps = 10;
			c->r[MAC_CONFIG] = c->r[MAC_CONFIG] & ~(1<<14) | 1<<15;
		}else
			sp = "???";
		if((v & 0x08) != 0){
			dpl = "full";
			c->r[MAC_CONFIG] |= 1<<11;
		}else{
			dpl = "half";
			c->r[MAC_CONFIG] &= ~(1<<11);
		}
		edev->link = 1;
		print("eth: %s %s duplex link\n", sp, dpl);
		c->r[MAC_CONFIG] |= 1<<3 | 1<<2;
		while((mdread(c, MDSTATUS) & LINK) != 0)
			tsleep(&up->sleep, return0, nil, Linkdelay);
	}
}

static int
replenish(Ctlr *c)
{
	Block *bp;
	int i;
	ulong *r;
	
	while(c->rxprodi != c->rxconsi){
		i = c->rxprodi;
		bp = iallocb(Rbsz);
		if(bp == nil){
			print("eth: out of memory for receive buffers\n");
			return -1;
		}
		c->rxs[i] = bp;
		r = &c->rxr[4 * i];
		r[0] = 0;
		cleandse(bp->base, bp->lim);
		r[1] = Rbsz;
		if(i == RXRING - 1) r[1] |= 1<<15;
		r[2] = PADDR(bp->rp);
		r[3] = 0;
		r[0] |= 1<<31;
		c->rxprodi = (c->rxprodi + 1) & (RXRING - 1);
	}
	c->r[DMA_RX_POLL] = 0xBA5EBA11;
	return 0;
}

static void
ethrx(Ether *edev)
{
	Ctlr *c;
	ulong *r;
	Block *bp;
	
	c = edev->ctlr;
	for(;;){
		r = &c->rxr[4 * c->rxconsi];
		if((r[0] >> 31) != 0)
			break;
		if((r[0] & (3<<8)) != (3<<8))
			iprint("eth: lilu dallas multidescriptor\n");
		bp = c->rxs[c->rxconsi];
		bp->wp = bp->rp + (r[0] >> 16 & 0x3fff);
		invaldse(bp->rp, bp->wp);
		etheriq(edev, bp);
		c->rxconsi = (c->rxconsi + 1) & (RXRING - 1);
		replenish(c);
	}
}

static void
ethtx(Ether *edev)
{
	Ctlr *c;
	ulong *r;
	Block *bp;
	
	c = edev->ctlr;
	ilock(&c->txlock);
	for(;;){
		r = &c->txr[4 * c->txi];
		if((r[0] >> 31) != 0){
			print("eth: transmit buffer full\n");
			break;
		}
		bp = qget(edev->oq);
		if(bp == nil)
			break;
		if(c->txs[c->txi] != nil)
			freeb(c->txs[c->txi]);
		c->txs[c->txi] = bp;
		cleandse(bp->rp, bp->wp);
		r[0] = 1<<30 | 1<<29 | 1<<28;
		if(c->txi == TXRING - 1)
			r[0] |= 1<<21;
		r[1] = BLEN(bp);
		r[2] = PADDR(bp->rp);
		r[3] = 0;
		r[0] |= 1<<31;
		coherence();
		c->r[DMA_TX_POLL] = 0xBA5EBA11;
		c->txi = (c->txi + 1) & (TXRING - 1);
	}
	iunlock(&c->txlock);
}

static void
ethirq(Ureg *, void *arg)
{
	Ether *edev;
	Ctlr *c;
	ulong fl;
	
	edev = arg;
	c = edev->ctlr;
	fl = c->r[DMA_STATUS];
	c->r[DMA_STATUS] = fl;
	if((fl & 1<<1) != 0)
		iprint("eth: transmit stop\n");
	if((fl & (1<<0|1<<2)) != 0)
		ethtx(edev);
	if((fl & 1<<4) != 0)
		iprint("eth: receive overflow\n");
	if((fl & (1<<6|1<<7)) != 0)
		ethrx(edev);
	if((fl & 1<<8) != 0)
		iprint("eth: receive stop\n");
}

static int
ethinit(Ether *edev)
{
	Ctlr *c;
	
	c = edev->ctlr;
	resetmgr[PERMODRST] |= PERMODRST_EMAC1;
	/* assume bootloader set up clock */
	sysmgr[SYSMGR_EMAC_CTRL] = 1<<2 | 1; /* RGMII */
	sysmgr[FPGA_MODULE] = sysmgr[FPGA_MODULE] & ~(1<<2 | 1<<3);
	microdelay(1);
	resetmgr[PERMODRST] &= ~PERMODRST_EMAC1;
	
	/* reset DMA */
	c->r[DMA_BUS_MODE] |= DMA_BUS_MODE_SWR;
	do microdelay(1);
	while((c->r[DMA_BUS_MODE] & DMA_BUS_MODE_SWR) != 0);
	/* wait for AXI transactions to finish */
	while((c->r[DMA_AXI_STATUS] & 3) != 0)
		microdelay(1);
	/* set up bus mode (32 beat bursts) */
	c->r[DMA_BUS_MODE] |= 32 << 8;
	
	c->r[MAC_ADDRESS] = 1<<31 | edev->ea[5] << 8 | edev->ea[4];
	c->r[MAC_ADDRESS+1] = edev->ea[3] << 24 | edev->ea[2] << 16 | edev->ea[1] << 8 | edev->ea[0];
	c->r[MAC_FRAME_FILTER] = 0;
	
	c->rxr = ucalloc(16 * RXRING);
	c->txr = ucalloc(16 * TXRING);
	c->rxs = xspanalloc(4 * RXRING, 4, 0);
	c->txs = xspanalloc(4 * TXRING, 4, 0);
	memset(c->rxr, 0, 16 * RXRING);
	memset(c->txr, 0, 16 * TXRING);
	c->rxconsi = 1;
	replenish(c);
	c->rxconsi = 0;
	replenish(c);
	
	c->r[RXRING_ADDRESS] = PADDR(c->rxr);
	c->r[TXRING_ADDRESS] = PADDR(c->txr);
	c->r[DMA_STATUS] = -1;
	c->r[INTERRUPT_MASK] = -1;
	c->r[DMA_INTERRUPT_ENABLE] = 1<<16 | 1<<15 | 1<<8 | 1<<6 | 1<<2 | 1<<1 | 1<<0;
	c->r[DMA_OPERATION_MODE] = 1<<1 | 1<<13;
	return 0;
}

static void
ethattach(Ether *edev)
{
	Ctlr *c;

	c = edev->ctlr;
	if(c->attach)
		return;
	c->attach = 1;
	kproc("ethproc", ethproc, edev);
}

static void
ethprom(void *arg, int on)
{
	Ether *edev;
	Ctlr *c;
	
	edev = arg;
	c = edev->ctlr;
	if(on)
		c->r[MAC_FRAME_FILTER] |=  0x80000001;
	else
		c->r[MAC_FRAME_FILTER] &= ~0x80000001;
}

static void
sethash(uchar *ea, ulong *hash)
{
	ulong crc;
	int i;
	uchar n;
	
	crc = ethercrc(ea, 6);
	n = 0;
	for(i = 0; i < 8; i++){
		n = n << 1 | crc & 1;
		crc >>= 1;
	}
	n ^= 0xff;
	hash[n>>5] |= (1<<(n & 31));
}

static void
ethmcast(void *arg, uchar *ea, int on)
{
	enum { MCSlots = 31 };
	Ether *edev;
	Ctlr *c;
	int i, p;
	ulong hash[8];
	
	edev = arg;
	c = edev->ctlr;
	if(on){
		c->mc = realloc(c->mc, (c->nmc + 1) * 6);
		memmove(c->mc[c->nmc++], ea, 6);
	}else{
		for(i = 0; i < c->nmc; i++)
			if(memcmp(c->mc[i], ea, 6) == 0)
				break;
		if(i == c->nmc)
			return;
		memmove(c->mc[i], c->mc[i+1], (c->nmc - i - 1) * 6);
	}
	p = c->r[MAC_FRAME_FILTER];
	/* set promiscuous in order to not lose packets while updating */
	c->r[MAC_FRAME_FILTER] = p | 0x80000001;
	if(c->nmc <= MCSlots){
		for(i = 0; i < c->nmc; i++){
			c->r[MAC_ADDRESS + 2 * (i + 1)] = 1<<31 | c->mc[i][5] << 8 | c->mc[i][4];
			c->r[MAC_ADDRESS + 2 * (i + 1) + 1] = c->mc[i][3] << 24 | c->mc[i][2] << 16 | c->mc[i][1] << 8 | c->mc[i][0];
		}
		for(i = 2 * i; i < 2*MCSlots; i++)
			c->r[MAC_ADDRESS + 2 + i] = 0;
		c->r[MAC_FRAME_FILTER] = p & ~(1<<2);
	}else{
		memset(hash, 0, sizeof(hash));
		for(i = 0; i < c->nmc; i++)
			sethash(c->mc[i], hash);
		for(i = 0; i < 8; i++)
			c->r[HASH_TABLE + i] = hash[i];
		c->r[MAC_FRAME_FILTER] = p | 1<<2;
	}
}

static long
ethifstat(Ether *edev, void *a, long n, ulong offset)
{
	static char *names[] = {
		"txoctetcount_gb", "txframecount_gb", "txbroadcastframes_g", "txmulticastframes_g",
		"tx64octets_gb", "tx65to127octets_gb", "tx128to255octets_gb", "tx256to511octets_gb",
		"tx512to1023octets_gb", "tx1024tomaxoctets_gb", "txunicastframes_gb", "txmulticastframes_gb",
		"txbroadcastframes_gb", "txunderflowerror", "txsinglecol_g", "txmulticol_g",
		"txdeferred", "txlatecol", "txexesscol", "txcarriererr",
		"txoctetcnt", "txframecount_g", "txexcessdef", "txpauseframes",
		"txvlanframes_g", "txoversize_g", "rxframecount_gb", "rxoctetcount_gb",
		"rxoctetcount_g", "rxbroadcastframes_g", "rxmulticastframes_g", "rxcrcerror",
		"rxalignmenterror", "rxrunterror", "rxjabbererror", "rxundersize_g",
		"rxoversize_g", "rx64octets_gb", "rx65to127octets_gb", "rx128to255octets_gb",
		"rx256to511octets_gb", "rx512to1023octets_gb", "rx1024tomaxoctets_gb", "rxunicastframes_g",
		"rxlengtherror", "rxoutofrangetype", "rxpauseframes", "rxfifooverflow",
		"rxvlanframes_gb", "rxwatchdogerror", "rxrcverror", "rxctrlframes_g",
	};
	int i;
	char *buf, *p, *e;
	Ctlr *c;
	
	p = buf = smalloc(READSTR);
	e = p + READSTR;
	c = edev->ctlr;
	for(i = 0; i < nelem(names); i++)
		p = seprint(p, e, "%s: %lud\n", names[i], c->r[0x114/4 + i]);
	n = readstr(offset, a, n, buf);
	free(buf);
	return n;
}

static int
etherpnp(Ether *edev)
{
	static Ctlr ct;
	static uchar mac[] = {0x0e, 0xa7, 0xde, 0xad, 0xca, 0xfe};
	
	if(ct.r != nil)
		return -1;
	
	memmove(edev->ea, mac, 6);
	edev->ctlr = &ct;
	edev->port = EMAC1_BASE;
	ct.r = (ulong *) edev->port;
	edev->irq = EMAC1IRQ;
	edev->ctlr = &ct;
	edev->attach = ethattach;
	edev->transmit = ethtx;
	edev->arg = edev;
	edev->mbps = 1000;
	edev->promiscuous = ethprom;
	edev->multicast = ethmcast;
	edev->ifstat = ethifstat;

	if(ethinit(edev) < 0){
		edev->ctlr = nil;
		return -1;
	}
	
	intrenable(edev->irq, ethirq, edev, LEVEL, edev->name);
	return 0;
}

void
ethercycvlink(void)
{
	addethercard("eth", etherpnp);
}
