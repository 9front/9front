#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/netif.h"
#include "../port/etherif.h"
#include "../port/ethermii.h"

enum {
	Moduleclk	= 125000000,	/* 125Mhz */
	Maxtu		= 1518,

	R_BUF_SIZE	= ((Maxtu+BLOCKALIGN-1)&~BLOCKALIGN),
};

enum {
	ENET_EIR	= 0x004/4,	/* Interrupt Event Register */
	ENET_EIMR	= 0x008/4,	/* Interrupt Mask Register */
		INT_BABR	=1<<30,	/* Babbling Receive Error */
		INT_BABT	=1<<31,	/* Babbling Transmit Error */
		INT_GRA		=1<<28,	/* Graceful Stop Complete */
		INT_TXF		=1<<27,	/* Transmit Frame Interrupt */
		INT_TXB		=1<<26,	/* Transmit Buffer Interrupt */
		INT_RXF		=1<<25,	/* Receive Frame Interrupt */
		INT_RXB		=1<<24,	/* Receive Buffer Interrupt */
		INT_MII		=1<<23,	/* MII Interrupt */
		INT_EBERR	=1<<22,	/* Ethernet Bus Error */
		INT_LC		=1<<21,	/* Late Collision */
		INT_RL		=1<<20,	/* Collision Retry Limit */
		INT_UN		=1<<19,	/* Transmit FIFO Underrun */
		INT_PLR		=1<<18,	/* Payload Receive Error */
		INT_WAKEUP	=1<<17,	/* Node Wakeup Request Indication */
		INT_TS_AVAIL	=1<<16,	/* Transmit Timestamp Available */
		INT_TS_TIMER	=1<<15,	/* Timestamp Timer */
		INT_RXFLUSH_2	=1<<14,	/* RX DMA Ring 2 flush indication */
		INT_RXFLUSH_1	=1<<13,	/* RX DMA Ring 1 flush indication */
		INT_RXFLUSH_0	=1<<12,	/* RX DMA Ring 0 flush indication */
		INT_TXF2	=1<<7,	/* Transmit frame interrupt, class 2 */
		INT_TXB2	=1<<6,	/* Transmit buffer interrupt, class 2 */
		INT_RXF2	=1<<5,	/* Receive frame interrupt, class 2 */
		INT_RXB2	=1<<4,	/* Receive buffer interrupt, class 2 */
		INT_TXF1	=1<<3,	/* Transmit frame interrupt, class 1 */
		INT_TXB1	=1<<2,	/* Transmit buffer interrupt, class 1 */
		INT_RXF1	=1<<1,	/* Receive frame interrupt, class 1 */
		INT_RXB1	=1<<0,	/* Receive buffer interrupt, class 1 */

	ENET_RDAR	= 0x010/4,	/* Receive Descriptor Active Register */
		RDAR_ACTIVE	=1<<24,	/* Descriptor Active */
	ENET_TDAR	= 0x014/4,	/* Transmit Descriptor Active Register */
		TDAR_ACTIVE	=1<<24,	/* Descriptor Active */

	ENET_ECR	= 0x024/4,	/* Ethernet Control Register */
		ECR_RESERVED	=7<<28,
		ECR_SVLANDBL	=1<<11,	/* S-VLAN double tag */
		ECR_VLANUSE2ND	=1<<10,	/* VLAN use second tag */
		ECR_SVLANEN	=1<<9,	/* S-VLAN enable */
		ECR_DBSWP	=1<<8,	/* Descriptor Byte Swapping Enable */
		ECR_DBGEN	=1<<6,	/* Debug Enable */
		ECR_SPEED_100M	=0<<5,
		ECR_SPEED_1000M	=1<<5,
		ECR_EN1588	=1<<4,	/* Enables enhanced functionality of the MAC */
		ECR_SLEEP	=1<<3,	/* Sleep Mode Enable */
		ECR_MAGICEN	=1<<2,	/* Magic Packet Detection Enable */
		ECR_ETHEREN	=1<<1,	/* Ethernet Enable */
		ECR_RESET	=1<<0,	/* Ethernet MAC Reset */

