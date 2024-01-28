/*
 * MediaTek Ethernet for the MT7688
 * thank you to the folks at NetBSD
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


/* RX Descriptor Format */
#define RXD_LEN1(x)	(((x) >> 0) & 0x3fff)
#define RXD_LAST1	(1 << 14)
#define RXD_LEN0(x)	(((x) >> 16) & 0x3fff)
#define RXD_LAST0	(1 << 30)
#define RXD_DDONE	(1 << 31)
#define RXD_FOE(x)	(((x) >> 0) & 0x3fff)
#define RXD_FVLD	(1 << 14)
#define RXD_INFO(x)	(((x) >> 16) & 0xff)
#define RXD_PORT(x)	(((x) >> 24) & 0x7)
#define RXD_INFO_CPU	(1 << 27)
#define RXD_L4_FAIL	(1 << 28)
#define RXD_IP_FAIL	(1 << 29)
#define RXD_L4_VLD	(1 << 30)
#define RXD_IP_VLD	(1 << 31)


/* TX Descriptor Format */
#define TXD_LEN1(x)	(((x) & 0x3fff) << 0)
#define TXD_LAST1	(1 << 14)
#define TXD_BURST	(1 << 15)
#define TXD_LEN0(x)	(((x) & 0x3fff) << 16)
#define TXD_LAST0	(1 << 30)
#define TXD_DDONE	(1 << 31)
#define TXD_VIDX(x)	(((x) & 0xf) << 0)
#define TXD_VPRI(x)	(((x) & 0x7) << 4)
#define TXD_VEN		(1 << 7)
#define TXD_SIDX(x)	(((x) & 0xf) << 8)
#define TXD_SEN(x)	(1 << 13)
#define TXD_QN(x)	(((x) & 0x7) << 16)
#define TXD_PN(x)	(((x) & 0x7) << 24)
#define	TXD_PN_CPU	0
#define	TXD_PN_GDMA1	1
#define	TXD_PN_GDMA2	2
#define TXD_TCP_EN	(1 << 29)
#define TXD_UDP_EN	(1 << 30)
#define TXD_IP_EN	(1 << 31)


/* pdma global cfgs */
#define  GLO_CFG_TX_WB_DDONE	(1 << 6)
#define   GLO_CFG_BURST_SZ_4	(0 << 4)
#define   GLO_CFG_BURST_SZ_8	(1 << 4)
#define   GLO_CFG_BURST_SZ_16	(2 << 4)
#define  GLO_CFG_RX_DMA_EN		(1 << 2)
#define  GLO_CFG_TX_DMA_EN		(1 << 0)
#define	RX_DMA_BUSY				(1 << 3)
#define TX_DMA_BUSY				(1 << 1)


/* interupt masks */
#define  INT_RX_DONE_INT1		(1 << 17)
#define  INT_RX_DONE_INT0		(1 << 16)
#define  INT_TX_DONE_INT3		(1 << 3)
#define  INT_TX_DONE_INT2		(1 << 2)
#define  INT_TX_DONE_INT1		(1 << 1)
#define  INT_TX_DONE_INT0		(1 << 0)
#define  INT_RX_DUKKHA			(1 << 31)
#define  INT_TX_DUKKHA			(1 << 29)


/* mii stuff */
#define  PCTL0_WR_VAL(x)	(((x) & 0xffff) << 16)
#define  PCTL0_RD_CMD		(1 << 14)
#define  PCTL0_WR_CMD		(1 << 13)
#define  PCTL0_REG(x)		(((x) & 0x1f) << 8)
#define  PCTL0_ADDR(x)		(((x) & 0x1f) << 0)
#define  PCTL1_RD_VAL(x)	(((x) >> 16) & 0xffff)
#define  PCTL1_RD_DONE		(1 << 1)	/* read clear */
#define  PCTL1_WR_DONE		(1 << 0)	/* read clear */


/* Debugging options */
enum{
	Miidebug	=	0,
	Ethdebug	=	0,
	Attchbug	=	0,
};


enum{
	Nrd		= 256,	/* Number rx descriptors */
	Ntd		= 64,	/* Number tx descriptors */
	Rbsz	= 2048,	/* block size */
};


