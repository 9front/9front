/*
 * Broadcom BCM57xx
 * Not implemented:
 *  proper fatal error handling
 *  multiple rings
 *  QoS
 *  checksum offloading
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

#define Rbsz		ROUNDUP(sizeof(Etherpkt)+4, 4)

typedef struct Ctlr Ctlr;
struct Ctlr {
	Lock txlock, imlock;
	Ctlr *link;
	uvlong port;
	Pcidev *pdev;
	ulong *nic, *status;
	/* One Ring to find them, One Ring to bring them all and in the darkness bind them */
	ulong *recvret, *recvprod, *sendr;
	ulong recvreti, recvprodi, sendri, sendcleani;
	Block **sends, **recvs;
	int active, duplex;
};

enum {
	RecvRetRingLen = 0x200,
	RecvProdRingLen = 0x200,
	SendRingLen = 0x200,
};

enum {
	Reset = 1<<0,
	Enable = 1<<1,
	Attn = 1<<2,
	
	PowerControlStatus = 0x4C,

	MiscHostCtl = 0x68,
	ClearIntA = 1<<0,
	MaskPCIInt = 1<<1,
	ByteSwap = 1<<2,
	WordSwap = 1<<3,
	EnablePCIStateRegister = 1<<4,
	EnableClockControlRegister = 1<<5,
	IndirectAccessEnable = 1<<7,
	TaggedStatus = 1<<9,
	
	DMARWControl = 0x6C,
	DMAWatermarkMask = ~(7<<19),
	DMAWatermarkValue = 3<<19,

	MemoryWindow = 0x7C,
	MemoryWindowData = 0x84,
	
	SendRCB = 0x100,
	RecvRetRCB = 0x200,
	
	InterruptMailbox = 0x204,
	
	RecvProdBDRingIndex = 0x26c,
	RecvBDRetRingIndex = 0x284,
	SendBDRingHostIndex = 0x304,
	
	MACMode = 0x400,
	MACPortMask = ~((1<<3)|(1<<2)),
	MACPortGMII = 1<<3,
	MACPortMII = 1<<2,
	MACEnable = (1<<23) | (1<<22) | (1<<21) | (1 << 15) | (1 << 14) | (1<<12) | (1<<11),
	MACHalfDuplex = 1<<1,
	
	MACEventStatus = 0x404,
	MACEventEnable = 0x408,
	MACAddress = 0x410,
	EthernetRandomBackoff = 0x438,
	ReceiveMTU = 0x43C,
	MIComm = 0x44C,
	MIStatus = 0x450,
	MIMode = 0x454,
	ReceiveMACMode = 0x468,
	TransmitMACMode = 0x45C,
	TransmitMACLengths = 0x464,
	MACHash = 0x470,
	ReceiveRules = 0x480,
	
	ReceiveRulesConfiguration = 0x500,
	LowWatermarkMaximum = 0x504,
	LowWatermarkMaxMask = ~0xFFFF,
	LowWatermarkMaxValue = 2,

	SendDataInitiatorMode = 0xC00,
	SendInitiatorConfiguration = 0x0C08,
	SendStats = 1<<0,
	SendInitiatorMask = 0x0C0C,
	
	SendDataCompletionMode = 0x1000,
	SendBDSelectorMode = 0x1400,
	SendBDInitiatorMode = 0x1800,
	SendBDCompletionMode = 0x1C00,
	
	ReceiveListPlacementMode = 0x2000,
	ReceiveListPlacement = 0x2010,
	ReceiveListPlacementConfiguration = 0x2014,
	ReceiveStats = 1<<0,
	ReceiveListPlacementMask = 0x2018,
	
	ReceiveDataBDInitiatorMode = 0x2400,
	ReceiveBDHostAddr = 0x2450,
	ReceiveBDFlags = 0x2458,
	ReceiveBDNIC = 0x245C,
	ReceiveDataCompletionMode = 0x2800,
	ReceiveBDInitiatorMode = 0x2C00,
	ReceiveBDRepl = 0x2C18,
	
	ReceiveBDCompletionMode = 0x3000,
	HostCoalescingMode = 0x3C00,
	HostCoalescingRecvTicks = 0x3C08,
	HostCoalescingSendTicks = 0x3C0C,
	RecvMaxCoalescedFrames = 0x3C10,
	SendMaxCoalescedFrames = 0x3C14,
	RecvMaxCoalescedFramesInt = 0x3C20,
	SendMaxCoalescedFramesInt = 0x3C24,
	StatusBlockHostAddr = 0x3C38,
	FlowAttention = 0x3C48,