	ENET_MMFR	= 0x040/4,	/* MII Management Frame Register */
		MMFR_ST		=1<<30,
		MMFR_RD		=2<<28,
		MMFR_WR		=1<<28,
		MMFR_PA_SHIFT	=23,
		MMFR_TA		=2<<16,
		MMFR_RA_SHIFT	=18,

	ENET_MSCR	= 0x044/4,	/* MII Speed Control Register */
		MSCR_SPEED_SHIFT=1,	/* MII speed = module_clock/((SPEED+1)*2) */
		MSCR_DIS_PRE	=1<<7,	/* disable preamble */
		MSCR_HOLD_SHIFT	=8,	/* hold cycles in module_clock */

	ENET_MIBC	= 0x064/4,	/* MIB Control Register */
	ENET_RCR	= 0x084/4,	/* Receive Control Register */
		RCR_GRS		=1<<31,	/* Gracefull Receive Stopped */
		RCR_NLC		=1<<30,	/* Payload Length Check Disable */
		RCR_MAX_FL_SHIFT=16,	/* Maximum Frame Length */
		RCR_CFEN	=1<<15,	/* MAC Control Frame Enable */
		RCR_CRCFWD	=1<<14,	/* Forward Received CRC */
		RCR_PAUFWD	=1<<13,	/* Forward Pause Frames */
		RCR_PADEN	=1<<12,	/* Enable Frame Padding Remove */
		RCR_RMII_10T	=1<<9,	/* Enables 10-Mbit/s mode of the RMII/RGMII */
		RCR_RMII_MODE	=1<<8,	/* RMII Mode Enable */
		RCR_RGMII_EN	=1<<6,	/* RGMII Mode Enable */
		RCR_FCE		=1<<5,	/* Flow Control Enable */
		RCR_REJ		=1<<4,	/* Broadcast Frame Reject */
		RCR_PROM	=1<<3,	/* Promiscuous Mode */
		RCR_MII_MODE	=1<<2,	/* Media Independent Interface Mode (must always be set) */
		RCR_DRT		=1<<1,	/* Disable Receive On Timeout */
		RCR_LOOP	=1<<0,	/* Internal Loopback */

	ENET_TCR	= 0x0C4/4,	/* Transmit Control Register */
		TCR_CRCFWD	=1<<9,	/* Foward Frame From Application With CRC */
		TCR_ADDINS	=1<<8,	/* Set MAC Address on Transmit */
		TCR_RFC_PAUSE	=1<<4,	/* Receive Frame Control Pause */
		TCR_TFC_PAUSE	=1<<3,	/* Transmit Frame Control Pause */
		TCR_FDEN	=1<<2,	/* Full-Duplex Enable */
		TCR_GTS		=1<<0,	/* Graceful Transmit Stop */

	ENET_PALR	= 0x0E4/4,	/* Physical Address Lower Register */
	ENET_PAUR	= 0x0E8/4,	/* Physical Address Upper Register */

	ENET_OPD	= 0x0EC/4,	/* Opcode/Pause Duration Register */

	ENET_TXIC0	= 0x0F0/4,	/* Transmit Interrupt Coalescing Register */
	ENET_TXIC1	= 0x0F4/4,	/* Transmit Interrupt Coalescing Register */
	ENET_TXIC2	= 0x0F8/4,	/* Transmit Interrupt Coalescing Register */
	ENET_RXIC0	= 0x100/4,	/* Receive Interrupt Coalescing Register */
	ENET_RXIC1	= 0x104/4,	/* Receive Interrupt Coalescing Register */
	ENET_RXIC2	= 0x108/4,	/* Receive Interrupt Coalescing Register */
		IC_EN		= 1<<31,
		IC_CS		= 1<<30,
		IC_FT_SHIFT	= 20,
		IC_TT_SHIFT	= 0,

