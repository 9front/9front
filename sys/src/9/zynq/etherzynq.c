#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/netif.h"
#include "../port/etherif.h"

#define Rbsz		ROUNDUP(sizeof(Etherpkt)+16, 64)

enum {
	RXRING = 0x200,
	TXRING = 0x200,
	Linkdelay = 500,
	MDC_DIV = 6,
};

enum {
	NET_CTRL,
	NET_CFG,
	NET_STATUS,
	DMA_CFG = 4,
	TX_STATUS,
	RX_QBAR,
	TX_QBAR,
	RX_STATUS,
	INTR_STATUS,
	INTR_EN,
	INTR_DIS,
	INTR_MASK,
	PHY_MAINT,
	RX_PAUSEQ,
	TX_PAUSEQ,
	HASH_BOT = 32,
	HASH_TOP,
	SPEC_ADDR1_BOT,
	SPEC_ADDR1_TOP,
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
};

enum {
	/* NET_CTRL */
	RXEN = 1<<2,
	TXEN = 1<<3,
	MDEN = 1<<4,
	STARTTX = 1<<9,
	/* NET_CFG */
	SPEED = 1<<0,
	FDEN = 1<<1,
	COPYALLEN = 1<<4,
	MCASTHASHEN = 1<<6,
	UCASTHASHEN = 1<<7,
	RX1536EN = 1<<8,
	GIGE_EN = 1<<10,
	RXCHKSUMEN = 1<<24,
	/* NET_STATUS */
	PHY_IDLE = 1<<2,
	/* DMA_CFG */
	TXCHKSUMEN  = 1<<11,
	/* TX_STATUS */
	TXCOMPL = 1<<5,
	/* INTR_{EN,DIS} */
	MGMTDONE = 1<<0,
	RXCOMPL = 1<<1,
	RXUSED = 1<<2,
	TXUNDER = 1<<4,
	RXOVER = 1<<10,
	/* MDCTRL */
	MDRESET = 1<<15,
	AUTONEG = 1<<12,
	FULLDUP = 1<<8,
	/* MDSTATUS */
	LINK = 1<<2,
	/* MDGSTATUS */
	RECVOK = 3<<12,
};

enum {
	RxUsed = 1,
	TxUsed = 1<<31,
	FrameEnd = 1<<15,
};

enum {
	GEM0_CLK_CTRL = 0x140/4,
};

typedef struct Ctlr Ctlr;

struct Ctlr {
	ulong *r;
	Rendez phy;
	int phyaddr;
	int rxconsi, rxprodi, txi;
	ulong *rxr, *txr;
	Block **rxs, **txs;
	Lock txlock;
	int attach;
};

static int
phyidle(void *v)
{
	return ((Ctlr*)v)->r[NET_STATUS] & PHY_IDLE;
}

static void
mdwrite(Ctlr *c, int r, u16int v)
{
	sleep(&c->phy, phyidle, c);
	c->r[PHY_MAINT] = 1<<30 | 1<<28 | 1<<17 | c->phyaddr << 23 | r << 18 | v;
	sleep(&c->phy, phyidle, c);
}

static u16int
mdread(Ctlr *c, int r)
{
	sleep(&c->phy, phyidle, c);
	c->r[PHY_MAINT] = 1<<30 | 1<< 29 | 1<<17 | c->phyaddr << 23 | r << 18;
	sleep(&c->phy, phyidle, c);
	return c->r[PHY_MAINT];
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
			c->r[NET_CFG] |= GIGE_EN;
			slcr[GEM0_CLK_CTRL] = 1 << 20 | 8 << 8 | 1;
		}else if((v & 0x20) != 0){
			sp = "100BASE-TX";
			edev->mbps = 100;
			c->r[NET_CFG] = c->r[NET_CFG] & ~GIGE_EN | SPEED;
			slcr[GEM0_CLK_CTRL] = 5 << 20 | 8 << 8 | 1;
		}else if((v & 0x10) != 0){
			sp = "10BASE-T";
			edev->mbps = 10;
			c->r[NET_CFG] = c->r[NET_CFG] & ~(GIGE_EN | SPEED);
			slcr[GEM0_CLK_CTRL] = 20 << 20 | 20 << 8 | 1;
		}else
			sp = "???";
		if((v & 0x08) != 0){
			dpl = "full";
			c->r[NET_CFG] |= FDEN;
		}else{
			dpl = "half";
			c->r[NET_CFG] &= ~FDEN;
		}
		edev->link = 1;
		print("eth: %s %s duplex link\n", sp, dpl);
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
		r = &c->rxr[2 * i];
		r[0] = RxUsed | PADDR(bp->rp);
		if(i == RXRING - 1)
			r[0] |= 2;
		r[1] = 0;
		cleandse(bp->base, bp->lim);
		clean2pa(PADDR(bp->base), PADDR(bp->lim));
		r[0] &= ~RxUsed;
		c->rxprodi = (c->rxprodi + 1) & (RXRING - 1);
	}
	return 0;
}