	MemArbiterMode = 0x4000,
	
	BufferManMode = 0x4400,
	
	MBUFLowWatermark = 0x4414,
	MBUFHighWatermark = 0x4418,
	
	ReadDMAMode = 0x4800,
	ReadDMAStatus = 0x4804,
	WriteDMAMode = 0x4C00,
	WriteDMAStatus = 0x4C04,
	
	RISCState = 0x5004,
	FTQReset = 0x5C00,
	MSIMode = 0x6000,
	
	ModeControl = 0x6800,
	ByteWordSwap = (1<<4)|(1<<5)|(1<<2),//|(1<<1),
	HostStackUp = 1<<16,
	HostSendBDs = 1<<17,
	InterruptOnMAC = 1<<26,
	
	MiscConfiguration = 0x6804,
	CoreClockBlocksReset = 1<<0,
	GPHYPowerDownOverride = 1<<26,
	DisableGRCResetOnPCIE = 1<<29,
	TimerMask = ~0xFF,
	TimerValue = 65<<1,
	MiscLocalControl = 0x6808,
	InterruptOnAttn = 1<<3,
	AutoSEEPROM = 1<<24,
	
	SwArbitration = 0x7020,
	SwArbitSet1 = 1<<1,
	SwArbitWon1 = 1<<9,
	TLPControl = 0x7C00,
	
	PhyControl = 0x00,
	PhyStatus = 0x01,
	PhyLinkStatus = 1<<2,
	PhyAutoNegComplete = 1<<5,
	PhyPartnerStatus = 0x05,
	Phy100FD = 1<<8,
	Phy100HD = 1<<7,
	Phy10FD = 1<<6,
	Phy10HD = 1<<5,
	PhyGbitStatus = 0x0A,
	Phy1000FD = 1<<12,
	Phy1000HD = 1<<11,
	PhyAuxControl = 0x18,
	PhyIntStatus = 0x1A,
	PhyIntMask = 0x1B,
	
	Updated = 1<<0,
	LinkStateChange = 1<<1,
	Error = 1<<2,
	
	PacketEnd = 1<<2,
	FrameError = 1<<10,
};

enum {
	BCM5752 = 0x1600, 
	BCM5752M = 0x1601, 
	BCM5709 = 0x1639, 
	BCM5709S = 0x163a, 
	BCM5716 = 0x163b, 
	BCM5716S = 0x163c, 
	BCM5700 = 0x1644, 
	BCM5701 = 0x1645, 
	BCM5702 = 0x1646, 
	BCM5703 = 0x1647, 
	BCM5704 = 0x1648, 
	BCM5704S_2 = 0x1649, 
	BCM5706 = 0x164a, 
	BCM5708 = 0x164c, 
	BCM5702FE = 0x164d, 
	BCM57710 = 0x164e, 
	BCM57711 = 0x164f, 
	BCM57711E = 0x1650, 
	BCM5705 = 0x1653, 
	BCM5705_2 = 0x1654, 
	BCM5717 = 0x1655, 
	BCM5718 = 0x1656, 
	BCM5720 = 0x1658, 
	BCM5721 = 0x1659, 
	BCM5722 = 0x165a, 
	BCM5723 = 0x165b, 
	BCM5724 = 0x165c, 
	BCM5705M = 0x165d, 
	BCM5705M_2 = 0x165e, 
	BCM5714 = 0x1668, 
	BCM5780 = 0x166a, 
	BCM5780S = 0x166b, 
	BCM5754M = 0x1672, 
	BCM5755M = 0x1673, 
	BCM5756ME = 0x1674, 
	BCM5750 = 0x1676, 
	BCM5751 = 0x1677, 
	BCM5715 = 0x1678, 
	BCM5715S = 0x1679, 
	BCM5754 = 0x167a, 
	BCM5755 = 0x167b, 
	BCM5750M = 0x167c, 
	BCM5751M = 0x167d, 
	BCM5751F = 0x167e, 
	BCM5787F = 0x167f, 
	BCM5761e = 0x1680, 
	BCM5761 = 0x1681, 
	BCM5764M = 0x1684, 
	BCM57760 = 0x1690, 
	BCM57788 = 0x1691, 
	BCM57780 = 0x1692, 
	BCM5787M = 0x1693, 
	BCM57790 = 0x1694, 
	BCM5782 = 0x1696, 
	BCM5784M = 0x1698, 
	BCM5785 = 0x1699, 
	BCM5786 = 0x169a, 
	BCM5787 = 0x169b, 
	BCM5788 = 0x169c, 
	BCM5789 = 0x169d, 
	BCM5785_2 = 0x16a0, 
	BCM5702X = 0x16a6, 
	BCM5703X = 0x16a7, 
	BCM5704S = 0x16a8, 
	BCM5706S = 0x16aa, 
	BCM5708S = 0x16ac, 
	BCM57761 = 0x16b0, 
	BCM57781 = 0x16b1, 
	BCM57791 = 0x16b2, 
	BCM57765 = 0x16b4, 
	BCM57785 = 0x16b5, 
	BCM57795 = 0x16b6, 
	BCM5702A3 = 0x16c6, 
	BCM5703_2 = 0x16c7, 
	BCM5781 = 0x16dd, 
	BCM5753 = 0x16f7, 
	BCM5753M = 0x16fd, 
	BCM5753F = 0x16fe, 
	BCM5906 = 0x1712,
	BCM5906M = 0x1713,
};