	ENET_IAUR	= 0x118/4,	/* Descriptor Individual Upper Address Register */
	ENET_IALR	= 0x11C/4,	/* Descriptor Individual Lower Address Register */
	ENET_GAUR	= 0x120/4,	/* Descriptor Group Upper Address Register */
	ENET_GALR	= 0x124/4,	/* Descriptor Group Lower Address Register */
	ENET_TFWR	= 0x144/4,	/* Transmit FIFO Watermark Register */
		TFWR_STRFWD	= 1<<8,

	ENET_RDSR1	= 0x160/4,	/* Receive Descriptor Ring 1 Start Register */
	ENET_TDSR1	= 0x164/4,	/* Transmit Buffer Descriptor Ring 1 Start Register */
	ENET_MRBR1	= 0x168/4,	/* Maximum Receive Buffer Size Register Ring 1 */

	ENET_RDSR2	= 0x16C/4,	/* Receive Descriptor Ring 2 Start Register */
	ENET_TDSR2	= 0x170/4,	/* Transmit Buffer Descriptor Ring 2 Start Register */
	ENET_MRBR2	= 0x174/4,	/* Maximum Receive Buffer Size Register Ring 2 */

	ENET_RDSR	= 0x180/4,	/* Receive Descriptor Ring 0 Start Register */
	ENET_TDSR	= 0x184/4,	/* Transmit Buffer Descriptor Ring 0 Start Register */
	ENET_MRBR	= 0x188/4,	/* Maximum Receive Buffer Size Register Ring 0 */

	ENET_RSFL	= 0x190/4,	/* Receive FIFO Section Full Threshold */
	ENET_RSEM	= 0x194/4,	/* Receive FIFO Section Empty Threshold */
	ENET_RAEM	= 0x198/4,	/* Receive FIFO Almost Empty Threshold */
	ENET_RAFL	= 0x19C/4,	/* Receive FIFO Almost Full Threshold */

	ENET_TSEM	= 0x1A0/4,	/* Transmit FIFO Section Empty Threshold */
	ENET_TAEM	= 0x1A4/4,	/* Transmit FIFO Almost Empty Threshold */
	ENET_TAFL	= 0x1A8/4,	/* Transmit FIFO Almost Full Threshold */

	ENET_TIPG	= 0x1AC/4,	/* Transmit Inter-Packet Gap */
	ENET_FTRL	= 0x1B0/4,	/* Frame Truncation Length */
	ENET_TACC	= 0x1C0/4,	/* Transmit Accelerator Function Configuration */
	ENET_RACC	= 0x1C4/4,	/* Receive Accelerator Function Configuration */

	ENET_RCMR1	= 0x1C8/4,	/* Receive Classification Match Register */
	ENET_RCMR2	= 0x1CC/4,	/* Receive Classification Match Register */

	ENET_DMA1CFG	= 0x1D8/4,	/* DMA Class Based Configuration */
	ENET_DMA2CFG	= 0x1DC/4,	/* DMA Class Based Configuration */

	ENET_RDAR1	= 0x1E0/4,	/* Receive Descriptor Active Register - Ring 1 */
	ENET_TDAR1	= 0x1E4/4,	/* Transmit Descriptor Active Register - Ring 1 */
	ENET_RDAR2	= 0x1E8/4,	/* Receive Descriptor Active Register - Ring 2 */
	ENET_TDAR2	= 0x1EC/4,	/* Transmit Descriptor Active Register - Ring 2 */

	ENET_QOS	= 0x1F0/4,	/* QOS Scheme */
};

enum {
	/* transmit descriptor status bits */
	TD_R		= 1<<(15+16),	/* Ready */
	TD_OWN		= 1<<(14+16),	/* Ownership */
	TD_W		= 1<<(13+16),	/* Wrap */
	TD_L		= 1<<(11+16),	/* Last in a frame */

	TD_TC		= 1<<(10+16),	/* Transmit CRC */
	TD_ERR		= TD_TC,

	TD_LEN		= 0xFFFF,

	/* receive desctriptor status bits */
	RD_E		= 1<<(15+16),	/* Empty */
	RD_W		= 1<<(13+16),	/* Wrap */
	RD_L		= 1<<(11+16),	/* Last in a frame */