typedef struct Desc Desc;
typedef struct Ctlr Ctlr;


struct Desc
{
	u32int	ptr0;
	u32int	info1;
	u32int	ptr1;
	u32int	info2;
};


struct Ctlr
{
	int		attached;
	QLock;
	Ether	*edev;		/* point back */

	struct {
		Block	*b[Nrd];
		Desc	*d;
		Rendez;
		Lock;
	}	rx[1];

	struct {
		Block	*b[Ntd];
		Desc	*d;
		Rendez;
	}	tx[1];

	Mii	*mii;

	QLock	statlock;
	int		rxstat;
	int		rxintr;
	int		rxdmaerr;
	int		txstat;
	int		txintr;
	int		txdmaerr;
	int		nointr;
	int		badrx;
};


static u32int
sysrd(int offset)
{
	return *IO(u32int, (SYSCTLBASE + offset));
}


static void
syswr(int offset, u32int val)
{
	*IO(u32int, (SYSCTLBASE + offset)) = val;
}


static u32int
ethrd(int offset)
{
	return *IO(u32int, (ETHBASE + offset));
}


static void
ethwr(int offset, u32int val)
{
	*IO(u32int, (ETHBASE + offset)) = val;
}


static u32int
swrd(int offset)
{
	return *IO(u32int, (SWCHBASE + offset));
}


static void
swwr(int offset, u32int val)
{
	*IO(u32int, (SWCHBASE + offset)) = val;
}


static int
miird(Mii*, int pa, int ra)
{
	int val = 0;
	int	timeout;

	if(Miidebug)
		iprint("miird, phy_addr; %d phy_reg: %d\n", pa, ra);

	if(pa > 5)
		return -1;


	swwr(SW_PCTL0, PCTL0_RD_CMD | PCTL0_ADDR(pa) | PCTL0_REG(ra));
	delay(1);

	for(timeout = 0; timeout < 2000; timeout++){
		if((val = swrd(SW_PCTL1)) & PCTL1_RD_DONE)
			break;
		microdelay(100);
	}

	if(!(val & PCTL1_RD_DONE))
		return -1;

	return PCTL1_RD_VAL(val);
}


static int
miiwr(Mii*, int pa, int ra, int val)
{
	int timeout;

	if(Miidebug)
		iprint("miiwr, phy_addr; %d phy_reg: %d val: 0x%04X\n", pa, ra, val);

	if(pa > 5)
		return -1;

	swwr(SW_PCTL0, PCTL0_WR_CMD | PCTL0_WR_VAL(val) | PCTL0_ADDR(pa) | PCTL0_REG(ra));
	delay(1);

	for(timeout = 0; timeout < 2000; timeout++){
		if((val = swrd(SW_PCTL1)) & PCTL1_WR_DONE)
			break;
		microdelay(100);
	}

	if(!(val & PCTL1_WR_DONE))
		return -1;

	return 0;
}


static void
getmacaddr(Ether *edev)
{
	ulong msb, lsb;

	lsb = ethrd(GDMA1_MAC_LSB);
	msb = ethrd(GDMA1_MAC_MSB);

	edev->ea[0] = msb>>8;
	edev->ea[1] = msb>>0;
	edev->ea[2]	= lsb>>24;
	edev->ea[3] = lsb>>16;
	edev->ea[4] = lsb>>8;
	edev->ea[5] = lsb>>0;

	if(Attchbug){
		iprint("ether getmac: %04lX %08lX\n", (msb & 0xFFFF), lsb);
		delay(10);
	}
}