#define csr32(c, r)	((c)->nic[(r)/4])
#define mem32(c, r) csr32(c, (r)+0x8000)

static Ctlr *bcmhead, *bcmtail;

static ulong
dummyread(ulong x)
{
	return x;
}

static int
miir(Ctlr *ctlr, int ra)
{
	while(csr32(ctlr, MIComm) & (1<<29));
	csr32(ctlr, MIComm) = (ra << 16) | (1 << 21) | (1 << 27) | (1 << 29);
	while(csr32(ctlr, MIComm) & (1<<29));
	if(csr32(ctlr, MIComm) & (1<<28)) return -1;
	return csr32(ctlr, MIComm) & 0xFFFF;
}

static int
miiw(Ctlr *ctlr, int ra, int value)
{
	while(csr32(ctlr, MIComm) & (1<<29));
	csr32(ctlr, MIComm) = (value & 0xFFFF) | (ra << 16) | (1 << 21) | (1 << 27) | (1 << 29);
	while(csr32(ctlr, MIComm) & (1<<29));
	return 0;
}

static void
checklink(Ether *edev)
{
	Ctlr *ctlr;
	ulong i;

	ctlr = edev->ctlr;
	miir(ctlr, PhyStatus); /* dummy read necessary */
	if(!(miir(ctlr, PhyStatus) & PhyLinkStatus)) {
		edev->link = 0;
		edev->mbps = 1000;
		ctlr->duplex = 1;
		print("bcm: no link\n");
		goto out;
	}
	edev->link = 1;
	while((miir(ctlr, PhyStatus) & PhyAutoNegComplete) == 0);
	i = miir(ctlr, PhyGbitStatus);
	if(i & (Phy1000FD | Phy1000HD)) {
		edev->mbps = 1000;
		ctlr->duplex = (i & Phy1000FD) != 0;
	} else if(i = miir(ctlr, PhyPartnerStatus), i & (Phy100FD | Phy100HD)) {
		edev->mbps = 100;
		ctlr->duplex = (i & Phy100FD) != 0;
	} else if(i & (Phy10FD | Phy10HD)) {
		edev->mbps = 10;
		ctlr->duplex = (i & Phy10FD) != 0;
	} else {
		edev->link = 0;
		edev->mbps = 1000;
		ctlr->duplex = 1;
		print("bcm: link partner supports neither 10/100/1000 Mbps\n"); 
		goto out;
	}
	print("bcm: %d Mbps link, %s duplex\n", edev->mbps, ctlr->duplex ? "full" : "half");
out:
	if(ctlr->duplex) csr32(ctlr, MACMode) &= ~MACHalfDuplex;
	else csr32(ctlr, MACMode) |= MACHalfDuplex;
	if(edev->mbps >= 1000)
		csr32(ctlr, MACMode) = (csr32(ctlr, MACMode) & MACPortMask) | MACPortGMII;
	else
		csr32(ctlr, MACMode) = (csr32(ctlr, MACMode) & MACPortMask) | MACPortMII;
	csr32(ctlr, MACEventStatus) |= (1<<4) | (1<<3); /* undocumented bits (sync and config changed) */
}