	RD_M		= 1<<(8+16),	/* Miss */
	RD_BC		= 1<<(7+16),	/* broadcast */
	RD_MC		= 1<<(6+16),	/* multicast */

	RD_LG		= 1<<(5+16),	/* length violation */
	RD_NO		= 1<<(4+16),	/* non octet aligned frame */
	RD_CR		= 1<<(2+16),	/* crc error */
	RD_OV		= 1<<(1+16),	/* overrun */
	RD_TR		= 1<<(0+16),	/* truncated */
	RD_ERR		= RD_LG | RD_NO | RD_CR | RD_OV | RD_TR,

	RD_LEN		= 0xFFFF,
};

typedef struct Descr Descr;
struct Descr
{
	u32int	status;
	u32int	addr;
};

typedef struct Ctlr Ctlr;
struct Ctlr
{
	u32int	*regs;
	u32int	intmask;

	struct {
		Block	*b[256];
		Descr	*d;
		Rendez;
	}	rx[1];

	struct {
		Block	*b[256];
		Descr	*d;
		Rendez;
	}	tx[1];

	struct {
		Rendez;
	}	free[1];

	struct {
		Mii;
		Rendez;
	}	mii[1];

	int	attached;
	QLock;
};

#define rr(c, r)	((c)->regs[r])
#define wr(c, r, v)	((c)->regs[r] = (v))

static int
mdiodone(void *arg)
{
	Ctlr *ctlr = arg;
	return rr(ctlr, ENET_EIR) & INT_MII;
}
static int
mdiowait(Ctlr *ctlr)
{
	int i;

	for(i = 0; i < 200; i++){
		tsleep(ctlr->mii, mdiodone, ctlr, 5);
		if(mdiodone(ctlr))
			return 0;
	}
	return -1;
}
static int
mdiow(Mii* mii, int phy, int addr, int data)
{
	Ctlr *ctlr = mii->ctlr;

	data &= 0xFFFF;
	wr(ctlr, ENET_EIR, INT_MII);
	wr(ctlr, ENET_MMFR, MMFR_WR | MMFR_ST | MMFR_TA | phy<<MMFR_PA_SHIFT | addr<<MMFR_RA_SHIFT | data);
	if(mdiowait(ctlr) < 0) return -1;
	return data;
}
static int
mdior(Mii* mii, int phy, int addr)
{
	Ctlr *ctlr = mii->ctlr;

	wr(ctlr, ENET_EIR, INT_MII);
	wr(ctlr, ENET_MMFR, MMFR_RD | MMFR_ST | MMFR_TA | phy<<MMFR_PA_SHIFT | addr<<MMFR_RA_SHIFT);
	if(mdiowait(ctlr) < 0) return -1;
	return rr(ctlr, ENET_MMFR) & 0xFFFF;
}

static void
interrupt(Ureg*, void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	u32int e;

	e = rr(ctlr, ENET_EIR);
	wr(ctlr, ENET_EIR, e);

	if(e & INT_RXF) wakeup(ctlr->rx);
	if(e & INT_TXF) wakeup(ctlr->tx);
	if(e & INT_MII) wakeup(ctlr->mii);
}

static void
shutdown(Ether *edev)
{
	Ctlr *ctlr = edev->ctlr;
	coherence();

	wr(ctlr, ENET_ECR, ECR_RESERVED | ECR_RESET);
	while(rr(ctlr, ENET_ECR) & ECR_RESET) delay(1);

	/* mask and clear interrupt events */
	wr(ctlr, ENET_EIMR, 0);
	wr(ctlr, ENET_EIR, ~0);
}

static int
tdfree(void *arg)
{
	Descr *d = arg;
	return (d->status & (TD_OWN|TD_R)) == 0;
}