static void
ethreset(Ether *edev)
{
	ulong buf;
	int i;
	Ctlr *ctlr = edev->ctlr;
	Mii *mii = ctlr->mii;

	iprint("reset eth and ephy\n");
	delay(10);




	buf = sysrd(SYSCTL_RST);
	buf |= (1<<24);
	syswr(SYSCTL_RST, buf);
	delay(1);
	buf ^= (1<<24);
	syswr(SYSCTL_RST, buf);


	miiwr(mii, 0, 31, 0x2000);
	miiwr(mii, 0, 26, 0x0020);

	for(i = 0; i < 5; i++){
		miiwr(mii, i, 31, 0x8000);
		miiwr(mii, i, 0, 0x3100);
		miiwr(mii, i, 30, 0xa000);
		miiwr(mii, i, 31, 0xa000);
		miiwr(mii, i, 16, 0x0606);
		miiwr(mii, i, 23, 0x0f0e);
		miiwr(mii, i, 24, 0x1610);
		miiwr(mii, i, 30, 0x1f15);
		miiwr(mii, i, 28, 0x6111);
	}

	miiwr(mii, 0, 31, 0x5000);
	miiwr(mii, 0, 19, 0x004a);
	miiwr(mii, 0, 20, 0x015a);
	miiwr(mii, 0, 21, 0x00ee);
	miiwr(mii, 0, 22, 0x0033);
	miiwr(mii, 0, 23, 0x020a);
	miiwr(mii, 0, 24, 0x0000);
	miiwr(mii, 0, 25, 0x024a);
	miiwr(mii, 0, 26, 0x035a);
	miiwr(mii, 0, 27, 0x02ee);
	miiwr(mii, 0, 28, 0x0233);
	miiwr(mii, 0, 29, 0x000a);
	miiwr(mii, 0, 30, 0x0000);
	miiwr(mii, 0, 31, 0x4000);
	miiwr(mii, 0, 29, 0x000d);
	miiwr(mii, 0, 30, 0x0500);


}

static void
doreset(Ether *edev)
{
	ulong buf;

	buf = sysrd(SYSCTL_RST);
	buf |= (1<<24);
	syswr(SYSCTL_RST, buf);
	delay(1);
	buf ^= (1<<24);
	syswr(SYSCTL_RST, buf);

	iprint("reset switch\n");
	delay(10);

	if(Attchbug){
		iprint("ether did a reset\n");
		delay(10);
	}

/* basic switch init */
	swwr(SW_FCT0, 0xC8A07850);
	swwr(SW_SGC2, 0x00000000);
	swwr(SW_PFC1, 0x00405555);	//vlan options
	swwr(SW_POC0, 0x00007f7f);
	swwr(SW_POC2, 0x00007f7f);
	swwr(SW_FCT2, 0x0002500c);
	swwr(SW_SWGC, 0x0008a301);
	swwr(SW_SOCPC, 0x02404040);
	swwr(SW_FPORT, 0x3f502b28);
	swwr(SW_FPA, 0x00000000);

	USED(edev);

}


static int
rdfull(void *arg)
{
	Desc *d = arg;
	return (d->info1 & RXD_DDONE) == 1;
}


static void
rxproc(void *arg)
{
	Ether	*edev = arg;
	Ctlr	*ctlr = edev->ctlr;
	Block	*b;
	Desc	*d;
	int		len, i;



	i = ethrd(PDMA_RX0_CPU_IDX);	/* get current index */

	while(waserror())
		;

	for(;;){
		ctlr->rxstat++;
		i = (i + 1) % Nrd;
		d = &ctlr->rx->d[i];

		if((d->info1 & RXD_DDONE) == 0)
			sleep(ctlr->rx, rdfull, d);

		len = RXD_LEN0(d->info1);	/* get length of packet */
		b = ctlr->rx->b[i];

		if(len > 0){
			b->wp = b->rp + len;
			dcflush(b->rp, BLEN(b));	/* move block to ram */
			etheriq(edev, b);			/* move block to ether input queue */

		if(Ethdebug)
			iprint("rxproc: (%d) len=%d | ", i, len);

		} else {
			ctlr->badrx++;
			freeb(b);
		}

			/* replenish */
			b = iallocb(Rbsz);
			if(b == nil)
				panic("NO RX BLOCKS");
			ctlr->rx->b[i] = b;
			dcflush(b->rp, Rbsz);
			d->ptr0 = PADDR(b->rp);	/* point to fresh block */
			d->info1 = 0;			/* clear out info1 & 2 */
			d->info2 = 0;
			d->info1 = RXD_LAST0;	/* clear ddone */

		ethwr(PDMA_RX0_CPU_IDX, i);	/* move to next index */
	}
}