static ulong*
currentrecvret(Ctlr *ctlr)
{
	if(ctlr->recvreti == (ctlr->status[4] & 0xFFFF)) return 0;
	return ctlr->recvret + ctlr->recvreti * 8;
}

static void
consumerecvret(Ctlr *ctlr)
{
	csr32(ctlr, RecvBDRetRingIndex) = ctlr->recvreti = (ctlr->recvreti + 1) & (RecvRetRingLen - 1);
}

static int
replenish(Ctlr *ctlr)
{
	ulong *next;
	ulong incr, idx;
	Block *bp;

	idx = ctlr->recvprodi;
	incr = (idx + 1) & (RecvProdRingLen - 1);
	if(incr == (ctlr->status[2] >> 16)) return -1;
	if(ctlr->recvs[idx] != 0) return -1;
	bp = iallocb(Rbsz);
	if(bp == nil) {
		print("bcm: out of memory for receive buffers\n");
		return -1;
	}
	ctlr->recvs[idx] = bp;
	next = ctlr->recvprod + idx * 8;
	memset(next, 0, 32);
	next[1] = PADDR(bp->rp);
	next[2] = Rbsz;
	next[7] = idx;
	coherence();
	csr32(ctlr, RecvProdBDRingIndex) = ctlr->recvprodi = incr;
	return 0;
}

static void
bcmreceive(Ether *edev)
{
	Ctlr *ctlr;
	Block *bp;
	ulong *pkt, len, idx;
	
	ctlr = edev->ctlr;
	for(; pkt = currentrecvret(ctlr); replenish(ctlr), consumerecvret(ctlr)) {
		idx = pkt[7] & (RecvProdRingLen - 1);
		bp = ctlr->recvs[idx];
		if(bp == 0) {
			print("bcm: nil block at %lux -- shouldn't happen\n", idx);
			break;
		}
		ctlr->recvs[idx] = 0;
		len = pkt[2] & 0xFFFF;
		bp->wp = bp->rp + len;
		if((pkt[3] & PacketEnd) == 0) print("bcm: partial frame received -- shouldn't happen\n");
		if(pkt[3] & FrameError) {
			freeb(bp); /* dump erroneous packets */ 
		} else {
			etheriq(edev, bp);
		}
	}
}

static void
bcmtransclean(Ether *edev, int dolock)
{
	Ctlr *ctlr;
	
	ctlr = edev->ctlr;
	if(dolock)
		ilock(&ctlr->txlock);
	while(ctlr->sendcleani != (ctlr->status[4] >> 16)) {
		freeb(ctlr->sends[ctlr->sendcleani]);
		ctlr->sends[ctlr->sendcleani] = 0;
		ctlr->sendcleani = (ctlr->sendcleani + 1) & (SendRingLen - 1);
	}
	if(dolock)
		iunlock(&ctlr->txlock);
}

static void
bcmtransmit(Ether *edev)
{
	Ctlr *ctlr;
	Block *bp;
	ulong *next;
	ulong incr;
	
	ctlr = edev->ctlr;
	ilock(&ctlr->txlock);
	while(1) {
		incr = (ctlr->sendri + 1) & (SendRingLen - 1);
		if(incr == (ctlr->status[4] >> 16)) {
			print("bcm: send queue full\n");
			break;
		}
		bp = qget(edev->oq);
		if(bp == nil) break;
		next = ctlr->sendr + ctlr->sendri * 4;
		next[0] = 0;
		next[1] = PADDR(bp->rp);
		next[2] = (BLEN(bp) << 16) | PacketEnd;
		next[3] = 0;
		if(ctlr->sends[ctlr->sendri] != 0)
			freeb(ctlr->sends[ctlr->sendri]);
		ctlr->sends[ctlr->sendri] = bp;
		coherence();
		csr32(ctlr, SendBDRingHostIndex) = ctlr->sendri = incr;
	}
	iunlock(&ctlr->txlock);
}

static void
bcmerror(Ether *edev)
{
	Ctlr *ctlr;
	
	ctlr = edev->ctlr;
	if(csr32(ctlr, FlowAttention)) {
		if(csr32(ctlr, FlowAttention) & 0xF8FF8080UL) {
			panic("bcm: fatal error %#.8ulx", csr32(ctlr, FlowAttention));
		}
		csr32(ctlr, FlowAttention) = 0;
	}
	csr32(ctlr, MACEventStatus) = 0; /* worth ignoring */
	if(csr32(ctlr, ReadDMAStatus) || csr32(ctlr, WriteDMAStatus)) {
		print("bcm: DMA error\n");
		csr32(ctlr, ReadDMAStatus) = 0;
		csr32(ctlr, WriteDMAStatus) = 0;
	}
	if(csr32(ctlr, RISCState)) {
		if(csr32(ctlr, RISCState) & 0x78000403) {
			panic("bcm: RISC halted %#.8ulx", csr32(ctlr, RISCState));
		}
		csr32(ctlr, RISCState) = 0;
	}
}