static void
txproc(void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	Block *b;
	Descr *d;
	uint i = 0;

	while(waserror())
		;

	for(;;){
		if((b = qbread(edev->oq, 100000)) == nil)
			break;

		d = &ctlr->tx->d[i];
		while(!tdfree(d))
			sleep(ctlr->free, tdfree, d);

		ctlr->tx->b[i] = b;

		dmaflush(1, b->rp, BLEN(b));
		d->addr = PADDR(b->rp);
		coherence();
		if(i == nelem(ctlr->tx->b)-1){
			d->status = BLEN(b) | TD_OWN | TD_R | TD_L | TD_TC | TD_W;
			i = 0;
		} else {
			d->status = BLEN(b) | TD_OWN | TD_R | TD_L | TD_TC;
			i++;
		}
		wr(ctlr, ENET_TDAR, TDAR_ACTIVE);
	}
}

static int
tddone(void *arg)
{
	Descr *d = arg;
	return (d->status & (TD_OWN|TD_R)) == TD_OWN;
}

static void
frproc(void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	Block *b;
	Descr *d;
	uint i = 0;

	while(waserror())
		;

	for(;;){
		d = &ctlr->tx->d[i];
		while(!tddone(d))
			sleep(ctlr->tx, tddone, d);

		b = ctlr->tx->b[i];
		ctlr->tx->b[i] = nil;
		coherence();

		if(i == nelem(ctlr->tx->b)-1){
			d->status = TD_W;
			i = 0;
		} else {
			d->status = 0;
			i++;
		}

		wakeup(ctlr->free);
		freeb(b);
	}
}

static int
rdfull(void *arg)
{
	Descr *d = arg;
	return (d->status & RD_E) == 0;
}

static void
rxproc(void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	Block *b;
	Descr *d;
	uint s, i = 0;

	while(waserror())
		;

	for(;;){
		d = &ctlr->rx->d[i];
		s = d->status;
		if(s & RD_E){
			sleep(ctlr->rx, rdfull, d);
			continue;
		}
		if(((s^RD_L) & (RD_L|RD_ERR)) == 0){
			b = ctlr->rx->b[i];
			b->wp = b->rp + (s & RD_LEN);
			dmaflush(0, b->rp, BLEN(b));
			etheriq(edev, b);

			/* replenish */
			b = allocb(R_BUF_SIZE);
			ctlr->rx->b[i] = b;
			dmaflush(1, b->rp, R_BUF_SIZE);
			d->addr = PADDR(b->rp); 
			coherence();
		}
		if(i == nelem(ctlr->rx->b)-1) {
			d->status = RD_E | RD_W;
			i = 0;
		} else {
			d->status = RD_E;
			i++;
		}
		wr(ctlr, ENET_RDAR, RDAR_ACTIVE);
	}
}

static void
linkproc(void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	MiiPhy *phy;
	int link = -1;

	while(waserror())
		;

	miiane(ctlr->mii, ~0, AnaAP|AnaP, ~0);

	for(;;){
		miistatus(ctlr->mii);
		phy = ctlr->mii->curphy;
		if(phy->link == link){
			tsleep(ctlr->mii, return0, nil, 5000);
			continue;
		}
		link = phy->link;
		if(link){
			u32int ecr = rr(ctlr, ENET_ECR) & ~ECR_SPEED_1000M;
			u32int rcr = rr(ctlr, ENET_RCR) & ~(RCR_RMII_10T|RCR_FCE);
			u32int tcr = rr(ctlr, ENET_TCR) & ~(TCR_RFC_PAUSE|TCR_TFC_PAUSE|TCR_FDEN);

			switch(phy->speed){
			case 1000:
				ecr |= ECR_SPEED_1000M;
				rcr |= RCR_FCE;

				/* receive fifo thresholds */
				wr(ctlr, ENET_RSFL, 16);
				wr(ctlr, ENET_RSEM, 132);
				wr(ctlr, ENET_RAEM, 8);
				wr(ctlr, ENET_RAFL, 8);

				/* opcode/pause duration */
				wr(ctlr, ENET_OPD, 0xFFF0);
				break;
			case 100:
				ecr |= ECR_SPEED_100M;
				break;
			case 10:
				rcr |= RCR_RMII_10T;
				break;
			}
			if(phy->fd)
				tcr |= TCR_FDEN;
			if(phy->rfc)
				tcr |= TCR_RFC_PAUSE;
			if(phy->tfc)
				tcr |= TCR_TFC_PAUSE;

			wr(ctlr, ENET_ECR, ecr);
			wr(ctlr, ENET_RCR, rcr);
			wr(ctlr, ENET_TCR, tcr);

			edev->mbps = phy->speed;

			wr(ctlr, ENET_RDAR, RDAR_ACTIVE);
		}
		edev->link = link;
		print("#l%d: link %d speed %d\n", edev->ctlrno, edev->link, edev->mbps);
	}
}