static int
tdfree(void *arg)
{
	Desc *d = arg;
	return (d->info1 & (TXD_LAST0  | TXD_DDONE)) == (TXD_LAST0  | TXD_DDONE);
}


static void
txproc(void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	Block *b;
	Desc *d;
	int i, len, Δlen;

	i = ethrd(PDMA_TX0_CPU_IDX);	/* get current index */

	while(waserror())
		;

	for(;;){
		ctlr->txstat++;
		if((b = qbread(edev->oq, 100000)) == nil)	/* fetch packet from queue */
			break;
		

		d = &ctlr->tx->d[i];
		while(!tdfree(d))
			sleep(ctlr->tx, tdfree, d);

		ilock(ctlr->tx);	/* helps with packet loss */
		if(ctlr->tx->b[i] != nil)
			freeb(ctlr->tx->b[i]);

		ctlr->tx->b[i] = b;
		len = BLEN(b);

		if(len < 64){	/* tx needs at least 64 per packet */
			Δlen = 64 - len;
			b = padblock(b, -Δlen);
			len = BLEN(b) + Δlen;
		}

		if(Ethdebug)
			iprint("txproc: (%d) len=%d | ", i, len);

		dcflush(b->rp, Rbsz);	/* move packet to ram */
		d->ptr0 = PADDR(b->rp);
	//	d->info2 = TXD_QN(3) | TXD_PN(TXD_PN_GDMA1);
		d->info1 = TXD_LEN0(len) | TXD_LAST0;

		i = (i + 1) % Ntd;
		ethwr(PDMA_TX0_CPU_IDX, i);
		iunlock(ctlr->tx);
	}
}


static void
etherinterrupt(Ureg*, void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	u32int irq;
	int rxintΔ, txintΔ;


	rxintΔ = ctlr->rxintr;
	txintΔ = ctlr->txintr;

	irq = ethrd(INT_STATUS);	/* get interrupt requests */

	if(Ethdebug){
		iprint("ether interrupt: %08uX |", irq);
		delay(10);
	}


	if(irq & (INT_RX_DONE_INT0 | INT_RX_DONE_INT1)){
		ctlr->rxintr++;
		wakeup(ctlr->rx);
	}

	if(irq & (INT_TX_DONE_INT0 | INT_TX_DONE_INT1 | 
		INT_TX_DONE_INT2 | INT_TX_DONE_INT3)){
		ctlr->txintr++;
		wakeup(ctlr->tx);
	}

	if((rxintΔ == ctlr->rxintr) && (txintΔ == ctlr->txintr)){
		ctlr->nointr++;
		iprint("etherinterrupt: spurious %X\n", irq);
	}

	if(irq & INT_TX_DUKKHA)
		ctlr->txdmaerr++;

	if(irq & INT_RX_DUKKHA)
		ctlr->rxdmaerr++;

	ethwr(INT_STATUS, irq);	/* writing back 1's clears irqs */
}


static int
initmii(Ctlr *ctlr)
{

/*
 *	since the ethernet is wired right into 
 *	a 7? port switch, much of the mii stuff
 * 	is handled by the switch start up
 */

	Ether	*edev = ctlr->edev;	

	if((ctlr->mii = malloc(sizeof(Mii))) == nil)
		return -1;

	ctlr->mii->ctlr	= ctlr;
	ctlr->mii->mir	= miird;
	ctlr->mii->miw	= miiwr;

	if(mii(ctlr->mii, ~0) == 0 || ctlr->mii->curphy == nil){
		iprint("#l%d: init mii failure\n", edev->ctlrno);
		free(ctlr->mii);
		ctlr->mii = nil;
		return -1;
	}
	
	iprint("#l%d: phy%d id %.8ux oui %x\n", 
		edev->ctlrno, ctlr->mii->curphy->phyno, 
		ctlr->mii->curphy->id, ctlr->mii->curphy->oui);

	miireset(ctlr->mii);

	miiane(ctlr->mii, ~0, ~0, ~0);

	return 0;
}