static void
bcminterrupt(Ureg*, void *arg)
{
	Ether *edev;
	Ctlr *ctlr;
	ulong status, tag;
	
	edev = arg;
	ctlr = edev->ctlr;
	ilock(&ctlr->imlock);
	dummyread(csr32(ctlr, InterruptMailbox));
	csr32(ctlr, InterruptMailbox) = 1;
	status = ctlr->status[0];
	tag = ctlr->status[1];
	ctlr->status[0] = 0;
	if(status & Error) bcmerror(edev);
	if(status & LinkStateChange) checklink(edev);
//	print("bcm: interrupt %8ulx %8ulx\n", ctlr->status[2], ctlr->status[4]);
	bcmreceive(edev);
	bcmtransclean(edev, 1);
	bcmtransmit(edev);
	csr32(ctlr, InterruptMailbox) = tag << 24;
	iunlock(&ctlr->imlock);
}

static int
bcminit(Ether *edev)
{
	ulong i, j;
	Ctlr *ctlr;
	
	ctlr = edev->ctlr;
	print("bcm: reset\n");
	/* initialization procedure according to the datasheet */
	csr32(ctlr, MiscHostCtl) |= MaskPCIInt | ClearIntA;
	csr32(ctlr, SwArbitration) |= SwArbitSet1;
	for(i = 0; i < 10000 && (csr32(ctlr, SwArbitration) & SwArbitWon1) == 0; i++)
		microdelay(100);
	if(i == 10000){
		iprint("bcm: arbiter failed to respond\n");
		return -1;
	}
	csr32(ctlr, MemArbiterMode) |= Enable;
	csr32(ctlr, MiscHostCtl) |= IndirectAccessEnable | EnablePCIStateRegister | EnableClockControlRegister;
	csr32(ctlr, MiscHostCtl) = (csr32(ctlr, MiscHostCtl) & ~(ByteSwap|WordSwap)) | WordSwap;
	csr32(ctlr, ModeControl) |= ByteWordSwap;
	csr32(ctlr, MemoryWindow) = 0;
	mem32(ctlr, 0xB50) = 0x4B657654; /* magic number bullshit */
	csr32(ctlr, MiscConfiguration) |= GPHYPowerDownOverride | DisableGRCResetOnPCIE;
	csr32(ctlr, MiscConfiguration) |= CoreClockBlocksReset;
	microdelay(100000);
	ctlr->pdev->pcr |= 1<<1; /* pci memory access enable */
	pcisetbme(ctlr->pdev);
	csr32(ctlr, MiscHostCtl) |= MaskPCIInt;
	csr32(ctlr, MemArbiterMode) |= Enable;
	csr32(ctlr, MiscHostCtl) = (csr32(ctlr, MiscHostCtl) & ~(ByteSwap|WordSwap)) | WordSwap;
	csr32(ctlr, MiscHostCtl) |= IndirectAccessEnable | EnablePCIStateRegister | EnableClockControlRegister | TaggedStatus;
	csr32(ctlr, ModeControl) |= ByteWordSwap;
	csr32(ctlr, MACMode) = (csr32(ctlr, MACMode) & MACPortMask) | MACPortGMII;
	microdelay(40000);
	for(i = 0; i < 100000 && mem32(ctlr, 0xB50) != 0xB49A89AB; i++)
		microdelay(100);
	if(i == 100000){
		iprint("bcm: chip failed to reset\n");
		return -1;
	}
	switch(ctlr->pdev->did){
	case BCM5721:
	case BCM5751:
	case BCM5752:
		csr32(ctlr, TLPControl) |= (1<<25) | (1<<29);
		break;
	}
	memset(ctlr->status, 0, 20);
	csr32(ctlr, DMARWControl) = (csr32(ctlr, DMARWControl) & DMAWatermarkMask) | DMAWatermarkValue;
	csr32(ctlr, ModeControl) |= HostSendBDs | HostStackUp | InterruptOnMAC;
	csr32(ctlr, MiscConfiguration) = (csr32(ctlr, MiscConfiguration) & TimerMask) | TimerValue;
	csr32(ctlr, MBUFLowWatermark) = 0x20;
	csr32(ctlr, MBUFHighWatermark) = 0x60;
	csr32(ctlr, LowWatermarkMaximum) = (csr32(ctlr, LowWatermarkMaximum) & LowWatermarkMaxMask) | LowWatermarkMaxValue;
	csr32(ctlr, BufferManMode) |= Enable | Attn;
	for(i = 0; i < 100 && (csr32(ctlr, BufferManMode) & Enable) == 0; i++)
		microdelay(100);
	if(i == 100){
		iprint("bcm: buffer manager failed to start\n");
		return -1;
	}
	csr32(ctlr, FTQReset) = -1;
	csr32(ctlr, FTQReset) = 0;
	for(i = 0; i < 1000 && csr32(ctlr, FTQReset) != 0; i++)
		microdelay(100);
	if(i == 1000){
		iprint("bcm: ftq failed to reset\n");
		return -1;
	}
	csr32(ctlr, ReceiveBDHostAddr) = 0;
	csr32(ctlr, ReceiveBDHostAddr + 4) = PADDR(ctlr->recvprod);
	csr32(ctlr, ReceiveBDFlags) = RecvProdRingLen << 16;
	csr32(ctlr, ReceiveBDNIC) = 0x6000;
	csr32(ctlr, ReceiveBDRepl) = 25;
	csr32(ctlr, SendBDRingHostIndex) = 0;
	csr32(ctlr, SendBDRingHostIndex+4) = 0;
	mem32(ctlr, SendRCB) = 0;
	mem32(ctlr, SendRCB + 4) = PADDR(ctlr->sendr);
	mem32(ctlr, SendRCB + 8) = SendRingLen << 16;
	mem32(ctlr, SendRCB + 12) = 0x4000;
	for(i=1;i<4;i++)
		mem32(ctlr, RecvRetRCB + i * 0x10 + 8) = 2;
	mem32(ctlr, RecvRetRCB) = 0;
	mem32(ctlr, RecvRetRCB + 4) = PADDR(ctlr->recvret);
	mem32(ctlr, RecvRetRCB + 8) = RecvRetRingLen << 16;
	csr32(ctlr, RecvProdBDRingIndex) = 0;
	csr32(ctlr, RecvProdBDRingIndex+4) = 0;
	/* this delay is not in the datasheet, but necessary; Broadcom is fucking with us */
	microdelay(1000); 
	i = csr32(ctlr, 0x410);
	j = edev->ea[0] = i >> 8;
	j += edev->ea[1] = i;
	i = csr32(ctlr, MACAddress + 4);
	j += edev->ea[2] = i >> 24;
	j += edev->ea[3] = i >> 16;
	j += edev->ea[4] = i >> 8;
	j += edev->ea[5] = i;
	csr32(ctlr, EthernetRandomBackoff) = j & 0x3FF;
	csr32(ctlr, ReceiveMTU) = Rbsz;
	csr32(ctlr, TransmitMACLengths) = 0x2620;
	csr32(ctlr, ReceiveListPlacement) = 1<<3; /* one list */
	csr32(ctlr, ReceiveListPlacementMask) = 0xFFFFFF;
	csr32(ctlr, ReceiveListPlacementConfiguration) |= ReceiveStats;
	csr32(ctlr, SendInitiatorMask) = 0xFFFFFF;
	csr32(ctlr, SendInitiatorConfiguration) |= SendStats;
	csr32(ctlr, HostCoalescingMode) = 0;
	for(i = 0; i < 200 && csr32(ctlr, HostCoalescingMode) != 0; i++)
		microdelay(100);
	if(i == 200){
		iprint("bcm: host coalescing engine failed to stop\n");
		return -1;
	}
	csr32(ctlr, HostCoalescingRecvTicks) = 150;
	csr32(ctlr, HostCoalescingSendTicks) = 150;
	csr32(ctlr, RecvMaxCoalescedFrames) = 10;
	csr32(ctlr, SendMaxCoalescedFrames) = 10;
	csr32(ctlr, RecvMaxCoalescedFramesInt) = 0;
	csr32(ctlr, SendMaxCoalescedFramesInt) = 0;
	csr32(ctlr, StatusBlockHostAddr) = 0;
	csr32(ctlr, StatusBlockHostAddr + 4) = PADDR(ctlr->status);
	csr32(ctlr, HostCoalescingMode) |= Enable;
	csr32(ctlr, ReceiveBDCompletionMode) |= Enable | Attn;
	csr32(ctlr, ReceiveListPlacementMode) |= Enable;
	csr32(ctlr, MACMode) |= MACEnable;
	csr32(ctlr, MiscLocalControl) |= InterruptOnAttn | AutoSEEPROM;
	csr32(ctlr, InterruptMailbox) = 0;
	csr32(ctlr, WriteDMAMode) |= 0x200003fe; /* pulled out of my nose */
	csr32(ctlr, ReadDMAMode) |= 0x3fe;
	csr32(ctlr, ReceiveDataCompletionMode) |= Enable | Attn;
	csr32(ctlr, SendDataCompletionMode) |= Enable;
	csr32(ctlr, SendBDCompletionMode) |= Enable | Attn;
	csr32(ctlr, ReceiveBDInitiatorMode) |= Enable | Attn;
	csr32(ctlr, ReceiveDataBDInitiatorMode) |= Enable | (1<<4);
	csr32(ctlr, SendDataInitiatorMode) |= Enable;
	csr32(ctlr, SendBDInitiatorMode) |= Enable | Attn;
	csr32(ctlr, SendBDSelectorMode) |= Enable | Attn;
	ctlr->recvprodi = 0;
	while(replenish(ctlr) >= 0);
	csr32(ctlr, TransmitMACMode) |= Enable;
	csr32(ctlr, ReceiveMACMode) |= Enable;
	csr32(ctlr, PowerControlStatus) &= ~3;
	csr32(ctlr, MIStatus) |= 1<<0;
	csr32(ctlr, MACEventEnable) = 0;
	csr32(ctlr, MACEventStatus) |= (1<<12);
	csr32(ctlr, MIMode) = 0xC0000;
	microdelay(40);
	miiw(ctlr, PhyControl, 1<<15);
	for(i = 0; i < 1000 && miir(ctlr, PhyControl) & (1<<15); i++)
		microdelay(100);
	if(i == 1000){
		iprint("bcm: PHY failed to reset\n");
		return -1;
	}
	miiw(ctlr, PhyAuxControl, 2);
	miir(ctlr, PhyIntStatus);
	miir(ctlr, PhyIntStatus);
	miiw(ctlr, PhyIntMask, ~(1<<1));
	checklink(edev);
	csr32(ctlr, MACEventEnable) |= 1<<12;
	csr32(ctlr, MACHash) = -1;
	csr32(ctlr, MACHash+4) = -1;
	csr32(ctlr, MACHash+8) = -1;
	csr32(ctlr, MACHash+12) = -1;
	for(i = 0; i < 8; i++) csr32(ctlr, ReceiveRules + 8 * i) = 0;
	csr32(ctlr, ReceiveRulesConfiguration) = 1 << 3;
	csr32(ctlr, MSIMode) |= Enable;
	csr32(ctlr, MiscHostCtl) &= ~(MaskPCIInt | ClearIntA);
	return 0;
}