static void
attach(Ether *edev)
{
	Ctlr *ctlr = edev->ctlr;
	Descr *d;
	int i;

	eqlock(ctlr);
	if(ctlr->attached){
		qunlock(ctlr);
		return;
	}
	if(waserror()){
		qunlock(ctlr);
		nexterror();
	}

	/* RGMII mode, max frame length */
	wr(ctlr, ENET_RCR, RCR_MII_MODE | RCR_RGMII_EN | Maxtu<<RCR_MAX_FL_SHIFT);

	/* set MII clock to 2.5Mhz, 10ns hold time */
	wr(ctlr, ENET_MSCR, ((Moduleclk/(2*2500000))-1)<<MSCR_SPEED_SHIFT | ((Moduleclk/10000000)-1)<<MSCR_HOLD_SHIFT);

	ctlr->intmask |= INT_MII;
	wr(ctlr, ENET_EIMR, ctlr->intmask);
	mii(ctlr->mii, ~0);

	if(ctlr->mii->curphy == nil)
		error("no phy");

	print("#l%d: phy%d id %.8ux oui %x\n", 
		edev->ctlrno, ctlr->mii->curphy->phyno, 
		ctlr->mii->curphy->id, ctlr->mii->curphy->oui);

	/* clear mac filter hash table */
	wr(ctlr, ENET_IALR, 0);
	wr(ctlr, ENET_IAUR, 0);
	wr(ctlr, ENET_GALR, 0);
	wr(ctlr, ENET_GAUR, 0);

	/* set MAC address */
	wr(ctlr, ENET_PALR, (u32int)edev->ea[0]<<24 | (u32int)edev->ea[1]<<16 | (u32int)edev->ea[2]<<8 | edev->ea[3]<<0);
	wr(ctlr, ENET_PAUR, (u32int)edev->ea[4]<<24 | (u32int)edev->ea[5]<<16);

	if(ctlr->rx->d == nil)
		ctlr->rx->d = ucalloc(sizeof(Descr) * nelem(ctlr->rx->b));
	for(i=0; i<nelem(ctlr->rx->b); i++){
		Block *b = allocb(R_BUF_SIZE);
		ctlr->rx->b[i] = b;
		d = &ctlr->rx->d[i];
		dmaflush(1, b->rp, R_BUF_SIZE);
		d->addr = PADDR(b->rp);
		d->status = RD_E;
	}
	ctlr->rx->d[nelem(ctlr->rx->b)-1].status = RD_E | RD_W;
	wr(ctlr, ENET_MRBR, R_BUF_SIZE);
	coherence();
	wr(ctlr, ENET_RDSR, PADDR(ctlr->rx->d));

	if(ctlr->tx->d == nil)
		ctlr->tx->d = ucalloc(sizeof(Descr) * nelem(ctlr->tx->b));
	for(i=0; i<nelem(ctlr->tx->b); i++){
		ctlr->tx->b[i] = nil;
		d = &ctlr->tx->d[i];
		d->addr = 0;
		d->status = 0;
	}
	ctlr->tx->d[nelem(ctlr->tx->b)-1].status = TD_W;
	coherence();
	wr(ctlr, ENET_TDSR, PADDR(ctlr->tx->d));

	/* store and forward tx fifo */
	wr(ctlr, ENET_TFWR, TFWR_STRFWD);

	/* interrupt coalescing: 200 pkts, 1000 µs */
	wr(ctlr, ENET_RXIC0, IC_EN | 200<<IC_FT_SHIFT | ((1000*Moduleclk)/64000000)<<IC_TT_SHIFT);
	wr(ctlr, ENET_TXIC0, IC_EN | 200<<IC_FT_SHIFT | ((1000*Moduleclk)/64000000)<<IC_TT_SHIFT);

	ctlr->intmask |= INT_TXF | INT_RXF;
	wr(ctlr, ENET_EIMR, ctlr->intmask);

	/* enable ethernet */
	wr(ctlr, ENET_ECR, rr(ctlr, ENET_ECR) | ECR_ETHEREN | ECR_DBSWP);

	ctlr->attached = 1;

	kproc("ether-rx", rxproc, edev);
	kproc("ether-tx", txproc, edev);
	kproc("ether-fr", frproc, edev);

	kproc("ether-link", linkproc, edev);

	qunlock(ctlr);
	poperror();
}