static void
attach(Ether *edev)	//keep it minimal
{
	int i;
	ulong	buf;
	Ctlr *ctlr;
	Desc *d;

	ctlr = edev->ctlr;

	if(Attchbug){
		iprint("ether attach called\n");
		delay(10);
	}

	qlock(ctlr);
	if(ctlr->attached){
		qunlock(ctlr);

		if(Attchbug){
			iprint("ether attach already?\n");
			delay(10);
		}

		return;
	}

	if(waserror()){
		qunlock(ctlr);

		if(Attchbug){
			iprint("ether attach waserror?\n");
			delay(10);
		}

		free(ctlr->rx->d);
		free(ctlr->tx->d);
		nexterror();
	}

	doreset(edev);

	ethreset(edev);

	if(initmii(ctlr) < 0)
		error("mii failed");

	/* Allocate Rx/Tx ring KSEG1, is uncached memmory */
	ctlr->tx->d = (Desc *)KSEG1ADDR(xspanalloc(sizeof(Desc) * Ntd, CACHELINESZ, 0));
	ctlr->rx->d = (Desc *)KSEG1ADDR(xspanalloc(sizeof(Desc) * Nrd, CACHELINESZ, 0));

	if(ctlr->tx->d == nil || ctlr->rx->d == nil)
		error(Enomem);

	/* Allocate Rx blocks, initialize Rx ring. */
	for(i = 0; i < Nrd; i++){
		Block *b = iallocb(Rbsz);
		if(b == nil)
			error("rxblock");
		ctlr->rx->b[i] = b;
		dcflush(b->rp, Rbsz);
		d = &ctlr->rx->d[i];
		d->ptr0 = PADDR(b->rp);
		d->info1 = RXD_LAST0;
	}

	/* Initialize Tx ring */
	for(i = 0; i < Ntd; i++){
		ctlr->tx->b[i] = nil;
		ctlr->tx->d[i].info1 = TXD_LAST0 | TXD_DDONE;
		ctlr->tx->d[i].info2 = TXD_QN(3) | TXD_PN(TXD_PN_GDMA1);
	}

	if(Attchbug){
		iprint("ether attach clear nic\n");
		delay(10);
	}

	/* turn off and clear defaults */
	buf = ethrd(PDMA_GLOBAL_CFG);
	buf &= 0xFF;
	ethwr(PDMA_GLOBAL_CFG, buf); 
	delay(1);

	/* give Tx ring to nic */
	ethwr(PDMA_TX0_PTR, PADDR(ctlr->tx->d));
	ethwr(PDMA_TX0_COUNT, Ntd);
	ethwr(PDMA_TX0_CPU_IDX, 0);
	ethwr(PDMA_IDX_RST, 1 << 0);
	coherence();

	/* give Rx ring to nic */
	ethwr(PDMA_RX0_PTR, PADDR(ctlr->rx->d));
	ethwr(PDMA_RX0_COUNT, Nrd);
	ethwr(PDMA_RX0_CPU_IDX, (Nrd - 1));
	ethwr(PDMA_IDX_RST, 1 << 16);
	coherence();


	/* clear pending irqs */
	buf = ethrd(INT_STATUS);
	ethwr(INT_STATUS, buf);

	/* setup interupts */
	ethwr(INT_MASK,
		INT_RX_DONE_INT1 |
		INT_RX_DONE_INT0 |
		INT_TX_DONE_INT3 |
		INT_TX_DONE_INT2 |
		INT_TX_DONE_INT1 |
		INT_TX_DONE_INT0);

	if(Attchbug){
		iprint("ether attach start\n");
		delay(10);
	}


	/* start dma */
	ethwr(PDMA_GLOBAL_CFG, GLO_CFG_TX_WB_DDONE | GLO_CFG_RX_DMA_EN | GLO_CFG_TX_DMA_EN); 


	if(Attchbug){
		iprint("ether attach vlan\n");
		delay(10);
	}

	/* outer vlan id */
	ethwr(SDM_CON, 0x8100);

	edev->link = 1;
	ctlr->attached = 1;

	if(Attchbug){
		iprint("ether attach kprocs\n");
		delay(10);
	}

	kproc("rxproc", rxproc, edev);
	kproc("txproc", txproc, edev);

	qunlock(ctlr);
	poperror();

	if(Attchbug)
		iprint("ether attach done\n");
}