static void
bcmpci(void)
{
	Pcidev *pdev;
	
	pdev = nil;
	while(pdev = pcimatch(pdev, 0, 0)) {
		Ctlr *ctlr;
		void *mem;
		
		if(pdev->ccrb != 2 || pdev->ccru != 0)
			continue;
		if(pdev->vid != 0x14e4)
			continue;
		if(pdev->mem[0].bar & 1)
			continue;

		switch(pdev->did){
		default:
			continue;
		case BCM5752:
		case BCM5752M:
		case BCM5709:
		case BCM5709S:
		case BCM5716:
		case BCM5716S:
		case BCM5700:
		case BCM5701:
		case BCM5702:
		case BCM5703:
		case BCM5704:
		case BCM5704S_2:
		case BCM5706:
		case BCM5708:
		case BCM5702FE:
		case BCM57710:
		case BCM57711:
		case BCM57711E:
		case BCM5705:
		case BCM5705_2:
		case BCM5717:
		case BCM5718:
		case BCM5720:
		case BCM5721:
		case BCM5722:
		case BCM5723:
		case BCM5724:
		case BCM5705M:
		case BCM5705M_2:
		case BCM5714:
		case BCM5780:
		case BCM5780S:
		case BCM5754M:
		case BCM5755M:
		case BCM5756ME:
		case BCM5750:
		case BCM5751:
		case BCM5715:
		case BCM5715S:
		case BCM5754:
		case BCM5755:
		case BCM5750M:
		case BCM5751M:
		case BCM5751F:
		case BCM5787F:
		case BCM5761e:
		case BCM5761:
		case BCM5764M:
		case BCM57760:
		case BCM57788:
		case BCM57780:
		case BCM5787M:
		case BCM57790:
		case BCM5782:
		case BCM5784M:
		case BCM5785:
		case BCM5786:
		case BCM5787:
		case BCM5788:
		case BCM5789:
		case BCM5785_2:
		case BCM5702X:
		case BCM5703X:
		case BCM5704S:
		case BCM5706S:
		case BCM5708S:
		case BCM57761:
		case BCM57781:
		case BCM57791:
		case BCM57765:
		case BCM57785:
		case BCM57795:
		case BCM5702A3:
		case BCM5703_2:
		case BCM5781:
		case BCM5753:
		case BCM5753M:
		case BCM5753F:
		case BCM5906:	/* ??? */
		case BCM5906M:	/* ??? */
		case 0x1670: 	/* ??? */
			break;
		}
		ctlr = malloc(sizeof(Ctlr));
		if(ctlr == nil) {
			print("bcm: unable to alloc Ctlr\n");
			continue;
		}
		ctlr->sends = malloc(sizeof(ctlr->sends[0]) * SendRingLen);
		ctlr->recvs = malloc(sizeof(ctlr->recvs[0]) * RecvProdRingLen);
		if(ctlr->sends == nil || ctlr->recvs == nil){
			print("bcm: unable to alloc ctlr->sends and ctlr->recvs\n");
			free(ctlr->sends);
			free(ctlr->recvs);
			free(ctlr);
			continue;
		}
		ctlr->port = pdev->mem[0].bar & ~0xF;
		mem = vmap(ctlr->port, pdev->mem[0].size);
		if(mem == nil) {
			print("bcm: can't map %llux\n", ctlr->port);
			free(ctlr->sends);
			free(ctlr->recvs);
			free(ctlr);
			continue;
		}
		ctlr->pdev = pdev;
		ctlr->nic = mem;
		ctlr->status = xspanalloc(20, 16, 0);
		ctlr->recvprod = xspanalloc(32 * RecvProdRingLen, 16, 0);
		ctlr->recvret = xspanalloc(32 * RecvRetRingLen, 16, 0);
		ctlr->sendr = xspanalloc(16 * SendRingLen, 16, 0);
		if(bcmhead != nil)
			bcmtail->link = ctlr;
		else
			bcmhead = ctlr;
		bcmtail = ctlr;
	}
}