static void
prom(void *arg, int on)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;

	if(on)
		wr(ctlr, ENET_RCR, rr(ctlr, ENET_RCR) | RCR_PROM);
	else
		wr(ctlr, ENET_RCR, rr(ctlr, ENET_RCR) & ~RCR_PROM);
}

static void
multi(void *arg, uchar*, int)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	Netaddr *a;
	u64int hash;

	hash = 0;
	for(a = edev->maddr; a != nil; a = a->next)
		hash |= 1ULL << ((ethercrc(a->addr, edev->alen) >> (32 - 6)) & 0x3F);

	wr(ctlr, ENET_GALR, hash & 0xFFFFFFFF);
	wr(ctlr, ENET_GAUR, hash >> 32);
}

static long
ctl(Ether*, void*, long len)
{
	return len;
}

static int
reset(Ether *edev)
{
	Ctlr *ctlr = edev->ctlr;
	u32int paddr1, paddr2;

	/* steal mac address from uboot */
	paddr1 = rr(ctlr, ENET_PALR);
	paddr2 = rr(ctlr, ENET_PAUR);
	edev->ea[0] = paddr1>>24;
	edev->ea[1] = paddr1>>16;
	edev->ea[2] = paddr1>>8;
	edev->ea[3] = paddr1>>0;
	edev->ea[4] = paddr2>>24;
	edev->ea[5] = paddr2>>16;

	shutdown(edev);

	return 0;
}

static int
pnp(Ether *edev)
{
	static Ctlr ctlr[1];

	if(ctlr->regs != nil)
		return -1;

	ctlr->regs = (u32int*)(VIRTIO + 0xbe0000);

	ctlr->mii->ctlr = ctlr;
	ctlr->mii->mir = mdior;
	ctlr->mii->miw = mdiow;

	edev->port = (uintptr)ctlr->regs - KZERO;
	edev->irq = IRQenet1;
	edev->ctlr = ctlr;
	edev->attach = attach;
	edev->shutdown = shutdown;
	edev->promiscuous = prom;
	edev->multicast = multi;
	edev->ctl = ctl;
	edev->arg = edev;
	edev->mbps = 1000;
	edev->maxmtu = Maxtu;

	setclkgate("enet1.ipp_ind_mac0_txclk", 0);
	setclkgate("sim_enet.mainclk", 0);

	setclkrate("enet1.ipg_clk", "system_pll1_div3", 266*Mhz);
	setclkrate("enet1.ipp_ind_mac0_txclk", "system_pll2_div8", Moduleclk);
	setclkrate("enet1.ipg_clk_time", "system_pll2_div10", 25*Mhz);

	setclkgate("enet1.ipp_ind_mac0_txclk", 1);
	setclkgate("sim_enet.mainclk", 1);

	if(reset(edev) < 0)
		return -1;

	intrenable(edev->irq+0, interrupt, edev, BUSUNKNOWN, edev->name);
	intrenable(edev->irq+1, interrupt, edev, BUSUNKNOWN, edev->name);
	intrenable(edev->irq+2, interrupt, edev, BUSUNKNOWN, edev->name);
	intrenable(edev->irq+3, interrupt, edev, BUSUNKNOWN, edev->name);

	return 0;
}

void
etherimxlink(void)
{
	addethercard("imx", pnp);
}