static void
ethrx(Ether *edev)
{
	Ctlr *c;
	ulong *r;
	Block *bp;

	c = edev->ctlr;
//	print("rx! %p %p\n", PADDR(&c->rxr[2 * c->rxconsi]), c->r[RX_QBAR]);
	for(;;){
		r = &c->rxr[2 * c->rxconsi];
		if((r[0] & RxUsed) == 0)
			break;
		if((r[1] & FrameEnd) == 0)
			print("eth: partial frame received -- shouldn't happen\n");
		bp = c->rxs[c->rxconsi];
		bp->wp = bp->rp + (r[1] & 0x1fff);
		invaldse(bp->rp, bp->wp);
		inval2pa(PADDR(bp->rp), PADDR(bp->wp));
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
		r = &c->txr[2 * c->txi];
		if((r[1] & TxUsed) == 0){
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
		clean2pa(PADDR(bp->rp), PADDR(bp->wp));
		r[0] = PADDR(bp->rp);
		r[1] = BLEN(bp) | FrameEnd | TxUsed;
		if(r == c->txr + 2 * (TXRING - 1))
			r[1] |= 1<<30;
		coherence();
		r[1] &= ~TxUsed;
		coherence();
		c->r[NET_CTRL] |= STARTTX;
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
	fl = c->r[INTR_STATUS];
	c->r[INTR_STATUS] = fl;
	if((fl & MGMTDONE) != 0)
		wakeup(&c->phy);
	if((fl & TXUNDER) != 0)
		ethtx(edev);
	if((fl & RXCOMPL) != 0)
		ethrx(edev);
	if((fl & RXUSED) != 0)
		print("eth: DMA read RX descriptor with used bit set, shouldn't happen\n");
	if((fl & RXOVER) != 0)
		print("eth: RX overrun, shouldn't happen\n");
}

static void
ethprom(void *arg, int on)
{
	Ether *edev;
	Ctlr *c;

	edev = arg;
	c = edev->ctlr;
	if(on)
		c->r[NET_CFG] |= COPYALLEN;
	else
		c->r[NET_CFG] &= ~COPYALLEN;
}

static void
ethmcast(void *arg, uchar *ea, int on)
{
	Ether *edev;
	Ctlr *c;
	u64int a;
	uchar x;

	edev = arg;
	c = edev->ctlr;
	if(edev->nmaddr == 0){
		c->r[NET_CFG] &= ~MCASTHASHEN;
		c->r[HASH_BOT] = 0;
		c->r[HASH_TOP] = 0;
	}
	if(!on)
		return;
	a = (u64int)ea[0]     | (u64int)ea[1]<<8  | (u64int)ea[2]<<16 |
	    (u64int)ea[3]<<24 | (u64int)ea[4]<<32 | (u64int)ea[5]<<40;
	x = a ^ (a>>6) ^ (a>>12) ^ (a>>18) ^ (a>>24) ^ (a>>30) ^ (a>>36) ^ (a>>42);
	x &= 63;
	if(x < 32)
		c->r[HASH_BOT] |= 1<<x;
	else
		c->r[HASH_TOP] |= 1<<(x-32);
	c->r[NET_CFG] |= MCASTHASHEN;
}

static int
ethinit(Ether *edev)
{
	Ctlr *c;
	int i;
	
	c = edev->ctlr;
	c->r[NET_CTRL] = 0;
	c->r[RX_STATUS] = 0xf;
	c->r[TX_STATUS] = 0xff;
	c->r[INTR_DIS] = 0x7FFFEFF;
	c->r[NET_CFG] = MDC_DIV << 18 | FDEN | SPEED | RX1536EN | GIGE_EN | RXCHKSUMEN;
	c->r[SPEC_ADDR1_BOT] = edev->ea[0] | edev->ea[1] << 8 | edev->ea[2] << 16 | edev->ea[3] << 24;
	c->r[SPEC_ADDR1_TOP] = edev->ea[4] | edev->ea[5] << 8;
	c->r[DMA_CFG] = TXCHKSUMEN | (Rbsz/64) << 16 | 1 << 10 | 3 << 8 | 0x10;

	c->rxr = ucalloc(8 * RXRING);
	c->txr = ucalloc(8 * TXRING);
	c->rxs = xspanalloc(4 * RXRING, 4, 0);
	c->txs = xspanalloc(4 * TXRING, 4, 0);
	for(i = 0; i < 2 * RXRING; ){
		c->rxr[i++] = 1;
		c->rxr[i++] = 0;
	}
	c->rxconsi = 1;
	replenish(c);
	c->rxconsi = 0;
	replenish(c);
	for(i = 0; i < 2 * TXRING; ){
		c->txr[i++] = 0;
		c->txr[i++] = 1<<31;
	}
	c->txr[2 * (TXRING - 1)] |= 1<<30;
	c->r[RX_QBAR] = PADDR(c->rxr);
	c->r[TX_QBAR] = PADDR(c->txr);
	
	c->r[NET_CTRL] = MDEN | TXEN | RXEN;
	c->r[INTR_EN] = MGMTDONE | TXUNDER | RXCOMPL | RXUSED | RXOVER;
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

static int
etherpnp(Ether *edev)
{
	static Ctlr ct;
	static uchar mac[] = {0x0e, 0xa7, 0xde, 0xad, 0xbe, 0xef};
	
	if(ct.r != nil)
		return -1;

	memmove(edev->ea, mac, 6);
	edev->ctlr = &ct;
	edev->port = ETH0_BASE;
	ct.r = vmap(edev->port, BY2PG);
	edev->irq = ETH0IRQ;
	edev->ctlr = &ct;
	edev->transmit = ethtx;
	edev->attach = ethattach;
	edev->promiscuous = ethprom;
	edev->multicast = ethmcast;
	edev->arg = edev;
	edev->mbps = 1000;
	
	if(ethinit(edev) < 0){
		edev->ctlr = nil;
		return -1;
	}

	intrenable(edev->irq, ethirq, edev, LEVEL, edev->name);
	return 0;
}

void
etherzynqlink(void)
{
	addethercard("eth", etherpnp);
}