static void
bcmpromiscuous(void* arg, int on)
{
	Ctlr *ctlr;
	
	ctlr = ((Ether*)arg)->ctlr;
	if(on)
		csr32(ctlr, ReceiveMACMode) |= 1<<8;
	else
		csr32(ctlr, ReceiveMACMode) &= ~(1<<8);
}

static void
bcmmulticast(void*, uchar*, int)
{
}

static int
bcmpnp(Ether* edev)
{
	Ctlr *ctlr;
	
again:
	if(bcmhead == nil)
		bcmpci();
	
	for(ctlr = bcmhead; ctlr != nil; ctlr = ctlr->link) {
		if(ctlr->active)
			continue;
		
		if(edev->port == 0 || edev->port == ctlr->port) {
			ctlr->active = 1;
			break;
		}
	}
	
	if(ctlr == nil)
		return -1;

	pcienable(ctlr->pdev);
	pcisetbme(ctlr->pdev);

	edev->ctlr = ctlr;
	edev->port = ctlr->port;
	edev->irq = ctlr->pdev->intl;
	edev->tbdf = ctlr->pdev->tbdf;
	edev->transmit = bcmtransmit;
	edev->multicast = bcmmulticast;
	edev->promiscuous = bcmpromiscuous;
	edev->arg = edev;
	edev->mbps = 1000;
	
	if(bcminit(edev) < 0){
		edev->ctlr = nil;
		goto again;
	}

	intrenable(edev->irq, bcminterrupt, edev, edev->tbdf, edev->name);

	return 0;
}

void
etherbcmlink(void)
{
	addethercard("bcm", bcmpnp);
}