static void
shutdown(Ether *edev)
{
	USED(edev);
}

/* promiscuous stub */
static void
prom(void*, int)
{
}

/* multicast stub */
static void
multi(void*, uchar*, int)
{
}


static long
ifstat(Ether* edev, void* a, long n, ulong offset)
{
	char* p;
	Ctlr* ctlr;
	int l;

	ctlr = edev->ctlr;

	p = smalloc(READSTR);
	l = 0;
	qlock(ctlr);
	l += snprint(p+l, READSTR-l, "tx: %d\n", ctlr->txstat);
	l += snprint(p+l, READSTR-l, "rx: %d\n", ctlr->rxstat);
	l += snprint(p+l, READSTR-l, "txintr: %d\n", ctlr->txintr);
	l += snprint(p+l, READSTR-l, "rxintr: %d\n", ctlr->rxintr);
	l += snprint(p+l, READSTR-l, "nointr: %d\n", ctlr->nointr);
	l += snprint(p+l, READSTR-l, "bad rx: %d\n", ctlr->badrx);
	l += snprint(p+l, READSTR-l, "\n");
	l += snprint(p+l, READSTR-l, "dma errs: tx: %d rx: %d\n", ctlr->txdmaerr, ctlr->rxdmaerr);
	l += snprint(p+l, READSTR-l, "\n");
	l += snprint(p+l, READSTR-l, "txptr: %08uX\n", ethrd(PDMA_TX0_PTR));
	l += snprint(p+l, READSTR-l, "txcnt: %uX\n", ethrd(PDMA_TX0_COUNT));
	l += snprint(p+l, READSTR-l, "txidx: %uX\n", ethrd(PDMA_TX0_CPU_IDX));
	l += snprint(p+l, READSTR-l, "txdtx: %uX\n", ethrd(PDMA_TX0_DMA_IDX));
	l += snprint(p+l, READSTR-l, "\n");
	l += snprint(p+l, READSTR-l, "rxptr: %08uX\n", ethrd(PDMA_RX0_PTR));
	l += snprint(p+l, READSTR-l, "rxcnt: %uX\n", ethrd(PDMA_RX0_COUNT));
	l += snprint(p+l, READSTR-l, "rxidx: %uX\n", ethrd(PDMA_RX0_CPU_IDX));
	l += snprint(p+l, READSTR-l, "rxdtx: %uX\n", ethrd(PDMA_RX0_DMA_IDX));
	l += snprint(p+l, READSTR-l, "\n");
	l += snprint(p+l, READSTR-l, "GLOBAL CFG: %08uX\n", ethrd(PDMA_GLOBAL_CFG));
	l += snprint(p+l, READSTR-l, "INT STATUS: %08uX\n", ethrd(INT_STATUS));
	l += snprint(p+l, READSTR-l, "INT   MASK: %08uX\n", ethrd(INT_MASK));
	snprint(p+l, READSTR-l, "\n");

	n = readstr(offset, a, n, p);
	free(p);

	qunlock(ctlr);

	return n;
}

/* set Ether and Ctlr */
static int
pnp(Ether *edev)
{
	static Ctlr ctlr[1];

	if(Attchbug)
		iprint("ether pnp called\n");

	if(edev->ctlr != nil)
		return -1;

	/* only one controller */
	if(edev->ctlrno != 0)
		return -1;


	ctlr->edev	=	edev;

	edev->port	= (uintptr)(KSEG1|ETHBASE);
	edev->ctlr	= ctlr;
	edev->irq	= IRQethr;
	edev->mbps	= 100;
	edev->maxmtu = 1536;
	edev->arg	= edev;

	edev->attach = attach;
	edev->shutdown = shutdown;
	edev->ifstat = ifstat;
//	edev->ctl = ctl;
	edev->promiscuous = prom;
	edev->multicast = multi;

	getmacaddr(edev);

	intrenable(edev->irq, etherinterrupt, edev, 0, edev->name);

	if(Attchbug)
		iprint("ether pnp done\n");

	return 0;
}


void
ether7688link(void)
{
	addethercard("ether7688", pnp);
}
