/*
 * Intel WiFi Link driver.
 *
 * Written without any documentation but Damien Bergaminis
 * OpenBSD iwn(4) and iwm(4) driver sources. Requires intel
 * firmware to be present in /lib/firmware/iwn-* on attach.
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "../port/error.h"
#include "../port/netif.h"
#include "../port/etherif.h"
#include "../port/wifi.h"

enum {
	MaxQueue	= 24*1024,	/* total buffer is 2*MaxQueue: 48k at 22Mbit ≅ 20ms */

	Ntxlog		= 8,
	Ntx		= 1<<Ntxlog,
	Ntxqmax		= MaxQueue/1500,

	Nrxlog		= 8,
	Nrx		= 1<<Nrxlog,

	Rstatsize	= 16,
	Rbufsize	= 4*1024,
	Rdscsize	= 8,

	Tbufsize	= 4*1024,
	Tdscsize	= 128,
	Tcmdsize	= 140,

	FWPageshift	= 12,
	FWPagesize	= 1<<FWPageshift,
	FWBlockshift	= 3,
	FWBlockpages	= 1<<FWBlockshift,
	FWBlocksize	= 1<<(FWBlockshift + FWPageshift),
};

/* registers */
enum {
	Cfg		= 0x000,	/* config register */
		CfgMacDashShift	= 0,
		CfgMacDashMask	= 3<<CfgMacDashShift,
		CfgMacStepShift	= 2,
		CfgMacStepMask	= 3<<CfgMacStepShift,

		MacSi		= 1<<8,
		RadioSi		= 1<<9,

		CfgPhyTypeShift	= 10,
		CfgPhyTypeMask	= 3<<CfgPhyTypeShift,
		CfgPhyDashShift	= 12,
		CfgPhyDashMask	= 3<<CfgPhyDashShift,
		CfgPhyStepShift	= 14,
		CfgPhyStepMask	= 3<<CfgPhyStepShift,

		EepromLocked	= 1<<21,
		NicReady	= 1<<22,
		HapwakeL1A	= 1<<23,
		PrepareDone	= 1<<25,
		Prepare		= 1<<27,
		EnablePme	= 1<<28,

	Isr		= 0x008,	/* interrupt status */
	Imr		= 0x00c,	/* interrupt mask */
		Ialive		= 1<<0,
		Iwakeup		= 1<<1,
		Iswrx		= 1<<3,
		Ictreached	= 1<<6,
		Irftoggled	= 1<<7,
		Iswerr		= 1<<25,
		Isched		= 1<<26,
		Ifhtx		= 1<<27,
		Irxperiodic	= 1<<28,
		Ihwerr		= 1<<29,
		Ifhrx		= 1<<31,

		Ierr		= Iswerr | Ihwerr,
		Idefmask	= Ierr | Ifhtx | Ifhrx | Ialive | Iwakeup | Iswrx | Ictreached | Irftoggled,

	FhIsr		= 0x010,	/* second interrupt status */

	Reset		= 0x020,
		
	Rev		= 0x028,	/* hardware revision */

	EepromIo	= 0x02c,	/* EEPROM i/o register */
	EepromGp	= 0x030,
		
	OtpromGp	= 0x034,
		DevSelOtp	= 1<<16,
		RelativeAccess	= 1<<17,
		EccCorrStts	= 1<<20,
		EccUncorrStts	= 1<<21,

	Gpc		= 0x024,	/* gp cntrl */
		MacAccessEna	= 1<<0,
		MacClockReady	= 1<<0,
		InitDone	= 1<<2,
		MacAccessReq	= 1<<3,
		NicSleep	= 1<<4,
		RfKill		= 1<<27,

	Gio		= 0x03c,
		EnaL0S		= 1<<1,

	GpDrv	= 0x050,
		GpDrvCalV6	= 1<<2,
		GpDrv1X2	= 1<<3,
		GpDrvRadioIqInvert	= 1<<7, 

	Led		= 0x094,
		LedBsmCtrl	= 1<<5,
		LedOn		= 0x38,
		LedOff		= 0x78,

	UcodeGp1Clr	= 0x05c,
		UcodeGp1RfKill		= 1<<1,
		UcodeGp1CmdBlocked	= 1<<2,
		UcodeGp1CtempStopRf	= 1<<3,

	ShadowRegCtrl	= 0x0a8,

	MboxSet	= 0x088,
		MboxSetOsAlive	= 1<<5,

	Giochicken	= 0x100,
		L1AnoL0Srx	= 1<<23,
		DisL0Stimer	= 1<<29,

	AnaPll		= 0x20c,

	Dbghpetmem	= 0x240,
	Dbglinkpwrmgmt	= 0x250,

	MemRaddr	= 0x40c,
	MemWaddr	= 0x410,
	MemWdata	= 0x418,
	MemRdata	= 0x41c,

	PrphWaddr	= 0x444,
	PrphRaddr	= 0x448,
	PrphWdata	= 0x44c,
	PrphRdata	= 0x450,

	HbusTargWptr	= 0x460,

	UcodeLoadStatus	= 0x1af0,
};

/*
 * Flow-Handler registers.
 */
enum {
	FhTfbdCtrl0	= 0x1900,	// +q*8
	FhTfbdCtrl1	= 0x1904,	// +q*8

	FhKwAddr	= 0x197c,

	FhSramAddr	= 0x19a4,	// +q*4

	FhCbbcQueue0	= 0x19d0,	// +q*4
	FhCbbcQueue16	= 0x1bf0,	// +q*4
	FhCbbcQueue20	= 0x1b20,	// +q*4

	FhStatusWptr	= 0x1bc0,
	FhRxBase	= 0x1bc4,
	FhRxWptr	= 0x1bc8,
	FhRxConfig	= 0x1c00,
		FhRxConfigEna		= 1<<31,
		FhRxConfigRbSize8K	= 1<<16,
		FhRxConfigSingleFrame	= 1<<15,
		FhRxConfigIrqDstHost	= 1<<12,
		FhRxConfigIgnRxfEmpty	= 1<<2,

		FhRxConfigNrbdShift	= 20,
		FhRxConfigRbTimeoutShift= 4,

	FhRxStatus	= 0x1c44,

	FhTxConfig	= 0x1d00,	// +q*32
		FhTxConfigDmaCreditEna	= 1<<3,
		FhTxConfigDmaEna	= 1<<31,
		FhTxConfigCirqHostEndTfd= 1<<20,

	FhTxBufStatus	= 0x1d08,	// +q*32
		FhTxBufStatusTbNumShift	= 20,
		FhTxBufStatusTbIdxShift = 12,
		FhTxBufStatusTfbdValid	= 3,

	FhTxChicken	= 0x1e98,

	FhTxStatus	= 0x1eb0,
	FhTxErrors	= 0x1eb8,
};

/*
 * NIC internal memory offsets.
 */
enum {
	ApmgClkCtrl	= 0x3000,
	ApmgClkEna	= 0x3004,
	ApmgClkDis	= 0x3008,
		DmaClkRqt	= 1<<9,
		BsmClkRqt	= 1<<11,

	ApmgPs		= 0x300c,
		EarlyPwroffDis	= 1<<22,
		PwrSrcVMain	= 0<<24,
		PwrSrcVAux	= 2<<24,
		PwrSrcMask	= 3<<24,
		ResetReq	= 1<<26,

	ApmgDigitalSvr	= 0x3058,
	ApmgAnalogSvr	= 0x306c,
	ApmgPciStt	= 0x3010,
	BsmWrCtrl	= 0x3400,
	BsmWrMemSrc	= 0x3404,
	BsmWrMemDst	= 0x3408,
	BsmWrDwCount	= 0x340c,
	BsmDramTextAddr	= 0x3490,
	BsmDramTextSize	= 0x3494,
	BsmDramDataAddr	= 0x3498,
	BsmDramDataSize	= 0x349c,
	BsmSramBase	= 0x3800,

	/* 8000 family */
	ReleaseCpuReset	= 0x300c,
		CpuResetBit = 0x1000000,

	LmpmChick	= 0xa01ff8,
		ExtAddr = 1,
};

/*
 * TX scheduler registers.
 */
enum {
	SchedBase		= 0xa02c00,
	SchedSramAddr		= SchedBase,

	SchedDramAddr4965	= SchedBase+0x010,
	SchedTxFact4965		= SchedBase+0x01c,
	SchedQueueRdptr4965	= SchedBase+0x064,	// +q*4
	SchedQChainSel4965	= SchedBase+0x0d0,
	SchedIntrMask4965	= SchedBase+0x0e4,
	SchedQueueStatus4965	= SchedBase+0x104,	// +q*4

	SchedDramAddr		= SchedBase+0x008,
	SchedTxFact		= SchedBase+0x010,
	SchedQueueWrptr		= SchedBase+0x018,	// +q*4
	SchedQueueRdptr		= SchedBase+0x068,	// +q*4
	SchedQChainSel		= SchedBase+0x0e8,
	SchedIntrMask		= SchedBase+0x108,

	SchedQueueStatus	= SchedBase+0x10c,	// +q*4

	SchedGpCtrl		= SchedBase+0x1a8,
		Enable31Queues	= 1<<0,
		AutoActiveMode	= 1<<18,

	SchedChainExtEn		= SchedBase+0x244,
	SchedAggrSel		= SchedBase+0x248,
	SchedEnCtrl		= SchedBase+0x254,

	SchedQueueRdptr20	= SchedBase+0x2b4,	// +q*4
	SchedQueueStatus20	= SchedBase+0x334,	// +q*4
};

enum {
	SchedCtxOff4965		= 0x380,
	SchedCtxLen4965		= 416,

	SchedCtxOff		= 0x600,		// +q*8

	SchedSttsOff		= 0x6A0,		// +q*16

	SchedTransTblOff	= 0x7E0,		// +q*2
};

enum {
	FilterPromisc		= 1<<0,
	FilterCtl		= 1<<1,
	FilterMulticast		= 1<<2,
	FilterNoDecrypt		= 1<<3,
	FilterNoDecryptMcast	= 1<<4,
	FilterBSS		= 1<<5,
	FilterBeacon		= 1<<6,
};

enum {
	RFlag24Ghz		= 1<<0,
	RFlagCCK		= 1<<1,
	RFlagAuto		= 1<<2,
	RFlagShSlot		= 1<<4,
	RFlagShPreamble		= 1<<5,
	RFlagNoDiversity	= 1<<7,
	RFlagAntennaA		= 1<<8,
	RFlagAntennaB		= 1<<9,
	RFlagTSF		= 1<<15,
	RFlagCTSToSelf		= 1<<30,
};

enum {
	TFlagNeedProtection	= 1<<0,
		TFlagNeedRTS		= 1<<1,
		TFlagNeedCTS		= 1<<2,
	TFlagNeedACK		= 1<<3,
	TFlagLinkq		= 1<<4,
	TFlagImmBa		= 1<<6,
	TFlagFullTxOp		= 1<<7,
	TFlagBtDis		= 1<<12,
	TFlagAutoSeq		= 1<<13,
	TFlagMoreFrag		= 1<<14,
	TFlagInsertTs		= 1<<16,
	TFlagNeedPadding	= 1<<20,
};

enum {
	CmdAdd = 1,
	CmdModify,
	CmdRemove,
};

typedef struct FWInfo FWInfo;
typedef struct FWImage FWImage;
typedef struct FWSect FWSect;
typedef struct FWBlock FWBlock;
typedef struct FWMem FWMem;

typedef struct TXQ TXQ;
typedef struct RXQ RXQ;

typedef struct Station Station;

typedef struct Ctlr Ctlr;

struct FWSect
{
	uchar	*data;
	uint	addr;
	uint	size;
};

struct FWImage
{
	struct {
		int	nsect;
		union {
			struct {
				FWSect	text;
				FWSect	data;
			};
			FWSect	sect[16];
		};
		struct {
			u32int	flowmask;
			u32int	eventmask;
		} defcalib;
	} init, main;

	struct {
		FWSect	text;
	} boot;

	uint	rev;
	uint	build;
	char	descr[64+1];

	u32int	capa[4];
	u32int	api[4];

	u32int	physku;

	u32int	pagedmemsize;

	uchar	data[];
};

struct FWInfo
{
	u32int	major;
	u32int	minor;
	uchar	type;
	uchar	subtype;

	u32int	scdptr;
	u32int	regptr;
	u32int	logptr;
	u32int	errptr;
	u32int	tstamp;

	struct {
		u32int	major;
		u32int	minor;
		u32int	errptr;
		u32int	logptr;
	} umac;
};

struct FWBlock
{
	uint	size;
	uchar	*p;
};

struct FWMem
{
	uchar	*css;

	uint	npage;
	uint	nblock;

	FWBlock	block[32];
};

struct TXQ
{
	uint	n;
	uint	i;
	Block	**b;
	uchar	*d;
	uchar	*c;

	uint	lastcmd;

	Rendez;
	QLock;
};

struct RXQ
{
	uint	i;
	Block	**b;
	u32int	*p;
	uchar	*s;
};

struct Station
{
	int	id;
};

struct Ctlr {
	Lock;
	QLock;

	Ctlr *link;
	uvlong port;
	Pcidev *pdev;
	Ether *edev;
	Wifi *wifi;

	char *fwname;
	int family;
	int type;
	uint step;
	uint dash;

	int power;
	int broken;
	int attached;

	u32int ie;

	u32int *nic;
	uchar *kwpage;

	/* assigned sta ids in hardware sta table or -1 if unassigned */
	Station bcast;
	Station bss;

	u32int rxflags;
	u32int rxfilter;

	int phyid;
	int macid;
	int bindid;
	int timeid;

	/* current receiver settings */
	uchar bssid[Eaddrlen];
	int channel;
	int prom;
	int aid;

	uvlong systime;

	RXQ rx;
	TXQ tx[7];

	int ndma;
	int ntxq;

	struct {
		Rendez;
		u32int	m;
		u32int	w;
	} wait;

	struct {
		uchar	type;
		uchar	step;
		uchar	dash;
		uchar	pnum;
		uchar	txantmask;
		uchar	rxantmask;
	} rfcfg;

	struct {
		int	otp;
		uint	off;

		uchar	version;
		uchar	type;
		u16int	volt;
		u16int	temp;
		u16int	rawtemp;

		char	regdom[4+1];

		u32int	crystal;
	} eeprom;

	struct {
		u32int	version;

		void	*buf;
		int	len;

		int	off;
		int	ret;
		int	type;
		int	sts;
	} nvm;

	struct {
		union {
			Block	*cmd[21];
			struct {
				Block *cfg;
				Block *nch;
				Block *papd[9];
				Block *txp[9];
			};
		};
		int	done;
	} calib;

	struct {
		u32int	base;
		uchar	*s;
	} sched;

	FWInfo fwinfo;
	FWImage *fw;

	FWMem fwmem;
};

/* controller types */
enum {
	Type4965	= 0,
	Type5300	= 2,
	Type5350	= 3,
	Type5150	= 4,
	Type5100	= 5,
	Type1000	= 6,
	Type6000	= 7,
	Type6050	= 8,
	Type6005	= 11,	/* also Centrino Advanced-N 6030, 6235 */
	Type2030	= 12,
	Type2000	= 16,

	Type8265	= 35,
};

static struct ratetab {
	uchar	rate;
	uchar	plcp;
	uchar	flags;
} ratetab[] = {
	{   2,  10, RFlagCCK },
	{   4,  20, RFlagCCK },
	{  11,  55, RFlagCCK },
	{  22, 110, RFlagCCK },

	{  12, 0xd, 0 },
	{  18, 0xf, 0 },
	{  24, 0x5, 0 },
	{  36, 0x7, 0 },
	{  48, 0x9, 0 },
	{  72, 0xb, 0 },
	{  96, 0x1, 0 },
	{ 108, 0x3, 0 },
	{ 120, 0x3, 0 }
};

static uchar iwlrates[] = {
	0x80 | 2,
	0x80 | 4,
	0x80 | 11,
	0x80 | 22,

	0x80 | 12,
	0x80 | 18,
	0x80 | 24,
	0x80 | 36,
	0x80 | 48,
	0x80 | 72,
	0x80 | 96,
	0x80 | 108,
	0x80 | 120,
	0
};

static char *fwname[32] = {
	[Type4965] "iwn-4965",
	[Type5300] "iwn-5000",
	[Type5350] "iwn-5000",
	[Type5150] "iwn-5150",
	[Type5100] "iwn-5000",
	[Type1000] "iwn-1000",
	[Type6000] "iwn-6000",
	[Type6050] "iwn-6050",
	[Type6005] "iwn-6005", /* see in iwlattach() below */
	[Type2030] "iwn-2030",
	[Type2000] "iwn-2000",
};

static char *qcmd(Ctlr *ctlr, uint qid, uint code, uchar *data, int size, Block *block);
static char *flushq(Ctlr *ctlr, uint qid);
static char *cmd(Ctlr *ctlr, uint code, uchar *data, int size);

#define csr32r(c, r)	(*((c)->nic+((r)/4)))
#define csr32w(c, r, v)	(*((c)->nic+((r)/4)) = (v))

static uint
get16(uchar *p){
	return *((u16int*)p);
}
static uint
get32(uchar *p){
	return *((u32int*)p);
}
static void
put32(uchar *p, uint v){
	*((u32int*)p) = v;
}
static void
put64(uchar *p, uvlong v)
{
	*((u64int*)p) = v;
}
static void
put16(uchar *p, uint v){
	*((u16int*)p) = v;
};

static char*
niclock(Ctlr *ctlr)
{
	int i;

	csr32w(ctlr, Gpc, csr32r(ctlr, Gpc) | MacAccessReq);
	for(i=0; i<1000; i++){
		if((csr32r(ctlr, Gpc) & (NicSleep | MacAccessEna)) == MacAccessEna)
			return 0;
		delay(10);
	}
	return "niclock: timeout";
}

static void
nicunlock(Ctlr *ctlr)
{
	csr32w(ctlr, Gpc, csr32r(ctlr, Gpc) & ~MacAccessReq);
}

static u32int
prphread(Ctlr *ctlr, uint off)
{
	csr32w(ctlr, PrphRaddr, ((sizeof(u32int)-1)<<24) | off);
	coherence();
	return csr32r(ctlr, PrphRdata);
}
static void
prphwrite(Ctlr *ctlr, uint off, u32int data)
{
	csr32w(ctlr, PrphWaddr, ((sizeof(u32int)-1)<<24) | off);
	coherence();
	csr32w(ctlr, PrphWdata, data);
}

static u32int
memread(Ctlr *ctlr, uint off)
{
	csr32w(ctlr, MemRaddr, off);
	coherence();
	return csr32r(ctlr, MemRdata);
}
static void
memwrite(Ctlr *ctlr, uint off, u32int data)
{
	csr32w(ctlr, MemWaddr, off);
	coherence();
	csr32w(ctlr, MemWdata, data);
}

static void
setfwinfo(Ctlr *ctlr, uchar *d, int len)
{
	FWInfo *i;

	i = &ctlr->fwinfo;

	switch(len){
	case 1+1+2+1+1 + 1+1 + 1+1 + 2 + 4+4+4+4+4+4 + 4:
	case 1+1+2+1+1 + 1+1 + 1+1 + 2 + 4+4+4+4+4+4 + 4 + 4+4 + 1+1 + 2 + 4+4:
		i->minor = *d++;
		i->major = *d++;
		d += 2;					// id
		d++;					// api minor
		d++;					// api major
		i->subtype = *d++;
		i->type = *d++;
		d++;					// mac
		d++;					// opt
		d += 2;					// reserved2

		i->tstamp = get32(d); d += 4;
		i->errptr = get32(d); d += 4;
		i->logptr = get32(d); d += 4;
		i->regptr = get32(d); d += 4;
		d += 4;					// dbgm_config_ptr
		d += 4;					// alive counter ptr

		i->scdptr = get32(d); d += 4;

		if(len < 1+1+2+1+1+1+1+1+1+2+4+4+4+4+4 + 4+4+4+1+1+2+4+4)
			break;

		d += 4;					// fwrd addr
		d += 4;					// fwrd size

		i->umac.minor = *d++;
		i->umac.major = *d++;
		d++;					// id
		d += 2;
		i->umac.errptr = get32(d); d += 4;
		i->umac.logptr = get32(d); d += 4;
		break;

	case 4+4 + 1+1 + 1+1 + 4+4+4+4+4+4 + 4 + 4+4 + 4+4+4+4:
		i->minor = get32(d);
		d += 4;
		i->major = get32(d);
		d += 4;
		i->subtype = *d++;
		i->type = *d++;
		d++;					// mac
		d++;					// opt

		i->tstamp = get32(d); d += 4;
		i->errptr = get32(d); d += 4;
		i->logptr = get32(d); d += 4;
		i->regptr = get32(d); d += 4;
		d += 4;					// dbgm_config_ptr
		d += 4;					// alive counter ptr

		i->scdptr = get32(d);
		d += 4;

		d += 4;					// fwrd addr
		d += 4;					// fwrd size

		i->umac.minor = get32(d); d += 4;
		i->umac.major = get32(d); d += 4;
		i->umac.errptr = get32(d); d += 4;
		i->umac.logptr = get32(d); d += 4;
		break;
	
	default:
		if(len < 32)
			break;
		i->minor = *d++;
		i->major = *d++;
		d += 2+8;
		i->type = *d++;
		i->subtype = *d++;
		d += 2;
		i->logptr = get32(d); d += 4;
		i->errptr = get32(d); d += 4;
		i->tstamp = get32(d); d += 4;
	}
	USED(d);

	if(0){
		iprint("fwinfo: ver %ud.%ud type %ud.%ud\n",
			i->major, i->minor, i->type, i->subtype);
		iprint("fwinfo: scdptr=%.8ux\n", i->scdptr);
		iprint("fwinfo: regptr=%.8ux\n", i->regptr);
		iprint("fwinfo: logptr=%.8ux\n", i->logptr);
		iprint("fwinfo: errptr=%.8ux\n", i->errptr);

		iprint("fwinfo: ts=%.8ux\n", i->tstamp);

		iprint("fwinfo: umac ver %ud.%ud\n", i->umac.major, i->umac.minor);
		iprint("fwinfo: umac errptr %.8ux\n", i->umac.errptr);
		iprint("fwinfo: umac logptr %.8ux\n", i->umac.logptr);
	}
}

static void
dumpctlr(Ctlr *ctlr)
{
	u32int dump[13];
	int i;

	print("lastcmd: %ud (0x%ux)\n", ctlr->tx[4].lastcmd,  ctlr->tx[4].lastcmd);

	if(ctlr->fwinfo.errptr == 0){
		print("no error pointer\n");
		return;
	}
	for(i=0; i<nelem(dump); i++)
		dump[i] = memread(ctlr, ctlr->fwinfo.errptr + i*4);

	if(ctlr->family >= 7000){
		print(	"error:\tid %ux, trm_hw_status %.8ux %.8ux,\n"
			"\tbranchlink2 %.8ux, interruptlink %.8ux %.8ux,\n"
			"\terrordata %.8ux %.8ux %.8ux\n",
			dump[1], dump[2], dump[3],
			dump[4], dump[5], dump[6],
			dump[7], dump[8], dump[9]);
	} else {
		print(	"error:\tid %ux, pc %ux,\n"
			"\tbranchlink %.8ux %.8ux, interruptlink %.8ux %.8ux,\n"
			"\terrordata %.8ux %.8ux, srcline %ud, tsf %ux, time %ux\n",
			dump[1], dump[2],
			dump[4], dump[3], dump[6], dump[5],
			dump[7], dump[8], dump[9], dump[10], dump[11]);
	}
}

static char*
eepromlock(Ctlr *ctlr)
{
	int i, j;

	for(i=0; i<100; i++){
		csr32w(ctlr, Cfg, csr32r(ctlr, Cfg) | EepromLocked);
		for(j=0; j<100; j++){
			if(csr32r(ctlr, Cfg) & EepromLocked)
				return 0;
			delay(10);
		}
	}
	return "eepromlock: timeout";
}
static void
eepromunlock(Ctlr *ctlr)
{
	csr32w(ctlr, Cfg, csr32r(ctlr, Cfg) & ~EepromLocked);
}
static char*
eepromread(Ctlr *ctlr, void *data, int count, uint off)
{
	uchar *out = data;
	u32int w, s;
	int i;

	w = 0;
	off += ctlr->eeprom.off;
	for(; count > 0; count -= 2, off++){
		csr32w(ctlr, EepromIo, off << 2);
		for(i=0; i<10; i++){
			w = csr32r(ctlr, EepromIo);
			if(w & 1)
				break;
			delay(5);
		}
		if(i == 10)
			return "eepromread: timeout";
		if(ctlr->eeprom.otp){
			s = csr32r(ctlr, OtpromGp);
			if(s & EccUncorrStts)
				return "eepromread: otprom ecc error";
			if(s & EccCorrStts)
				csr32w(ctlr, OtpromGp, s);
		}
		*out++ = w >> 16;
		if(count > 1)
			*out++ = w >> 24;
	}
	return 0;
}

static char*
handover(Ctlr *ctlr)
{
	int i;

	csr32w(ctlr, Cfg, csr32r(ctlr, Cfg) | NicReady);
	for(i=0; i<5; i++){
		if(csr32r(ctlr, Cfg) & NicReady)
			goto Ready;
		delay(10);
	}

	if(ctlr->family >= 7000){
		csr32w(ctlr, Dbglinkpwrmgmt, csr32r(ctlr, Dbglinkpwrmgmt) | (1<<31));
		delay(1);
	}

	csr32w(ctlr, Cfg, csr32r(ctlr, Cfg) | Prepare);
	for(i=0; i<15000; i++){
		if((csr32r(ctlr, Cfg) & PrepareDone) == 0)
			break;
		delay(10);
	}
	if(i >= 15000)
		return "handover: timeout";
	csr32w(ctlr, Cfg, csr32r(ctlr, Cfg) | NicReady);
	for(i=0; i<5; i++){
		if(csr32r(ctlr, Cfg) & NicReady)
			goto Ready;
		delay(10);
	}
	return "handover: timeout";
Ready:
	if(ctlr->family >= 7000)
		csr32w(ctlr, MboxSet, csr32r(ctlr, MboxSet) | MboxSetOsAlive);
	return nil;
}

static char*
clockwait(Ctlr *ctlr)
{
	int i;

	/* Set "initialization complete" bit. */
	csr32w(ctlr, Gpc, csr32r(ctlr, Gpc) | InitDone);
	for(i=0; i<2500; i++){
		if(csr32r(ctlr, Gpc) & MacClockReady)
			return 0;
		delay(10);
	}
	return "clockwait: timeout";
}

static char*
poweron(Ctlr *ctlr)
{
	int capoff;
	char *err;


	if(ctlr->family >= 7000){
		/* Reset entire device */
		csr32w(ctlr, Reset, (1<<7));
		delay(5);
	}

	if(ctlr->family < 8000){
		/* Disable L0s exit timer (NMI bug workaround). */
		csr32w(ctlr, Giochicken, csr32r(ctlr, Giochicken) | DisL0Stimer);
	}

	/* Don't wait for ICH L0s (ICH bug workaround). */
	csr32w(ctlr, Giochicken, csr32r(ctlr, Giochicken) | L1AnoL0Srx);

	/* Set FH wait threshold to max (HW bug under stress workaround). */
	csr32w(ctlr, Dbghpetmem, csr32r(ctlr, Dbghpetmem) | 0xffff0000);

	/* Enable HAP INTA to move adapter from L1a to L0s. */
	csr32w(ctlr, Cfg, csr32r(ctlr, Cfg) | HapwakeL1A);

	capoff = pcicap(ctlr->pdev, PciCapPCIe);
	if(capoff != -1){
		/* Workaround for HW instability in PCIe L0->L0s->L1 transition. */
		if(pcicfgr16(ctlr->pdev, capoff + 0x10) & 0x2)	/* LCSR -> L1 Entry enabled. */
			csr32w(ctlr, Gio, csr32r(ctlr, Gio) | EnaL0S);
		else
			csr32w(ctlr, Gio, csr32r(ctlr, Gio) & ~EnaL0S);
	}

	if(ctlr->family < 7000){
		if(ctlr->type != Type4965 && ctlr->type <= Type1000)
			csr32w(ctlr, AnaPll, csr32r(ctlr, AnaPll) | 0x00880300);
	}

	/* Wait for clock stabilization before accessing prph. */
	if((err = clockwait(ctlr)) != nil)
		return err;

	if(ctlr->family < 8000){
		if((err = niclock(ctlr)) != nil)
			return err;

		/* Enable DMA and BSM (Bootstrap State Machine). */
		if(ctlr->type == Type4965)
			prphwrite(ctlr, ApmgClkEna, DmaClkRqt | BsmClkRqt);
		else
			prphwrite(ctlr, ApmgClkEna, DmaClkRqt);
		delay(20);

		/* Disable L1-Active. */
		prphwrite(ctlr, ApmgPciStt, prphread(ctlr, ApmgPciStt) | (1<<11));

		nicunlock(ctlr);
	}

	ctlr->power = 1;

	return 0;
}

static void
poweroff(Ctlr *ctlr)
{
	int i, j;

	csr32w(ctlr, Reset, 1);

	/* Disable interrupts */
	ctlr->ie = 0;
	csr32w(ctlr, Imr, 0);
	csr32w(ctlr, Isr, ~0);
	csr32w(ctlr, FhIsr, ~0);

	/* Stop scheduler */
	if(ctlr->family >= 7000 || ctlr->type != Type4965)
		prphwrite(ctlr, SchedTxFact, 0);
	else
		prphwrite(ctlr, SchedTxFact4965, 0);

	/* Stop TX ring */
	if(niclock(ctlr) == nil){
		for(i = 0; i < ctlr->ndma; i++){
			csr32w(ctlr, FhTxConfig + i*32, 0);
			for(j = 0; j < 200; j++){
				if(csr32r(ctlr, FhTxStatus) & (0x10000<<i))
					break;
				delay(10);
			}
		}
		nicunlock(ctlr);
	}

	/* Stop RX ring */
	if(niclock(ctlr) == nil){
		csr32w(ctlr, FhRxConfig, 0);
		for(j = 0; j < 200; j++){
			if(csr32r(ctlr, FhRxStatus) & 0x1000000)
				break;
			delay(10);
		}
		nicunlock(ctlr);
	}

	if(ctlr->family <= 7000){
		/* Disable DMA */
		if(niclock(ctlr) == nil){
			prphwrite(ctlr, ApmgClkDis, DmaClkRqt);
			nicunlock(ctlr);
		}
		delay(5);
	}

	if(ctlr->family >= 7000){
		csr32w(ctlr, Dbglinkpwrmgmt, csr32r(ctlr, Dbglinkpwrmgmt) | (1<<31));
		csr32w(ctlr, Cfg, csr32r(ctlr, Cfg) | Prepare|EnablePme);
		delay(1);
		csr32w(ctlr, Dbglinkpwrmgmt, csr32r(ctlr, Dbglinkpwrmgmt) & ~(1<<31));
		delay(5);
	}

	/* Stop busmaster DMA activity. */
	csr32w(ctlr, Reset, csr32r(ctlr, Reset) | (1<<9));
	for(j = 0; j < 100; j++){
		if(csr32r(ctlr, Reset) & (1<<8))
			break;
		delay(10);
	}

	/* Reset the entire device. */
	csr32w(ctlr, Reset, csr32r(ctlr, Reset) | (1<<7));
	delay(10);

	/* Clear "initialization complete" bit. */
	csr32w(ctlr, Gpc, csr32r(ctlr, Gpc) & ~InitDone);

	ctlr->power = 0;
}

static char*
rominit(Ctlr *ctlr)
{
	uint prev, last;
	uchar buf[2];
	char *err;
	int i;

	ctlr->eeprom.otp = 0;
	ctlr->eeprom.off = 0;
	if(ctlr->type < Type1000 || (csr32r(ctlr, OtpromGp) & DevSelOtp) == 0)
		return nil;

	/* Wait for clock stabilization before accessing prph. */
	if((err = clockwait(ctlr)) != nil)
		return err;

	if((err = niclock(ctlr)) != nil)
		return err;
	prphwrite(ctlr, ApmgPs, prphread(ctlr, ApmgPs) | ResetReq);
	delay(5);
	prphwrite(ctlr, ApmgPs, prphread(ctlr, ApmgPs) & ~ResetReq);
	nicunlock(ctlr);

	/* Set auto clock gate disable bit for HW with OTP shadow RAM. */
	if(ctlr->type != Type1000)
		csr32w(ctlr, Dbglinkpwrmgmt, csr32r(ctlr, Dbglinkpwrmgmt) | (1<<31));

	csr32w(ctlr, EepromGp, csr32r(ctlr, EepromGp) & ~0x00000180);

	/* Clear ECC status. */
	csr32w(ctlr, OtpromGp, csr32r(ctlr, OtpromGp) | (EccCorrStts | EccUncorrStts));

	ctlr->eeprom.otp = 1;
	if(ctlr->type != Type1000)
		return nil;

	/* Switch to absolute addressing mode. */
	csr32w(ctlr, OtpromGp, csr32r(ctlr, OtpromGp) & ~RelativeAccess);

	/*
	 * Find the block before last block (contains the EEPROM image)
	 * for HW without OTP shadow RAM.
	 */
	prev = last = 0;
	for(i=0; i<3; i++){
		if((err = eepromread(ctlr, buf, 2, last)) != nil)
			return err;
		if(get16(buf) == 0)
			break;
		prev = last;
		last = get16(buf);
	}
	if(i == 0 || i >= 3)
		return "rominit: missing eeprom image";

	ctlr->eeprom.off = prev+1;
	return nil;
}

static int
iwlinit(Ether *edev)
{
	Ctlr *ctlr;
	char *err;
	uchar b[4];
	uint u, caloff, regoff;

	ctlr = edev->ctlr;

	/* Clear device-specific "PCI retry timeout" register (41h). */
	if(pcicfgr8(ctlr->pdev, 0x41) != 0)
		pcicfgw8(ctlr->pdev, 0x41, 0);

	/* Clear interrupt disable bit. Hardware bug workaround. */
	if(ctlr->pdev->pcr & 0x400){
		ctlr->pdev->pcr &= ~0x400;
		pcicfgw16(ctlr->pdev, PciPCR, ctlr->pdev->pcr);
	}

	ctlr->type = csr32r(ctlr, Rev);
	if(ctlr->family >= 8000){
		ctlr->type &= 0xFFFF;
		ctlr->dash = 0;
		ctlr->step = ctlr->type & 15, ctlr->type >>= 4;
	} else {
		ctlr->type &= 0x1FF;
		ctlr->dash = ctlr->type & 3, ctlr->type >>= 2;
		ctlr->step = ctlr->type & 3, ctlr->type >>= 2;
		if(fwname[ctlr->type] == nil){
			print("iwl: unsupported controller type %d\n", ctlr->type);
			return -1;
		}
	}

	if((err = handover(ctlr)) != nil)
		goto Err;

	/* >= 7000 family needs firmware loaded to access NVM */
	if(ctlr->family >= 7000)
		return 0;

	if((err = poweron(ctlr)) != nil)
		goto Err;

	if((csr32r(ctlr, EepromGp) & 0x7) == 0){
		err = "bad rom signature";
		goto Err;
	}
	if((err = eepromlock(ctlr)) != nil)
		goto Err;
	if((err = rominit(ctlr)) != nil)
		goto Err2;
	if((err = eepromread(ctlr, edev->ea, sizeof(edev->ea), 0x15)) != nil){
		eepromunlock(ctlr);
		goto Err;
	}
	if((err = eepromread(ctlr, b, 2, 0x048)) != nil){
	Err2:
		eepromunlock(ctlr);
		goto Err;
	}
	u = get16(b);
	ctlr->rfcfg.type = u & 3;	u >>= 2;
	ctlr->rfcfg.step = u & 3;	u >>= 2;
	ctlr->rfcfg.dash = u & 3;	u >>= 4;
	ctlr->rfcfg.txantmask = u & 15;	u >>= 4;
	ctlr->rfcfg.rxantmask = u & 15;
	if((err = eepromread(ctlr, b, 2, 0x66)) != nil)
		goto Err2;
	regoff = get16(b);
	if((err = eepromread(ctlr, b, 4, regoff+1)) != nil)
		goto Err2;
	strncpy(ctlr->eeprom.regdom, (char*)b, 4);
	ctlr->eeprom.regdom[4] = 0;
	if((err = eepromread(ctlr, b, 2, 0x67)) != nil)
		goto Err2;
	caloff = get16(b);
	if((err = eepromread(ctlr, b, 4, caloff)) != nil)
		goto Err2;
	ctlr->eeprom.version = b[0];
	ctlr->eeprom.type = b[1];
	ctlr->eeprom.volt = get16(b+2);

	ctlr->eeprom.temp = 0;
	ctlr->eeprom.rawtemp = 0;
	if(ctlr->type == Type2030 || ctlr->type == Type2000){
		if((err = eepromread(ctlr, b, 2, caloff + 0x12a)) != nil)
			goto Err2;
		ctlr->eeprom.temp = get16(b);
		if((err = eepromread(ctlr, b, 2, caloff + 0x12b)) != nil)
			goto Err2;
		ctlr->eeprom.rawtemp = get16(b);
	}

	if(ctlr->type != Type4965 && ctlr->type != Type5150){
		if((err = eepromread(ctlr, b, 4, caloff + 0x128)) != nil)
			goto Err2;
		ctlr->eeprom.crystal = get32(b);
	}
	eepromunlock(ctlr);

	switch(ctlr->type){
	case Type4965:
		ctlr->rfcfg.txantmask = 3;
		ctlr->rfcfg.rxantmask = 7;
		break;
	case Type5100:
		ctlr->rfcfg.txantmask = 2;
		ctlr->rfcfg.rxantmask = 3;
		break;
	case Type6000:
		if(ctlr->pdev->did == 0x422c || ctlr->pdev->did == 0x4230){
			ctlr->rfcfg.txantmask = 6;
			ctlr->rfcfg.rxantmask = 6;
		}
		break;
	}
	poweroff(ctlr);
	return 0;
Err:
	print("iwlinit: %s\n", err);
	poweroff(ctlr);
	return -1;
}

static char*
crackfw(FWImage *i, uchar *data, uint size, int alt)
{
	uchar *p, *e;
	FWSect *s;
	uint t, l;

	memset(i, 0, sizeof(*i));
	if(size < 4){
Tooshort:
		return "firmware image too short";
	}
	p = data;
	e = p + size;
	i->rev = get32(p); p += 4;
	if(i->rev == 0){
		uvlong altmask;

		if(size < (4+64+4+4+8))
			goto Tooshort;
		if(memcmp(p, "IWL\n", 4) != 0)
			return "bad firmware signature";
		p += 4;
		strncpy(i->descr, (char*)p, 64);
		i->descr[64] = 0;
		p += 64;
		i->rev = get32(p); p += 4;
		i->build = get32(p); p += 4;
		altmask = get32(p); p += 4;
		altmask |= (uvlong)get32(p) << 32; p += 4;
		while(alt > 0 && (altmask & (1ULL<<alt)) == 0)
			alt--;
		for(;p < e; p += (l + 3) & ~3){
			if(p + 8 > e)
				goto Tooshort;

			t = get32(p), p += 4;
			l = get32(p), p += 4;
			if(p + l > e)
				goto Tooshort;

			if((t >> 16) != 0 && (t >> 16) != alt)
				continue;

			switch(t & 0xFFFF){
			case 1:
				s = &i->main.text;
				if(i->main.nsect < 1)
					i->main.nsect = 1;
				s->addr = 0x00000000;
				break;
			case 2:
				s = &i->main.data;
				if(i->main.nsect < 2)
					i->main.nsect = 2;
				s->addr = 0x00800000;
				break;
			case 3:
				s = &i->init.text;
				if(i->init.nsect < 1)
					i->init.nsect = 1;
				s->addr = 0x00000000;
				break;
			case 4:
				s = &i->init.data;
				if(i->init.nsect < 2)
					i->init.nsect = 2;
				s->addr = 0x00800000;
				break;
			case 5:
				s = &i->boot.text;
				s->addr = 0x00000000;
				break;
			case 19:
				if(i->main.nsect >= nelem(i->main.sect))
					return "too many main sections";
				s = &i->main.sect[i->main.nsect++];
				goto Chunk;
			case 20:
				if(i->init.nsect >= nelem(i->init.sect))
					return "too many init sections";
				s = &i->init.sect[i->init.nsect++];
			Chunk:
				if(l < 4)
					goto Tooshort;
				s->addr = get32(p);
				p += 4, l -= 4;
				break;

			case 22:
				if(l < 12)
					goto Tooshort;
				switch(get32(p)){
				case 0:
					i->main.defcalib.flowmask = get32(p+4);
					i->main.defcalib.eventmask = get32(p+8);
					break;
				case 1:
					i->init.defcalib.flowmask = get32(p+4);
					i->init.defcalib.eventmask = get32(p+8);
					break;
				}
				continue;

			case 23:
				if(l < 4)
					goto Tooshort;
				i->physku = get32(p);
				continue;

			case 29:
				if(l < 8)
					goto Tooshort;
				t = get32(p);
				if(t >= nelem(i->api))
					goto Tooshort;
				i->api[t] = get32(p+4);
				continue;
			case 30:
				if(l < 8)
					goto Tooshort;
				t = get32(p);
				if(t >= nelem(i->capa))
					goto Tooshort;
				i->capa[t] = get32(p+4);
				continue;

			case 32:
				if(l < 4)
					goto Tooshort;
				i->pagedmemsize = get32(p) & -FWPagesize;
				continue;

			default:
				continue;
			}
			s->size = l;
			s->data = p;
		}
	} else {
		if(((i->rev>>8) & 0xFF) < 2)
			return "need firmware api >= 2";
		if(((i->rev>>8) & 0xFF) >= 3){
			i->build = get32(p); p += 4;
		}
		if((p + 5*4) > e)
			goto Tooshort;

		i->main.text.size = get32(p); p += 4;
		i->main.data.size = get32(p); p += 4;
		i->init.text.size = get32(p); p += 4;
		i->init.data.size = get32(p); p += 4;
		i->boot.text.size = get32(p); p += 4;

		i->main.text.data = p; p += i->main.text.size;
		i->main.data.data = p; p += i->main.data.size;
		i->init.text.data = p; p += i->init.text.size;
		i->init.data.data = p; p += i->init.data.size;
		i->boot.text.data = p; p += i->boot.text.size;
		if(p > e)
			goto Tooshort;

		i->main.nsect = 2;
		i->init.nsect = 2;
		i->main.text.addr = 0x00000000;
		i->main.data.addr = 0x00800000;
		i->init.text.addr = 0x00000000;
		i->init.data.addr = 0x00800000;
	}
	return 0;
}

static FWImage*
readfirmware(char *name)
{
	uchar dirbuf[sizeof(Dir)+100], *data;
	char buf[128], *err;
	FWImage *fw;
	int n, r;
	Chan *c;
	Dir d;

	if(!iseve())
		error(Eperm);
	if(!waserror()){
		snprint(buf, sizeof buf, "/boot/%s", name);
		c = namec(buf, Aopen, OREAD, 0);
		poperror();
	} else {
		snprint(buf, sizeof buf, "/lib/firmware/%s", name);
		c = namec(buf, Aopen, OREAD, 0);
	}
	if(waserror()){
		cclose(c);
		nexterror();
	}
	n = devtab[c->type]->stat(c, dirbuf, sizeof dirbuf);
	if(n <= 0)
		error("can't stat firmware");
	convM2D(dirbuf, n, &d, nil);
	fw = smalloc(sizeof(*fw) + 16 + d.length);
	data = (uchar*)(fw+1);
	if(waserror()){
		free(fw);
		nexterror();
	}
	r = 0;
	while(r < d.length){
		n = devtab[c->type]->read(c, data+r, d.length-r, (vlong)r);
		if(n <= 0)
			break;
		r += n;
	}
	if((err = crackfw(fw, data, r, 1)) != nil)
		error(err);
	poperror();
	poperror();
	cclose(c);
	return fw;
}


static int
gotirq(void *arg)
{
	Ctlr *ctlr = arg;
	return (ctlr->wait.m & ctlr->wait.w) != 0;
}

static u32int
irqwait(Ctlr *ctlr, u32int mask, int timeout)
{
	u32int r;

	ilock(ctlr);
	r = ctlr->wait.m & mask;
	if(r == 0){
		ctlr->wait.w = mask;
		iunlock(ctlr);
		if(!waserror()){
			tsleep(&ctlr->wait, gotirq, ctlr, timeout);
			poperror();
		}
		ilock(ctlr);
		ctlr->wait.w = 0;
		r = ctlr->wait.m & mask;
	}
	ctlr->wait.m &= ~r;
	iunlock(ctlr);
	return r;
}

static int
rbplant(Ctlr *ctlr, int i)
{
	Block *b;

	b = iallocb(Rbufsize + 256);
	if(b == nil)
		return -1;
	b->rp = b->wp = (uchar*)ROUND((uintptr)b->base, 256);
	memset(b->rp, 0, Rdscsize);
	ctlr->rx.b[i] = b;
	ctlr->rx.p[i] = PCIWADDR(b->rp) >> 8;
	return 0;
}

static char*
initmem(Ctlr *ctlr)
{
	RXQ *rx;
	TXQ *tx;
	int i, q;

	if(ctlr->fw->pagedmemsize > 0){
		ctlr->fwmem.npage = ctlr->fw->pagedmemsize >> FWPageshift;
		ctlr->fwmem.nblock = ctlr->fwmem.npage >> FWBlockshift;
		if(ctlr->fwmem.nblock >= nelem(ctlr->fwmem.block)-1)
			return "paged memory size too big";
		for(i = 0; i < ctlr->fwmem.nblock; i++)
			ctlr->fwmem.block[i].size = FWBlocksize;
		ctlr->fwmem.block[i].size = (ctlr->fwmem.npage % FWBlockpages) << FWPageshift;
		if(ctlr->fwmem.block[i].size != 0)
			ctlr->fwmem.nblock++;
		for(i = 0; i < ctlr->fwmem.nblock; i++){
			if(ctlr->fwmem.block[i].p == nil){
				ctlr->fwmem.block[i].p = mallocalign(ctlr->fwmem.block[i].size, FWPagesize, 0, 0);
				if(ctlr->fwmem.block[i].p == nil)
					return "no memory for firmware block";
			}
		}
		if(ctlr->fwmem.css == nil){
			if((ctlr->fwmem.css = mallocalign(FWPagesize, FWPagesize, 0, 0)) == nil)
				return "no memory for firmware css page";
		}
	}

	rx = &ctlr->rx;
	if(rx->b == nil)
		rx->b = malloc(sizeof(Block*) * Nrx);
	if(rx->p == nil)
		rx->p = mallocalign(sizeof(u32int) * Nrx, 256, 0, 0);
	if(rx->s == nil)
		rx->s = mallocalign(Rstatsize, 16, 0, 0);
	if(rx->b == nil || rx->p == nil || rx->s == nil)
		return "no memory for rx ring";
	memset(ctlr->rx.s, 0, Rstatsize);
	for(i=0; i<Nrx; i++){
		rx->p[i] = 0;
		if(rx->b[i] != nil){
			freeb(rx->b[i]);
			rx->b[i] = nil;
		}
		if(rbplant(ctlr, i) < 0)
			return "no memory for rx descriptors";
	}
	rx->i = 0;

	ctlr->ndma = 8;
	ctlr->ntxq = 20;
	if(ctlr->family >= 7000) {
		ctlr->ntxq = 31;
	} else {
		if(ctlr->type == Type4965) {
			ctlr->ndma = 7;
			ctlr->ntxq = 16;
		}
	}

	if(ctlr->sched.s == nil)
		ctlr->sched.s = mallocalign(512 * ctlr->ntxq * 2, 1024, 0, 0);
	if(ctlr->sched.s == nil)
		return "no memory for sched buffer";
	memset(ctlr->sched.s, 0, 512 * ctlr->ntxq);

	for(q=0; q < nelem(ctlr->tx); q++){
		tx = &ctlr->tx[q];
		if(tx->b == nil)
			tx->b = malloc(sizeof(Block*) * Ntx);
		if(tx->d == nil)
			tx->d = mallocalign(Tdscsize * Ntx, 256, 0, 0);
		if(tx->c == nil)
			tx->c = mallocalign(Tcmdsize * Ntx, 4, 0, 0);
		if(tx->b == nil || tx->d == nil || tx->c == nil)
			return "no memory for tx ring";
		memset(tx->d, 0, Tdscsize * Ntx);
		memset(tx->c, 0, Tcmdsize * Ntx);
		for(i=0; i<Ntx; i++){
			if(tx->b[i] != nil){
				freeblist(tx->b[i]);
				tx->b[i] = nil;
			}
		}
		tx->i = 0;
		tx->n = 0;
		tx->lastcmd = 0;
	}

	if(ctlr->kwpage == nil)
		ctlr->kwpage = mallocalign(4096, 4096, 0, 0);
	if(ctlr->kwpage == nil)
		return "no memory for kwpage";		
	memset(ctlr->kwpage, 0, 4096);

	return nil;
}

static char*
reset(Ctlr *ctlr)
{
	char *err;
	int q, i;

	if(ctlr->power)
		poweroff(ctlr);
	if((err = initmem(ctlr)) != nil)
		return err;
	if((err = poweron(ctlr)) != nil)
		return err;

	if(ctlr->family <= 7000){
		if((err = niclock(ctlr)) != nil)
			return err;
		prphwrite(ctlr, ApmgPs, (prphread(ctlr, ApmgPs) & ~PwrSrcMask) | PwrSrcVMain);
		nicunlock(ctlr);
	}

	if(ctlr->family >= 7000){
		u32int u;

		u = csr32r(ctlr, Cfg);

		u &= ~(RadioSi|MacSi|CfgMacDashMask|CfgMacStepMask|CfgPhyTypeMask|CfgPhyStepMask|CfgPhyDashMask);

		u |= (ctlr->step << CfgMacStepShift) & CfgMacStepMask;
		u |= (ctlr->dash << CfgMacDashShift) & CfgMacDashMask;

		u |= ctlr->rfcfg.type << CfgPhyTypeShift;
		u |= ctlr->rfcfg.step << CfgPhyStepShift;
		u |= ctlr->rfcfg.dash << CfgPhyDashShift;

		csr32w(ctlr, Cfg, u);

	} else {
		csr32w(ctlr, Cfg, csr32r(ctlr, Cfg) | RadioSi | MacSi);
	}

	if(ctlr->family < 8000){
		if((err = niclock(ctlr)) != nil)
			return err;
		if(ctlr->family == 7000 || ctlr->type != Type4965)
			prphwrite(ctlr, ApmgPs, prphread(ctlr, ApmgPs) | EarlyPwroffDis);
		nicunlock(ctlr);
	}
	if(ctlr->family < 7000){
		if((err = niclock(ctlr)) != nil)
			return err;
		if(ctlr->type == Type1000){
			/*
			 * Select first Switching Voltage Regulator (1.32V) to
			 * solve a stability issue related to noisy DC2DC line
			 * in the silicon of 1000 Series.
			 */
			prphwrite(ctlr, ApmgDigitalSvr, 
				(prphread(ctlr, ApmgDigitalSvr) & ~(0xf<<5)) | (3<<5));
		}
		if((ctlr->type == Type6005 || ctlr->type == Type6050) && ctlr->eeprom.version == 6)
			csr32w(ctlr, GpDrv, csr32r(ctlr, GpDrv) | GpDrvCalV6);
		if(ctlr->type == Type6005)
			csr32w(ctlr, GpDrv, csr32r(ctlr, GpDrv) | GpDrv1X2);
		if(ctlr->type == Type2030 || ctlr->type == Type2000)
			csr32w(ctlr, GpDrv, csr32r(ctlr, GpDrv) | GpDrvRadioIqInvert);
		nicunlock(ctlr);
	}

	if((err = niclock(ctlr)) != nil)
		return err;
	csr32w(ctlr, FhRxConfig, 0);
	csr32w(ctlr, FhRxWptr, 0);
	csr32w(ctlr, FhRxBase, PCIWADDR(ctlr->rx.p) >> 8);
	csr32w(ctlr, FhStatusWptr, PCIWADDR(ctlr->rx.s) >> 4);
	csr32w(ctlr, FhRxConfig,
		FhRxConfigEna | 
		FhRxConfigIgnRxfEmpty |
		FhRxConfigIrqDstHost | 
		FhRxConfigSingleFrame |
		(Nrxlog << FhRxConfigNrbdShift));
	csr32w(ctlr, FhRxWptr, (Nrx-1) & ~7);

	if(ctlr->family >= 7000 || ctlr->type != Type4965)
		prphwrite(ctlr, SchedTxFact, 0);
	else
		prphwrite(ctlr, SchedTxFact4965, 0);

	if(ctlr->family >= 7000){
		prphwrite(ctlr, SchedEnCtrl, 0);
		prphwrite(ctlr, SchedGpCtrl, prphread(ctlr, SchedGpCtrl)
			| Enable31Queues*(ctlr->ntxq == 31)
			| AutoActiveMode);
		for(q = 0; q < ctlr->ntxq; q++)
			prphwrite(ctlr, (q<20? SchedQueueStatus: SchedQueueStatus20) + q*4, 1 << 19);
	}

	csr32w(ctlr, FhKwAddr, PCIWADDR(ctlr->kwpage) >> 4);
	for(q = 0; q < ctlr->ntxq; q++){
		i = q < nelem(ctlr->tx) ? q : nelem(ctlr->tx)-1;
		if(q < 16)
			csr32w(ctlr, FhCbbcQueue0 + q*4, PCIWADDR(ctlr->tx[i].d) >> 8);
		else if(q < 20)
			csr32w(ctlr, FhCbbcQueue16 + (q-16)*4, PCIWADDR(ctlr->tx[i].d) >> 8);
		else
			csr32w(ctlr, FhCbbcQueue20 + (q-20)*4, PCIWADDR(ctlr->tx[i].d) >> 8);
	}

	if(ctlr->family >= 7000 || ctlr->type >= Type6000)
		csr32w(ctlr, ShadowRegCtrl, csr32r(ctlr, ShadowRegCtrl) | 0x800fffff);

	nicunlock(ctlr);

	csr32w(ctlr, UcodeGp1Clr, UcodeGp1RfKill);
	csr32w(ctlr, UcodeGp1Clr, UcodeGp1CmdBlocked);

	ctlr->systime = 0;

	ctlr->broken = 0;
	ctlr->wait.m = 0;
	ctlr->wait.w = 0;

	ctlr->bcast.id = -1;
	ctlr->bss.id = -1;

	ctlr->phyid = -1;
	ctlr->macid = -1;
	ctlr->bindid = -1;
	ctlr->timeid = -1;
	ctlr->aid = 0;

	ctlr->ie = Idefmask;
	csr32w(ctlr, Imr, ctlr->ie);
	csr32w(ctlr, Isr, ~0);

	csr32w(ctlr, UcodeGp1Clr, UcodeGp1RfKill);
	csr32w(ctlr, UcodeGp1Clr, UcodeGp1RfKill);
	csr32w(ctlr, UcodeGp1Clr, UcodeGp1RfKill);

	return nil;
}

static char*
sendmccupdate(Ctlr *ctlr, char *mcc)
{
	uchar c[2+1+1+4+5*4], *p;

	memset(p = c, 0, sizeof(c));
	*p++ = mcc[1];
	*p++ = mcc[0];
	*p++ = 0;
	*p++ = 0;	// reserved
	if(1){
		p += 4;
		p += 5*4;
	}
	return cmd(ctlr, 200, c, p - c);
}

static char*
sendbtcoexadv(Ctlr *ctlr)
{
	static u32int btcoex3wire[12] = {
		0xaaaaaaaa, 0xaaaaaaaa, 0xaaaaaaaa, 0xaaaaaaaa,
		0xcc00ff28, 0x0000aaaa, 0xcc00aaaa, 0x0000aaaa,
		0xc0004000, 0x00004000, 0xf0005000, 0xf0005000,
	};

	uchar c[Tcmdsize], *p;
	char *err;
	int i;

	/* set BT config */
	memset(c, 0, sizeof(c));
	p = c;

	if(ctlr->family >= 7000){
		put32(p, 3);
		p += 4;
		put32(p, (1<<4));
		p += 4;
	} else if(ctlr->type == Type2030){
		*p++ = 145; /* flags */
		p++; /* lead time */
		*p++ = 5; /* max kill */
		*p++ = 1; /* bt3 t7 timer */
		put32(p, 0xffff0000); /* kill ack */
		p += 4;
		put32(p, 0xffff0000); /* kill cts */
		p += 4;
		*p++ = 2; /* sample time */
		*p++ = 0xc; /* bt3 t2 timer */
		p += 2; /* bt4 reaction */
		for (i = 0; i < nelem(btcoex3wire); i++){
			put32(p, btcoex3wire[i]);
			p += 4;
		}
		p += 2; /* bt4 decision */
		put16(p, 0xff); /* valid */
		p += 2;
		put32(p, 0xf0); /* prio boost */
		p += 4;
		p++; /* reserved */
		p++; /* tx prio boost */
		p += 2; /* rx prio boost */
	}
	if((err = cmd(ctlr, 155, c, p-c)) != nil)
		return err;

	if(ctlr->family >= 7000)
		return nil;

	/* set BT priority */
	memset(c, 0, sizeof(c));
	p = c;

	*p++ = 0x6; /* init1 */
	*p++ = 0x7; /* init2 */
	*p++ = 0x2; /* periodic low1 */
	*p++ = 0x3; /* periodic low2 */
	*p++ = 0x4; /* periodic high1 */
	*p++ = 0x5; /* periodic high2 */
	*p++ = 0x6; /* dtim */
	*p++ = 0x8; /* scan52 */
	*p++ = 0xa; /* scan24 */
	p += 7; /* reserved */
	if((err = cmd(ctlr, 204, c, p-c)) != nil)
		return err;

	/* force BT state machine change */
	memset(c, 0, sizeof(c));
	p = c;

	*p++ = 1; /* open */
	*p++ = 1; /* type */
	p += 2; /* reserved */
	if((err = cmd(ctlr, 205, c, p-c)) != nil)
		return err;

	c[0] = 0; /* open */
	return cmd(ctlr, 205, c, p-c);
}

static char*
sendpagingcmd(Ctlr *ctlr)
{
	uchar c[3*4 + 4 + 32*4], *p;
	int i;

	p = c;
	put32(p, (3<<8) | (ctlr->fwmem.npage % FWBlockpages));
	p += 4;
	put32(p, FWPageshift + FWBlockshift);
	p += 4;
	put32(p, ctlr->fwmem.nblock);
	p += 4;

	put32(p, PCIWADDR(ctlr->fwmem.css) >> FWPageshift);
	p += 4;

	for(i = 0; i < ctlr->fwmem.nblock; i++){
		put32(p, PCIWADDR(ctlr->fwmem.block[i].p) >> FWPageshift);
		p += 4;
	}

	for(; i < 32; i++){
		put32(p, 0);
		p += 4;
	}

	return cmd(ctlr, 79 | (1<<8), c, p-c);
}

static char*
enablepaging(Ctlr *ctlr)
{
	FWSect *sect;
	int nsect;
	int i, j, o, n;

	if(ctlr->fwmem.css == nil)
		return nil;

	if(1){
		/* clear everything */
		memset(ctlr->fwmem.css, 0, FWPagesize);
		for(i = 0; i < ctlr->fwmem.nblock; i++)
			memset(ctlr->fwmem.block[i].p, 0, ctlr->fwmem.block[i].size);
	}

	if(ctlr->calib.done == 0){
		sect = ctlr->fw->init.sect;
		nsect = ctlr->fw->init.nsect;
	} else {
		sect = ctlr->fw->main.sect;
		nsect = ctlr->fw->main.nsect;
	}

	/* first CSS segment */
	for(i = 0; i < nsect; i++) {
		if(sect[i].addr == 0xAAAABBBB){
			i++;
			break;
		}
	}
	if(i+1 >= nsect)
		return "firmware misses CSS+paging sections";

	if(sect[i].size > FWPagesize)
		return "CSS section too big";
	if(sect[i+1].size > (ctlr->fwmem.npage << FWPageshift))
		return "paged section too big";

	memmove(ctlr->fwmem.css, sect[i].data, sect[i].size);

	for(j = 0, o = 0; o < sect[i+1].size; o += n, j++){
		n = sect[i+1].size - o;
		if(n > ctlr->fwmem.block[j].size)
			n = ctlr->fwmem.block[j].size;
		memmove(ctlr->fwmem.block[j].p, sect[i+1].data + o, n);
	}

	return sendpagingcmd(ctlr);
}

static int
readnvmsect1(Ctlr *ctlr, int type, void *data, int len, int off)
{
	uchar c[2+2+2+2], *p;
	char *err;

	p = c;
	*p++ = 0; // read op
	*p++ = 0; // target
	put16(p, type);
	p += 2;
	put16(p, off);
	p += 2;
	put16(p, len);
	p += 2;

	ctlr->nvm.off = -1;
	ctlr->nvm.ret = -1;
	ctlr->nvm.type = -1;
	ctlr->nvm.sts = -1;

	ctlr->nvm.buf = data;
	ctlr->nvm.len = len;

	if((err = cmd(ctlr, 136, c, p - c)) != nil){
		ctlr->nvm.buf = nil;
		ctlr->nvm.len = 0;
		print("readnvmsect: %s\n", err);
		return -1;
	}

	if(ctlr->nvm.ret < len)
		len = ctlr->nvm.ret;

	if(ctlr->nvm.sts != 0 || ctlr->nvm.off != off || (ctlr->nvm.type & 0xFF) != type)
		return -1;

	return len;
}

static int
readnvmsect(Ctlr *ctlr, int type, void *data, int len, int off)
{
	int n, r, o;

	for(o = 0; o < len; o += n){
		r = len - o;
		if(r > 256)
			r = 256;
		if((n = readnvmsect1(ctlr, type, (char*)data + o, r, o+off)) < 0)
			return -1;
		if(n < r){
			o += n;
			break;
		}
	}
	return o;
}

static char*
readnvmconfig(Ctlr *ctlr)
{
	uchar *ea = ctlr->edev->ea;
	uchar buf[8];
	uint u;
	char *err;

	if(readnvmsect(ctlr, 1, buf, 8, 0) != 8)
		return "can't read nvm version";

	ctlr->nvm.version = get16(buf);
	if (ctlr->family == 7000) {
		u = get16(buf + 2);

		ctlr->rfcfg.type = (u >> 4) & 3;
		ctlr->rfcfg.step = (u >> 2) & 3;
		ctlr->rfcfg.dash = (u >> 0) & 3;
		ctlr->rfcfg.pnum = (u >> 6) & 3;

		ctlr->rfcfg.txantmask = (u >> 8) & 15;
		ctlr->rfcfg.rxantmask = (u >> 12) & 15;

	} else {
		if(readnvmsect(ctlr, 12, buf, 8, 0) != 8)
			return "can't read nvm phy config";

		u = get32(buf);

		ctlr->rfcfg.type = (u >> 12) & 0xFFF;
		ctlr->rfcfg.step = (u >> 8) & 15;
		ctlr->rfcfg.dash = (u >> 4) & 15;
		ctlr->rfcfg.pnum = (u >> 6) & 3;

		ctlr->rfcfg.txantmask = (u >> 24) & 15;
		ctlr->rfcfg.rxantmask = (u >> 28) & 15;
	}
	if(ctlr->family >= 8000){
		if(readnvmsect(ctlr, 11, ea, Eaddrlen, 0x01<<1) != Eaddrlen){
			u32int a0, a1;

			if((err = niclock(ctlr)) != nil)
				return err;
			a0 = prphread(ctlr, 0xa03080);
			a1 = prphread(ctlr, 0xa03084);
			nicunlock(ctlr);

			ea[0] = a0 >> 24;
			ea[1] = a0 >> 16;
			ea[2] = a0 >> 8;
			ea[3] = a0 >> 0;
			ea[4] = a1 >> 8;
			ea[5] = a1 >> 0;
		}
	} else {
		readnvmsect(ctlr, 0, ea, Eaddrlen, 0x15<<1);
	}
	memmove(ctlr->edev->addr, ea, Eaddrlen);

	return nil;
}

static char*
sendtxantconfig(Ctlr *ctlr, uint val)
{
	uchar c[4];

	put32(c, val);
	return cmd(ctlr, 152, c, 4);
}

static char*
sendphyconfig(Ctlr *ctlr, u32int physku, u32int flowmask, u32int eventmask)
{
	uchar c[3*4];

	put32(c+0, physku);
	put32(c+4, flowmask);
	put32(c+8, eventmask);
	return cmd(ctlr, 106, c, 3*4);
}

static char*
setsmartfifo(Ctlr *ctlr, u32int state, int fullon)
{
	uchar c[4*(1 + 2 + 5*2 + 5*2)], *p;
	int i;

	memset(p = c, 0, sizeof(c));
	put32(p, state);
	p += 4;

	/* watermark long delay on */
	put32(p, 4096);
	p += 4;

	/* watermark full on */
	put32(p, 4096);
	p += 4;

	/* long delay timeouts */
	for(i = 0; i < 5*2; i++){
		put32(p, 1000000);
		p += 4;
	}

	/* full on timeouts */
	if(fullon){
		/* single unicast */
		put32(p, 320);
		p += 4;
		put32(p, 2016);
		p += 4;

		/* agg unicast */
		put32(p, 320);
		p += 4;
		put32(p, 2016);
		p += 4;

		/* mcast */
		put32(p, 2016);
		p += 4;
		put32(p, 10016);
		p += 4;

		/* ba */
		put32(p, 320);
		p += 4;
		put32(p, 2016);
		p += 4;

		/* tx re */
		put32(p, 320);
		p += 4;
		put32(p, 2016);
		p += 4;
	} else {
		for(i = 0; i < 5; i++){
			put32(p, 160);
			p += 4;
			put32(p, 400);
			p += 4;
		}
	}
	return cmd(ctlr, 209, c, p - c);
}

static char*
delstation(Ctlr *ctlr, Station *sta)
{
	uchar c[4], *p;
	char *err;

	if(sta->id < 0)
		return nil;

	memset(p = c, 0, sizeof(c));
	*p = sta->id;

	if((err = cmd(ctlr, 25, c, 4)) != nil)
		return err;

	sta->id = -1;
	return nil;
}

enum {
	StaTypeLink = 0,
	StaTypeGeneralPurpose,
	StaTypeMulticast,
	StaTypeTdlsLink,
	StaTypeAux,
};

static char*
setstation(Ctlr *ctlr, int id, int type, uchar addr[6], Station *sta)
{
	uchar c[Tcmdsize], *p;
	char *err;

	memset(p = c, 0, sizeof(c));

	*p++ = 0;			/* control (1 = update) */
	p++;				/* reserved */
	if(ctlr->family >= 7000){
		put16(p, 0xffff);
		p += 2;
		put32(p, ctlr->macid);
		p += 4;
	} else {
		p += 2;			/* reserved */
	}

	memmove(p, addr, 6);
	p += 8;

	*p++ = id;			/* sta id */

	if(ctlr->family >= 7000){
		*p++ = 1 << 1;		/* modify mask */
		p += 2;			/* reserved */

		put32(p, 0<<26 | 0<<28);
		p += 4;			/* station_flags */

		put32(p, 3<<26 | 3<<28);
		p += 4;			/* station_flags_mask */

		p++;			/* add_immediate_ba_tid */
		p++;			/* remove_immediate_ba_tid */
		p += 2;			/* add_immediate_ba_ssn */
		p += 2;			/* sleep_tx_count */
		p++;			/* sleep state flags */

		*p++ = (ctlr->fw->api[0] & (1<<30)) != 0 ? type : 0;		/* station_type */

		p += 2;			/* assoc id */

		p += 2;			/* beamform flags */

		put32(p, 1<<0);
		p += 4;			/* tfd_queue_mask */

		if(1){
			p += 2;		/* rx_ba_window */
			p++;		/* sp_length */
			p++;		/* uapsd_acs */
		}
	} else {
		p += 3;
		p += 2;			/* kflags */
		p++;			/* tcs2 */
		p++;			/* reserved */
		p += 5*2;		/* ttak */
		p++;			/* kid */
		p++;			/* reserved */
		p += 16;		/* key */
		if(ctlr->type != Type4965){
			p += 8;		/* tcs */
			p += 8;		/* rxmic */
			p += 8;		/* txmic */
		}
		p += 4;			/* htflags */
		p += 4;			/* mask */
		p += 2;			/* disable tid */
		p += 2;			/* reserved */
		p++;			/* add ba tid */
		p++;			/* del ba tid */
		p += 2;			/* add ba ssn */
		p += 4;			/* reserved */
	}

	if((err = cmd(ctlr, 24, c, p - c)) != nil)
		return err;

	sta->id = id;

	return nil;
}

static char*
setphycontext(Ctlr *ctlr, int amr)
{
	uchar c[Tcmdsize], *p;
	int phyid;
	char *err;

	phyid = ctlr->phyid;
	if(phyid < 0){
		if(amr == CmdRemove)
			return nil;
		amr = CmdAdd;
		phyid = 0;
	} else if(amr == CmdAdd)
		amr = CmdModify;

	memset(p = c, 0, sizeof(c));
	put32(p, phyid);	// id and color
	p += 4;
	put32(p, amr);
	p += 4;
	put32(p, 0);		// apply time 0 = immediate
	p += 4;
	put32(p, 0);		// tx param color ????
	p += 4;

	*p++ = (ctlr->rxflags & RFlag24Ghz) != 0;
	*p++ = ctlr->channel;	// channel number
	*p++ = 0;		// channel width (20MHz<<val)
	*p++ = 0;		// pos1 below 

	put32(p, ctlr->rfcfg.txantmask);
	p += 4;
	put32(p, ctlr->rfcfg.rxantmask<<1 | (1<<10) | (1<<12));
	p += 4;
	put32(p, 0);		// acquisition_data ????
	p += 4;
	put32(p, 0);		// dsp_cfg_flags
	p += 4;

	if((err = cmd(ctlr, 8, c, p - c)) != nil)
		return err;

	if(amr == CmdRemove)
		phyid = -1;
	ctlr->phyid = phyid;
	return nil;
}

static u32int
reciprocal(u32int v)
{
	return v != 0 ? 0xFFFFFFFFU / v : 0;
}

static char*
setmaccontext(Ether *edev, Ctlr *ctlr, int amr, Wnode *bss)
{
	uchar c[4+4 + 4+4 + 8+8 + 4+4+4+4+4+4+4 + 5*8 + 12*4], *p;
	int macid, i;
	char *err;

	macid = ctlr->macid;
	if(macid < 0){
		if(amr == CmdRemove)
			return nil;
		amr = CmdAdd;
		macid = 0;
	} else if(amr == CmdAdd)
		amr = CmdModify;

	memset(p = c, 0, sizeof(c));
	put32(p, macid);
	p += 4;
	put32(p, amr);
	p += 4;

	put32(p, 5);	// mac type 5 = bss
	p += 4;

	put32(p, 0);	// tsf id ???
	p += 4;

	memmove(p, edev->ea, 6);
	p += 8;

	memmove(p, ctlr->bssid, 6);
	p += 8;

	put32(p, bss == nil? 0xF : (bss->validrates & 0xF));
	p += 4;
	put32(p, bss == nil? 0xFF : (bss->validrates >> 4));
	p += 4;

	put32(p, 0);	// protection flags
	p += 4;

	put32(p, ctlr->rxflags & RFlagShPreamble);
	p += 4;
	put32(p, ctlr->rxflags & RFlagShSlot);
	p += 4;
	put32(p, ctlr->rxfilter);
	p += 4;

	put32(p, 0);	// qos flags
	p += 4;

	for(i = 0; i < 4; i++){
		put16(p, 0x07);		// cw_min
		p += 2;
		put16(p, 0x0f);		// cw_max
		p += 2;
		*p++ = 2;		// aifsn
		*p++ = (1<<i);		// fifos_mask
		put16(p, 102*32);	// edca_txop
		p += 2;
	}
	p += 8;

	if(bss != nil){
		int dtimoff = bss->ival * (int)bss->dtimcount * 1024;

		/* is assoc */
		put32(p, bss->aid != 0);
		p += 4;

		/* dtim time (system time) */
		put32(p, bss->rs + dtimoff);
		p += 4;

		/* dtim tsf */
		put64(p, bss->ts + dtimoff);
		p += 8;

		/* beacon interval */
		put32(p, bss->ival);
		p += 4;
		put32(p, reciprocal(bss->ival));
		p += 4;

		/* dtim interval */
		put32(p, bss->ival * bss->dtimperiod);
		p += 4;
		put32(p, reciprocal(bss->ival * bss->dtimperiod));
		p += 4;

		/* listen interval */
		put32(p, 10);
		p += 4;

		/* assoc id */
		put32(p, bss->aid & 0x3fff);
		p += 4;

		/* assoc beacon arrive time */
		put32(p, bss->rs);
		p += 4;
	}
	USED(p);

	if((err = cmd(ctlr, 40, c, sizeof(c))) != nil)
		return err;

	if(amr == CmdRemove)
		macid = -1;
	ctlr->macid = macid;

	return nil;
}

static char*
setbindingcontext(Ctlr *ctlr, int amr)
{
	uchar c[Tcmdsize], *p;
	int bindid;
	char *err;
	int i;

	bindid = ctlr->bindid;
	if(bindid < 0){
		if(amr == CmdRemove)
			return nil;
		amr = CmdAdd;
		bindid = 0;
	} else if(amr == CmdAdd)
		amr = CmdModify;

	if(ctlr->phyid < 0)
		return "setbindingcontext: no phyid";
	if(ctlr->macid < 0)
		return "setbindingcontext: no macid";

	p = c;
	put32(p, bindid);
	p += 4;
	put32(p, amr);
	p += 4;

	i = 0;
	if(amr != CmdRemove){
		put32(p, ctlr->macid);
		p += 4;
		i++;
	}
	for(; i < 3; i++){
		put32(p, -1);
		p += 4;
	}
	put32(p, ctlr->phyid);
	p += 4;

	if((err = cmd(ctlr, 43, c, p - c)) != nil)
		return err;

	if(amr == CmdRemove)
		bindid = -1;
	ctlr->bindid = bindid;
	return nil;
}

static char*
settimeevent(Ctlr *ctlr, int amr, int ival)
{
	int timeid, duration, delay;
	uchar c[9*4], *p;
	char *err;

	if(ival){
		duration = ival*2;
		delay = ival/2;
	} else {
		duration = 1024;
		delay = 0;
	}

	timeid = ctlr->timeid;
	if(timeid < 0){
		if(amr == CmdRemove)
			return nil;
		amr = CmdAdd;
		timeid = 0;
	} else {
		if(amr == CmdAdd)
			amr = CmdModify;
	}
	if(ctlr->macid < 0)
		return "no mac id set";

	memset(p = c, 0, sizeof(c));
	put32(p, ctlr->macid);
	p += 4;
	put32(p, amr);
	p += 4;
	put32(p, timeid);
	p += 4;

	put32(p, 0);	// apply time
	p += 4;
	put32(p, delay);
	p += 4;
	put32(p, 0);	// depends on
	p += 4;
	put32(p, 1);	// interval
	p += 4;
	put32(p, duration);
	p += 4;
	*p++ = 1;	// repeat
	*p++ = 0;	// max frags
	put16(p, 1<<0 | 1<<1 | 1<<11);	// policy
	p += 2;

	if((err =  cmd(ctlr, 41, c, p - c)) != nil)
		return err;

	if(amr == CmdRemove)
		ctlr->timeid = -1;

	return nil;
}


static char*
setbindingquotas(Ctlr *ctlr, int bindid)
{
	uchar c[4*(3*4)], *p;
	int i;

	i = 0;
	p = c;

	if(bindid != -1){
		put32(p, bindid);
		p += 4;
		put32(p, 128);
		p += 4;
		put32(p, 0);
		p += 4;
		i++;
	}
	for(; i < 4; i++){
		put32(p, -1);
		p += 4;
		put32(p, 0);
		p += 4;
		put32(p, 0);
		p += 4;
	}

	return cmd(ctlr, 44, c, p - c);
}

static char*
setmcastfilter(Ctlr *ctlr)
{
	uchar *p;
	char *err;
	Block *b;

	b = allocb(4+6+2);
	p = b->rp;

	*p++ = 1;	// filter own
	*p++ = 0;	// port id
	*p++ = 0;	// count
	*p++ = 1;	// pass all

	memmove(p, ctlr->bssid, 6);
	p += 6;
	*p++ = 0;
	*p++ = 0;

	b->wp = p;
	if((err = qcmd(ctlr, 4, 208, nil, 0, b)) != nil){
		freeb(b);
		return err;
	}
	return flushq(ctlr, 4);
}

static char*
setmacpowermode(Ctlr *ctlr)
{
	uchar c[4 + 2+2 + 4+4+4+4 + 1+1 + 2+2 + 1+1+1+1 + 1+1+1+1 + 1+1], *p;

	p = c;
	put32(p, ctlr->macid);
	p += 4;

	put16(p, 0);	// flags
	p += 2;
	put16(p, 5);	// keep alive seconds
	p += 2;

	put32(p, 0);	// rx data timeout
	p += 4;
	put32(p, 0);	// tx data timeout
	p += 4;
	put32(p, 0);	// rx data timeout uapsd
	p += 4;
	put32(p, 0);	// tx data timeout uapsd
	p += 4;

	*p++ = 0;	// lprx rssi threshold
	*p++ = 0;	// skip dtim periods

	put16(p, 0);	// snooze interval
	p += 2;
	put16(p, 0);	// snooze window
	p += 2;

	*p++ = 0;	// snooze step
	*p++ = 0;	// qndp tid
	*p++ = 0;	// uapsd ac flags
	*p++ = 0;	// uapsd max sp

	*p++ = 0;	// heavy tx thld packets
	*p++ = 0;	// heavy rx thld packets

	*p++ = 0;	// heavy tx thld percentage
	*p++ = 0;	// heavy rx thld percentage

	*p++ = 0;	// limited ps threshold
	*p++ = 0;	// reserved

	return cmd(ctlr, 169, c, p - c);
}

static char*
disablebeaconfilter(Ctlr *ctlr)
{
	uchar c[11*4];

	memset(c, 0, sizeof(c));
	return cmd(ctlr, 210, c, 11*4);
}

static char*
updatedevicepower(Ctlr *ctlr)
{
	uchar c[4];

	memset(c, 0, sizeof(c));
	put16(c, 0<<13 | 1<<0);	// cont active off, pm enable

	return cmd(ctlr, 119, c, 4);
}

static char*
postboot7000(Ctlr *ctlr)
{
	char *err;

	if(ctlr->calib.done == 0){
		if((err = readnvmconfig(ctlr)) != nil)
			return err;

		if((err = setsmartfifo(ctlr, 3, 0)) != nil)
			return err;
	}

	if((err = sendtxantconfig(ctlr, ctlr->rfcfg.txantmask)) != nil)
		return err;

	if(ctlr->calib.done == 0){
		if((err = sendphyconfig(ctlr,
			ctlr->fw->physku,
			ctlr->fw->init.defcalib.flowmask,
			ctlr->fw->init.defcalib.eventmask)) != nil)
			return err;

		/* wait to collect calibration records */
		if(irqwait(ctlr, Ierr, 2000))
			return "calibration failed";

		if(ctlr->calib.done == 0){
			print("iwl: no calibration results\n");
			ctlr->calib.done = 1;
		}
	} else {
		Block *b;
		int i;

		for(i = 0; i < nelem(ctlr->calib.cmd); i++){
			if((b = ctlr->calib.cmd[i]) == nil)
				continue;
			b = copyblock(b, BLEN(b));
			if((qcmd(ctlr, 4, 108, nil, 0, b)) != nil){
				freeb(b);
				return err;
			}
			if((err = flushq(ctlr, 4)) != nil)
				return err;
		}

		if((err = sendphyconfig(ctlr,
			ctlr->fw->physku,
			ctlr->fw->main.defcalib.flowmask,
			ctlr->fw->main.defcalib.eventmask)) != nil)
			return err;

		if((err = sendbtcoexadv(ctlr)) != nil)
			return err;

		if((err = updatedevicepower(ctlr)) != nil){
			print("can't update device power: %s\n", err);
			return err;
		}
		if((err = sendmccupdate(ctlr, "ZZ")) != nil){
			print("can't disable beacon filter: %s\n", err);
			return err;
		}
		if((err = disablebeaconfilter(ctlr)) != nil){
			print("can't disable beacon filter: %s\n", err);
			return err;
		}
	}

	return nil;
}

static char*
postboot6000(Ctlr *ctlr)
{
	uchar c[Tcmdsize];
	char *err;

	/* disable wimax coexistance */
	memset(c, 0, sizeof(c));
	if((err = cmd(ctlr, 90, c, 4+4*16)) != nil)
		return err;

	if(ctlr->type != Type5150){
		/* calibrate crystal */
		memset(c, 0, sizeof(c));
		c[0] = 15;	/* code */
		c[1] = 0;	/* group */
		c[2] = 1;	/* ngroup */
		c[3] = 1;	/* isvalid */
		c[4] = ctlr->eeprom.crystal;
		c[5] = ctlr->eeprom.crystal>>16;
		/* for some reason 8086:4238 needs a second try */
		if(cmd(ctlr, 176, c, 8) != nil && (err = cmd(ctlr, 176, c, 8)) != nil)
			return err;
	}

	if(ctlr->calib.done == 0){
		/* query calibration (init firmware) */
		memset(c, 0, sizeof(c));
		put32(c + 0*(5*4) + 0, 0xffffffff);
		put32(c + 0*(5*4) + 4, 0xffffffff);
		put32(c + 0*(5*4) + 8, 0xffffffff);
		put32(c + 2*(5*4) + 0, 0xffffffff);
		if((err = cmd(ctlr, 101, c, (((2*(5*4))+4)*2)+4)) != nil)
			return err;

		/* wait to collect calibration records */
		if(irqwait(ctlr, Ierr, 2000))
			return "calibration failed";

		if(ctlr->calib.done == 0){
			print("iwl: no calibration results\n");
			ctlr->calib.done = 1;
		}
	} else {
		static uchar cmds[] = {8, 9, 11, 17, 16};
		int q;

		/* send calibration records (runtime firmware) */
		for(q=0; q<nelem(cmds); q++){
			Block *b;
			int i;

			i = cmds[q];
			if(i == 8 && ctlr->type != Type5150 && ctlr->type != Type2030 &&
				ctlr->type != Type2000)
				continue;
			if(i == 17 && (ctlr->type >= Type6000 || ctlr->type == Type5150) &&
				ctlr->type != Type2030 && ctlr->type != Type2000)
				continue;

			if((b = ctlr->calib.cmd[i]) == nil)
				continue;
			b = copyblock(b, BLEN(b));
			if((err = qcmd(ctlr, 4, 176, nil, 0, b)) != nil){
				freeb(b);
				return err;
			}
			if((err = flushq(ctlr, 4)) != nil)
				return err;
		}

		/* temperature sensor offset */
		switch (ctlr->type){
		case Type6005:
			memset(c, 0, sizeof(c));
			c[0] = 18;
			c[1] = 0;
			c[2] = 1;
			c[3] = 1;
			put16(c + 4, 2700);
			if((err = cmd(ctlr, 176, c, 4+2+2)) != nil)
				return err;
			break;

		case Type2030:
		case Type2000:
			memset(c, 0, sizeof(c));
			c[0] = 18;
			c[1] = 0;
			c[2] = 1;
			c[3] = 1;
			if(ctlr->eeprom.rawtemp != 0){
				put16(c + 4, ctlr->eeprom.temp);
				put16(c + 6, ctlr->eeprom.rawtemp);
			} else{
				put16(c + 4, 2700);
				put16(c + 6, 2700);
			}
			put16(c + 8, ctlr->eeprom.volt);
			if((err = cmd(ctlr, 176, c, 4+2+2+2+2)) != nil)
				return err;
			break;
		}

		if(ctlr->type == Type6005 || ctlr->type == Type6050){
			/* runtime DC calibration */
			memset(c, 0, sizeof(c));
			put32(c + 0*(5*4) + 0, 0xffffffff);
			put32(c + 0*(5*4) + 4, 1<<1);
			if((err = cmd(ctlr, 101, c, (((2*(5*4))+4)*2)+4)) != nil)
				return err;
		}

		if((err = sendtxantconfig(ctlr, ctlr->rfcfg.txantmask & 7)) != nil)
			return err;

		if(ctlr->type == Type2030){
			if((err = sendbtcoexadv(ctlr)) != nil)
				return err;
		}
	}
	return nil;
}

static void
initqueue(Ctlr *ctlr, int qid, int fifo, int chainmode, int window)
{
	csr32w(ctlr, HbusTargWptr, (qid << 8) | 0);

	if(ctlr->family >= 7000 || ctlr->type != Type4965){
		if(ctlr->family >= 7000)
			prphwrite(ctlr, SchedQueueStatus + qid*4, 1 << 19);

		if(chainmode)
			prphwrite(ctlr, SchedQChainSel, prphread(ctlr, SchedQChainSel) | (1<<qid));
		else
			prphwrite(ctlr, SchedQChainSel, prphread(ctlr, SchedQChainSel) & ~(1<<qid));

		prphwrite(ctlr, SchedAggrSel, prphread(ctlr, SchedAggrSel) & ~(1<<qid));

		prphwrite(ctlr, SchedQueueRdptr + qid*4, 0);

		/* Set scheduler window size and frame limit. */
		memwrite(ctlr, ctlr->sched.base + SchedCtxOff + qid*8, 0);
		memwrite(ctlr, ctlr->sched.base + SchedCtxOff + qid*8 + 4, window<<16 | window);

		if(ctlr->family >= 7000){
			prphwrite(ctlr, SchedQueueStatus + qid*4, 0x017f0018 | fifo);
		} else {
			prphwrite(ctlr, SchedQueueStatus + qid*4, 0x00ff0018 | fifo);
		}
	} else {
		if(chainmode)
			prphwrite(ctlr, SchedQChainSel4965, prphread(ctlr, SchedQChainSel4965) | (1<<qid));
		else
			prphwrite(ctlr, SchedQChainSel4965, prphread(ctlr, SchedQChainSel4965) & ~(1<<qid));

		prphwrite(ctlr, SchedQueueRdptr4965 + qid*4, 0);

		/* Set scheduler window size and frame limit. */
		memwrite(ctlr, ctlr->sched.base + SchedCtxOff4965 + qid*8, window);
		memwrite(ctlr, ctlr->sched.base + SchedCtxOff4965 + qid*8 + 4, window<<16);

		prphwrite(ctlr, SchedQueueStatus4965 + qid*4, 0x0007fc01 | fifo<<1);
	}
}

static char*
postboot(Ctlr *ctlr)
{
	uint ctxoff, ctxlen, dramaddr;
	char *err;
	int i, f;

	if((err = niclock(ctlr)) != nil)
		return err;

	if(ctlr->family >= 7000 || ctlr->type != Type4965){
		dramaddr = SchedDramAddr;
		ctxoff = SchedCtxOff;
		ctxlen = (SchedTransTblOff + 2*ctlr->ntxq) - ctxoff;
	} else {
		dramaddr = SchedDramAddr4965;
		ctxoff = SchedCtxOff4965;
		ctxlen = SchedCtxLen4965;
	}

	ctlr->sched.base = prphread(ctlr, SchedSramAddr);
	for(i=0; i < ctxlen; i += 4)
		memwrite(ctlr, ctlr->sched.base + ctxoff + i, 0);

	prphwrite(ctlr, dramaddr, PCIWADDR(ctlr->sched.s)>>10);

	if(ctlr->family >= 7000) {
		prphwrite(ctlr, SchedEnCtrl, 0);
		prphwrite(ctlr, SchedChainExtEn, 0);
	}

	for(i = 0; i < nelem(ctlr->tx); i++){
		if(i == 4 && ctlr->family < 7000 && ctlr->type == Type4965)
			f = 4;
		else {
			static char qid2fifo[] = {
				 3, 2, 1, 0, 7, 5, 6,
			};
			f = qid2fifo[i];
		}
		initqueue(ctlr, i, f, i != 4 && ctlr->type != Type4965, 64);
	}

	/* Enable interrupts for all queues. */
	if(ctlr->family >= 7000){
		prphwrite(ctlr, SchedEnCtrl, 1 << 4);
	} else if(ctlr->type != Type4965) {
		prphwrite(ctlr, SchedIntrMask, (1<<ctlr->ntxq)-1);
	} else {
		prphwrite(ctlr, SchedIntrMask4965, (1<<ctlr->ntxq)-1);
	}

	/* Identify TX FIFO rings (0-7). */
	if(ctlr->family >= 7000 || ctlr->type != Type4965){
		prphwrite(ctlr, SchedTxFact, 0xff);
	} else {
		prphwrite(ctlr, SchedTxFact4965, 0xff);
	}

	/* Enable DMA channels */
	for(i = 0; i < ctlr->ndma; i++)
		csr32w(ctlr, FhTxConfig + i*32, FhTxConfigDmaEna | FhTxConfigDmaCreditEna);

	/* Auto Retry Enable */
	csr32w(ctlr, FhTxChicken, csr32r(ctlr, FhTxChicken) | 2);

	nicunlock(ctlr);

	if((err = enablepaging(ctlr)) != nil){
		ctlr->calib.done = 0;
		return err;
	}

	if(ctlr->family >= 7000)
		return postboot7000(ctlr);
	else if(ctlr->type != Type4965)
		return postboot6000(ctlr);

	return nil;
}

static char*
loadfirmware1(Ctlr *ctlr, u32int dst, uchar *data, int size)
{
	enum { Maxchunk = 0x20000 };
	uchar *dma;
	char *err;

	while(size > Maxchunk){
		if((err = loadfirmware1(ctlr, dst, data, Maxchunk)) != nil)
			return err;
		size -= Maxchunk;
		data += Maxchunk;
		dst += Maxchunk;
	}

	dma = mallocalign(size, 16, 0, 0);
	if(dma == nil)
		return "no memory for dma";
	memmove(dma, data, size);
	coherence();
	
	if(ctlr->family >= 7000 && dst >= 0x40000 && dst < 0x57fff)
		prphwrite(ctlr, LmpmChick, prphread(ctlr, LmpmChick) | ExtAddr);

	if((err = niclock(ctlr)) != nil){
		free(dma);
		return err;
	}
	csr32w(ctlr, FhTxConfig + 9*32, 0);
	csr32w(ctlr, FhSramAddr + 9*4, dst);
	csr32w(ctlr, FhTfbdCtrl0 + 9*8, PCIWADDR(dma));
	csr32w(ctlr, FhTfbdCtrl1 + 9*8, size);
	csr32w(ctlr, FhTxBufStatus + 9*32,
		(1<<FhTxBufStatusTbNumShift) |
		(1<<FhTxBufStatusTbIdxShift) |
		FhTxBufStatusTfbdValid);
	csr32w(ctlr, FhTxConfig + 9*32, FhTxConfigDmaEna | FhTxConfigCirqHostEndTfd);
	nicunlock(ctlr);

	err = nil;
	if(irqwait(ctlr, Ifhtx|Ierr, 5000) != Ifhtx)
		err = "dma error / timeout";

	if(ctlr->family >= 7000 && dst >= 0x40000 && dst < 0x57fff)
		prphwrite(ctlr, LmpmChick, prphread(ctlr, LmpmChick) & ~ExtAddr);

	free(dma);

	return err;
}

static char*
setloadstatus(Ctlr *ctlr, u32int val)
{
	char *err;

	if((err = niclock(ctlr)) != nil)
		return err;
	csr32w(ctlr, UcodeLoadStatus, val);
	nicunlock(ctlr);
	return nil;
}

static char*
loadsections(Ctlr *ctlr, FWSect *sect, int nsect)
{
	int i, num;
	char *err;

	if(ctlr->family >= 8000){
		if((err = niclock(ctlr)) != nil)
			return err;
		prphwrite(ctlr, ReleaseCpuReset, CpuResetBit);
		nicunlock(ctlr);
	}

	num = 0;
	for(i = 0; i < nsect; i++){
		if(sect[i].addr == 0xAAAABBBB)
			break;
		if(sect[i].addr == 0xFFFFCCCC)
			num = 16;
		else {
			if(sect[i].data == nil || sect[i].size == 0)
				return "bad load section";
			if((err = loadfirmware1(ctlr, sect[i].addr, sect[i].data, sect[i].size)) != nil)
				return err;
			num++;
		}
		if(ctlr->family >= 8000
		&& (err = setloadstatus(ctlr, (1ULL << num)-1)) != nil)
			return err;
	}
	return nil;
}

static char*
ucodestart(Ctlr *ctlr)
{
	if(ctlr->family >= 8000)
		return setloadstatus(ctlr, -1);
	csr32w(ctlr, Reset, 0);
	return nil;
}

static char*
boot(Ctlr *ctlr)
{
	int i, n, size;
	uchar *p, *dma;
	FWImage *fw;
	char *err;

	fw = ctlr->fw;

	if(fw->boot.text.size == 0){
		if(ctlr->calib.done == 0){
			if((err = loadsections(ctlr, fw->init.sect, fw->init.nsect)) != nil)
				return err;
			if((err = ucodestart(ctlr)) != nil)
				return err;
			if(irqwait(ctlr, Ierr|Ialive, 5000) != Ialive)
				return "init firmware boot failed";
			if((err = postboot(ctlr)) != nil)
				return err;
			if((err = reset(ctlr)) != nil)
				return err;
		}
		if((err = loadsections(ctlr, fw->main.sect, fw->main.nsect)) != nil)
			return err;
		if((err= ucodestart(ctlr)) != nil)
			return err;
		if(irqwait(ctlr, Ierr|Ialive, 5000) != Ialive)
			return "main firmware boot failed";
		return postboot(ctlr);
	}

	if(ctlr->family >= 7000)
		return "wrong firmware image";

	size = ROUND(fw->init.data.size, 16) + ROUND(fw->init.text.size, 16);
	dma = mallocalign(size, 16, 0, 0);
	if(dma == nil)
		return "no memory for dma";

	if((err = niclock(ctlr)) != nil){
		free(dma);
		return err;
	}

	p = dma;
	memmove(p, fw->init.data.data, fw->init.data.size);
	coherence();
	prphwrite(ctlr, BsmDramDataAddr, PCIWADDR(p) >> 4);
	prphwrite(ctlr, BsmDramDataSize, fw->init.data.size);
	p += ROUND(fw->init.data.size, 16);
	memmove(p, fw->init.text.data, fw->init.text.size);
	coherence();
	prphwrite(ctlr, BsmDramTextAddr, PCIWADDR(p) >> 4);
	prphwrite(ctlr, BsmDramTextSize, fw->init.text.size);

	nicunlock(ctlr);
	if((err = niclock(ctlr)) != nil){
		free(dma);
		return err;
	}

	p = fw->boot.text.data;
	n = fw->boot.text.size/4;
	for(i=0; i<n; i++, p += 4)
		prphwrite(ctlr, BsmSramBase+i*4, get32(p));

	prphwrite(ctlr, BsmWrMemSrc, 0);
	prphwrite(ctlr, BsmWrMemDst, 0);
	prphwrite(ctlr, BsmWrDwCount, n);

	prphwrite(ctlr, BsmWrCtrl, 1<<31);

	for(i=0; i<1000; i++){
		if((prphread(ctlr, BsmWrCtrl) & (1<<31)) == 0)
			break;
		delay(10);
	}
	if(i == 1000){
		nicunlock(ctlr);
		free(dma);
		return "bootcode timeout";
	}

	prphwrite(ctlr, BsmWrCtrl, 1<<30);
	nicunlock(ctlr);

	csr32w(ctlr, Reset, 0);
	if(irqwait(ctlr, Ierr|Ialive, 5000) != Ialive){
		free(dma);
		return "boot firmware boot failed";
	}
	free(dma);

	size = ROUND(fw->main.data.size, 16) + ROUND(fw->main.text.size, 16);
	dma = mallocalign(size, 16, 0, 0);
	if(dma == nil)
		return "no memory for dma";
	if((err = niclock(ctlr)) != nil){
		free(dma);
		return err;
	}
	p = dma;
	memmove(p, fw->main.data.data, fw->main.data.size);
	coherence();
	prphwrite(ctlr, BsmDramDataAddr, PCIWADDR(p) >> 4);
	prphwrite(ctlr, BsmDramDataSize, fw->main.data.size);
	p += ROUND(fw->main.data.size, 16);
	memmove(p, fw->main.text.data, fw->main.text.size);
	coherence();
	prphwrite(ctlr, BsmDramTextAddr, PCIWADDR(p) >> 4);
	prphwrite(ctlr, BsmDramTextSize, fw->main.text.size | (1<<31));
	nicunlock(ctlr);

	if(irqwait(ctlr, Ierr|Ialive, 5000) != Ialive){
		free(dma);
		return "main firmware boot failed";
	}
	free(dma);
	return postboot(ctlr);
}

static int
txqready(void *arg)
{
	TXQ *q = arg;
	return q->n < Ntxqmax;
}

static char*
qcmd(Ctlr *ctlr, uint qid, uint code, uchar *data, int size, Block *block)
{
	int hdrlen;
	Block *bcmd;
	uchar *d, *c;
	TXQ *q;

	assert(qid < ctlr->ntxq);

	if(code & 0xFF00)
		hdrlen = 8;
	else
		hdrlen = 4;

	if(hdrlen+size > Tcmdsize)
		bcmd = allocb(hdrlen + size);
	else
		bcmd = nil;

	ilock(ctlr);
	q = &ctlr->tx[qid];
	while(q->n >= Ntxqmax && !ctlr->broken){
		iunlock(ctlr);
		qlock(q);
		if(!waserror()){
			tsleep(q, txqready, q, 5);
			poperror();
		}
		qunlock(q);
		ilock(ctlr);
	}
	if(ctlr->broken){
		iunlock(ctlr);
		return "qcmd: broken";
	}
	q->n++;
	q->lastcmd = code;

	q->b[q->i] = block;
	if(bcmd != nil){
		bcmd->next = q->b[q->i];
		q->b[q->i] = bcmd;

		c = bcmd->rp;
		bcmd->wp = c + hdrlen + size;
	} else {
		c = q->c + q->i * Tcmdsize;
	}

	/* build command */
	if(hdrlen == 8){
		c[0] = code;
		c[1] = code>>8;	/* group id */
		c[2] = q->i;
		c[3] = qid;
		put16(c+4, size);
		c[6] = 0;
		c[7] = code>>16;
	} else {
		c[0] = code;
		c[1] = 0;	/* flags */
		c[2] = q->i;
		c[3] = qid;
	}
	if(size > 0)
		memmove(c+hdrlen, data, size);
	size += hdrlen;

	/* build descriptor */
	d = q->d + q->i * Tdscsize;
	*d++ = 0;
	*d++ = 0;
	*d++ = 0;
	*d++ = 1 + (block != nil); /* nsegs */
	put32(d, PCIWADDR(c));	d += 4;
	put16(d, size << 4); d += 2;
	if(block != nil){
		size = BLEN(block);
		put32(d, PCIWADDR(block->rp)); d += 4;
		put16(d, size << 4);
	}

	coherence();

	q->i = (q->i+1) % Ntx;
	csr32w(ctlr, HbusTargWptr, (qid<<8) | q->i);

	iunlock(ctlr);

	return nil;
}

static int
txqempty(void *arg)
{
	TXQ *q = arg;
	return q->n == 0;
}

static char*
flushq(Ctlr *ctlr, uint qid)
{
	TXQ *q;
	int i;

	q = &ctlr->tx[qid];
	qlock(q);
	for(i = 0; i < 200 && !ctlr->broken; i++){
		if(txqempty(q)){
			qunlock(q);
			return nil;
		}
		if(!waserror()){
			tsleep(q, txqempty, q, 10);
			poperror();
		}
	}
	qunlock(q);
	if(ctlr->broken)
		return "flushq: broken";
	return "flushq: timeout";
}

static char*
cmd(Ctlr *ctlr, uint code, uchar *data, int size)
{
	char *err;

	if(0) print("cmd %ud\n", code);
	if((err = qcmd(ctlr, 4, code, data, size, nil)) != nil)
		return err;
	return flushq(ctlr, 4);
}

static void
setled(Ctlr *ctlr, int which, int on, int off)
{
	uchar c[8];

	if(ctlr->family >= 7000)
		return;	// TODO

	csr32w(ctlr, Led, csr32r(ctlr, Led) & ~LedBsmCtrl);

	memset(c, 0, sizeof(c));
	put32(c, 10000);
	c[4] = which;
	c[5] = on;
	c[6] = off;
	cmd(ctlr, 72, c, sizeof(c));
}

static char*
rxoff7000(Ether *edev, Ctlr *ctlr)
{
	char *err;

	if((err = settimeevent(ctlr, CmdRemove, 0)) != nil){
		print("can't remove timeevent: %s\n", err);
		return err;
	}
	if((err = setsmartfifo(ctlr, 2, 0)) != nil){
		print("setsmartfifo: %s\n", err);
		return err;
	}
	if((err = setbindingquotas(ctlr, -1)) != nil){
		print("can't disable quotas: %s\n", err);
		return err;
	}
	if((err = setbindingcontext(ctlr, CmdRemove)) != nil){
		print("removing bindingcontext: %s\n", err);
		return err;
	}
	if((err = setmaccontext(edev, ctlr, CmdRemove, nil)) != nil){
		print("removing maccontext: %s\n", err);
		return err;
	}
	if((err = setphycontext(ctlr, CmdRemove)) != nil){
		print("setphycontext: %s\n", err);
		return err;
	}
	return nil;
}

static char*
rxon7000(Ether *edev, Ctlr *ctlr)
{
	char *err;

	if((err = setphycontext(ctlr, CmdAdd)) != nil){
		print("setphycontext: %s\n", err);
		return err;
	}
	if((err = setmaccontext(edev, ctlr, CmdAdd, nil)) != nil){
		print("setmaccontext: %s\n", err);
		return err;
	}
	if((err = setbindingcontext(ctlr, CmdAdd)) != nil){
		print("removing bindingcontext: %s\n", err);
		return err;
	}
	if((err = setsmartfifo(ctlr, 1, ctlr->aid != 0)) != nil){
		print("setsmartfifo: %s\n", err);
		return err;
	}
	if((err = setmcastfilter(ctlr)) != nil){
		print("can't set mcast filter: %s\n", err);
		return err;
	}
	if((err = setmacpowermode(ctlr)) != nil){
		print("can't set mac power: %s\n", err);
		return err;
	}
	if((err = setbindingquotas(ctlr, ctlr->bindid)) != nil){
		print("can't set binding quotas: %s\n", err);
		return err;
	}
	return nil;
}

static char*
rxon6000(Ether *edev, Ctlr *ctlr)
{
	uchar c[Tcmdsize], *p;
	char *err;

	memset(p = c, 0, sizeof(c));
	memmove(p, edev->ea, 6); p += 8;	/* myaddr */
	memmove(p, ctlr->bssid, 6); p += 8;	/* bssid */
	memmove(p, edev->ea, 6); p += 8;	/* wlap */
	*p++ = 3;				/* mode (STA) */
	*p++ = 0;				/* air (?) */
	/* rxchain */
	put16(p, ((ctlr->rfcfg.rxantmask & 7)<<1) | (2<<10) | (2<<12));
	p += 2;
	*p++ = 0xff;				/* ofdm mask (not yet negotiated) */
	*p++ = 0x0f;				/* cck mask (not yet negotiated) */
	put16(p, ctlr->aid & 0x3fff);
	p += 2;					/* aid */
	put32(p, ctlr->rxflags);
	p += 4;
	put32(p, ctlr->rxfilter);
	p += 4;
	*p++ = ctlr->channel;
	p++;					/* reserved */
	*p++ = 0xff;				/* ht single mask */
	*p++ = 0xff;				/* ht dual mask */
	if(ctlr->type != Type4965){
		*p++ = 0xff;			/* ht triple mask */
		p++;				/* reserved */
		put16(p, 0); p += 2;		/* acquisition */
		p += 2;				/* reserved */
	}
	if((err = cmd(ctlr, 16, c, p - c)) != nil){
		print("rxon6000: %s\n", err);
		return err;
	}
	return nil;
}

static char*
rxon(Ether *edev, Wnode *bss)
{
	Ctlr *ctlr = edev->ctlr;
	char *err;

	if(ctlr->family >= 7000){
		delstation(ctlr, &ctlr->bss);
		delstation(ctlr, &ctlr->bcast);
		if((err = rxoff7000(edev, ctlr)) != nil)
			goto Out;
	}

	ctlr->rxfilter = FilterNoDecrypt | FilterMulticast | FilterBeacon;
	if(ctlr->family >= 7000)
		ctlr->rxfilter |= FilterNoDecryptMcast;
	if(ctlr->prom)
		ctlr->rxfilter |= FilterPromisc;

	ctlr->rxflags =  RFlagTSF | RFlagCTSToSelf | RFlag24Ghz | RFlagAuto;
	if(bss != nil){
		ctlr->aid = bss->aid;
		ctlr->channel = bss->channel;
		memmove(ctlr->bssid, bss->bssid, sizeof(ctlr->bssid));
		if(bss->cap & (1<<5))
			ctlr->rxflags |= RFlagShPreamble;
		if(bss->cap & (1<<10))
			ctlr->rxflags |= RFlagShSlot;
		if(ctlr->aid != 0){
			ctlr->rxfilter |= FilterBSS;
			ctlr->rxfilter &= ~FilterBeacon;
			ctlr->bss.id = -1;
		} else {
			ctlr->bcast.id = -1;
		}
	} else {
		ctlr->aid = 0;
		memmove(ctlr->bssid, edev->bcast, sizeof(ctlr->bssid));
		ctlr->bcast.id = -1;
		ctlr->bss.id = -1;
	}

	if(ctlr->aid != 0)
		setled(ctlr, 2, 0, 1);		/* on when associated */
	else if(memcmp(ctlr->bssid, edev->bcast, sizeof(ctlr->bssid)) != 0)
		setled(ctlr, 2, 10, 10);	/* slow blink when connecting */
	else
		setled(ctlr, 2, 5, 5);		/* fast blink when scanning */

	if(ctlr->wifi->debug)
		print("#l%d: rxon: bssid %E, aid %x, channel %d, rxfilter %ux, rxflags %ux\n",
			edev->ctlrno, ctlr->bssid, ctlr->aid, ctlr->channel, ctlr->rxfilter, ctlr->rxflags);

	if(ctlr->family >= 7000)
		err = rxon7000(edev, ctlr);
	else
		err = rxon6000(edev, ctlr);
	if(err != nil)
		goto Out;

	if(ctlr->bcast.id == -1){
		if((err = setstation(ctlr,
			(ctlr->type != Type4965)? 15: 31,
			StaTypeGeneralPurpose,
			edev->bcast,
			&ctlr->bcast)) != nil)
			goto Out;
	}
	if(ctlr->bss.id == -1 && bss != nil && ctlr->aid != 0){
		if((err = setstation(ctlr,
			0,
			StaTypeLink,
			bss->bssid,
			&ctlr->bss)) != nil)
			goto Out;

		if(ctlr->family >= 7000)
			if((err = setmaccontext(edev, ctlr, CmdModify, bss)) != nil)
				goto Out;
	} else {
		if(ctlr->family >= 7000)
			if((err = settimeevent(ctlr, CmdAdd, (bss != nil)? bss->ival: 0)) != nil)
				goto Out;
	}
Out:
	return err;
}

static void
transmit(Wifi *wifi, Wnode *wn, Block *b)
{
	int flags, rate, ant;
	uchar c[Tcmdsize], *p;
	Ether *edev;
	Station *sta;
	Ctlr *ctlr;
	Wifipkt *w;
	char *err;

	edev = wifi->ether;
	ctlr = edev->ctlr;

	qlock(ctlr);
	if(ctlr->attached == 0 || ctlr->broken){
Broken:
		qunlock(ctlr);
		freeb(b);
		return;
	}

	if((wn->channel != ctlr->channel)
	|| (!ctlr->prom && (wn->aid != ctlr->aid || memcmp(wn->bssid, ctlr->bssid, Eaddrlen) != 0))){
		if(rxon(edev, wn) != nil)
			goto Broken;
	}

	if(b == nil){
		/* association note has no data to transmit */
		qunlock(ctlr);
		return;
	}

	flags = 0;
	sta = &ctlr->bcast;
	p = wn->minrate;
	w = (Wifipkt*)b->rp;
	if((w->a1[0] & 1) == 0){
		flags |= TFlagNeedACK;

		if(BLEN(b) > 512-4)
			flags |= TFlagNeedRTS;

		if((w->fc[0] & 0x0c) == 0x08 &&	ctlr->bss.id != -1){
			sta = &ctlr->bss;
			p = wn->actrate;
		}

		if(flags & (TFlagNeedRTS|TFlagNeedCTS)){
			if(ctlr->family >= 7000 || ctlr->type != Type4965){
				flags &= ~(TFlagNeedRTS|TFlagNeedCTS);
				flags |= TFlagNeedProtection;
			} else
				flags |= TFlagFullTxOp;
		}
	}

	if(sta->id == -1)
		goto Broken;

	if(p >= wifi->rates)
		rate = p - wifi->rates;
	else
		rate = 0;

	/* select first available antenna */
	ant = ctlr->rfcfg.txantmask & 7;
	ant |= (ant == 0);
	ant = ((ant - 1) & ant) ^ ant;

	memset(p = c, 0, sizeof(c));
	put16(p, BLEN(b));
	p += 2;
	p += 2;		/* lnext */
	put32(p, flags);
	p += 4;
	put32(p, 0);
	p += 4;		/* scratch */

	*p++ = ratetab[rate].plcp;
	*p++ = ratetab[rate].flags | (ant<<6);

	p += 2;		/* xflags */
	*p++ = sta->id;	/* station id */
	*p++ = 0;	/* security */
	*p++ = 0;	/* linkq */
	p++;		/* reserved */
	p += 16;	/* key */
	p += 2;		/* fnext */
	p += 2;		/* reserved */
	put32(p, ~0);	/* lifetime */
	p += 4;

	/* BUG: scratch ptr? not clear what this is for */
	put32(p, PCIWADDR(ctlr->kwpage));
	p += 5;

	*p++ = 60;	/* rts ntries */
	*p++ = 15;	/* data ntries */
	*p++ = 0;	/* tid */
	put16(p, 0);	/* timeout */
	p += 2;
	p += 2;		/* txop */
	qunlock(ctlr);

	if((err = qcmd(ctlr, 0, 28, c, p - c, b)) != nil){
		print("#l%d: transmit %s\n", edev->ctlrno, err);
		freeb(b);
	}
}

static long
iwlctl(Ether *edev, void *buf, long n)
{
	Ctlr *ctlr;

	ctlr = edev->ctlr;
	if(n >= 5 && memcmp(buf, "reset", 5) == 0){
		ctlr->broken = 1;
		return n;
	}
	if(ctlr->wifi)
		return wifictl(ctlr->wifi, buf, n);
	return 0;
}

static long
iwlifstat(Ether *edev, void *buf, long n, ulong off)
{
	Ctlr *ctlr;

	ctlr = edev->ctlr;
	if(ctlr->wifi)
		return wifistat(ctlr->wifi, buf, n, off);
	return 0;
}

static void
setoptions(Ether *edev)
{
	Ctlr *ctlr;
	int i;

	ctlr = edev->ctlr;
	for(i = 0; i < edev->nopt; i++)
		wificfg(ctlr->wifi, edev->opt[i]);
}

static void
iwlpromiscuous(void *arg, int on)
{
	Ether *edev;
	Ctlr *ctlr;

	edev = arg;
	ctlr = edev->ctlr;
	qlock(ctlr);
	ctlr->prom = on;
	rxon(edev, ctlr->wifi->bss);
	qunlock(ctlr);
}

static void
iwlmulticast(void *, uchar*, int)
{
}

static void
iwlrecover(void *arg)
{
	Ether *edev;
	Ctlr *ctlr;

	edev = arg;
	ctlr = edev->ctlr;
	while(waserror())
		;
	for(;;){
		tsleep(&up->sleep, return0, 0, 4000);

		qlock(ctlr);
		for(;;){
			if(ctlr->broken == 0)
				break;

			if(ctlr->power)
				poweroff(ctlr);

			if((csr32r(ctlr, Gpc) & RfKill) == 0)
				break;

			if(reset(ctlr) != nil)
				break;
			if(boot(ctlr) != nil)
				break;

			rxon(edev, ctlr->wifi->bss);
			break;
		}
		qunlock(ctlr);
	}
}

static void
iwlattach(Ether *edev)
{
	FWImage *fw;
	Ctlr *ctlr;
	char *err;

	ctlr = edev->ctlr;
	eqlock(ctlr);
	if(waserror()){
		print("#l%d: %s\n", edev->ctlrno, up->errstr);
		if(ctlr->power)
			poweroff(ctlr);
		qunlock(ctlr);
		nexterror();
	}
	if(ctlr->attached == 0){
		if((csr32r(ctlr, Gpc) & RfKill) == 0)
			error("wifi disabled by switch");

		if(ctlr->fw == nil){
			char *fn;

			fn = ctlr->fwname;
			if(fn == nil){
				fn = fwname[ctlr->type];
				if(ctlr->type == Type6005){
					switch(ctlr->pdev->did){
					case 0x0082:	/* Centrino Advanced-N 6205 */
					case 0x0085:	/* Centrino Advanced-N 6205 */
						break;
					default:	/* Centrino Advanced-N 6030, 6235 */
						fn = "iwn-6030";
					}
				}
			}
			fw = readfirmware(fn);
			print("#l%d: firmware: %s, rev %ux, build %ud, size [%d] %ux+%ux + [%d] %ux+%ux + %ux\n",
				edev->ctlrno, fn,
				fw->rev, fw->build,
				fw->main.nsect, fw->main.text.size, fw->main.data.size,
				fw->init.nsect, fw->init.text.size, fw->init.data.size,
				fw->boot.text.size);
			ctlr->fw = fw;
		}

		if(ctlr->family >= 7000){
			u32int u = ctlr->fw->physku;

			ctlr->rfcfg.type = u & 3;	u >>= 2;
			ctlr->rfcfg.step = u & 3;	u >>= 2;
			ctlr->rfcfg.dash = u & 3;	u >>= 12;

			ctlr->rfcfg.txantmask = u & 15;	u >>= 4;
			ctlr->rfcfg.rxantmask = u & 15;
		}

		if((err = reset(ctlr)) != nil)
			error(err);
		if((err = boot(ctlr)) != nil)
			error(err);

		if(ctlr->wifi == nil){
			qsetlimit(edev->oq, MaxQueue);

			ctlr->wifi = wifiattach(edev, transmit);
			/* tested with 2230, it has transmit issues using higher bit rates */
			if(ctlr->family >= 7000 || ctlr->type != Type2030)
				ctlr->wifi->rates = iwlrates;
		}

		setoptions(edev);

		ctlr->attached = 1;

		kproc("iwlrecover", iwlrecover, edev);
	}
	qunlock(ctlr);
	poperror();
}

static void
receive(Ctlr *ctlr)
{
	Block *b, *bb;
	uchar *d;
	RXQ *rx;
	TXQ *tx;
	uint hw;

	rx = &ctlr->rx;
	if(ctlr->broken || rx->s == nil || rx->b == nil)
		return;

	for(hw = get16(rx->s) % Nrx; rx->i != hw; rx->i = (rx->i + 1) % Nrx){
		int type, flags, idx, qid, len;

		b = rx->b[rx->i];
		if(b == nil)
			continue;

		d = b->rp;
		len = get32(d); d += 4;
		type = *d++;
		flags = *d++;
		USED(flags);
		idx = *d++;
		qid = *d++;

		tx = nil;
		bb = nil;
		if((qid & 0x80) == 0 && qid < ctlr->ntxq){
			tx = &ctlr->tx[qid];
			bb = tx->b[idx];
			tx->b[idx] = nil;
		}

		len &= 0x3fff;
		len -= 4;
		if(len >= 0) switch(type){
		case 1:		/* microcontroller ready */
			setfwinfo(ctlr, d, len);
			break;
		case 24:	/* add node done */
			if(len < 4)
				break;
			break;
		case 28:	/* tx done */
			if(ctlr->family >= 7000){
				if(len <= 36 || d[36] == 1 || d[36] == 2)
					break;
			} else if(ctlr->type == Type4965){
				if(len <= 20 || d[20] == 1 || d[20] == 2)
					break;
			} else {
				if(len <= 32 || d[32] == 1 || d[32] == 2)
					break;
			}
			if(ctlr->wifi != nil)
				wifitxfail(ctlr->wifi, bb);
			break;
		case 41:
			if(len >= 16 && get32(d) == 0 && ctlr->timeid == -1)
				ctlr->timeid = get32(d+8);
			break;
		case 102:	/* calibration result (Type5000 only) */
			if(ctlr->family >= 7000)
				break;
			if(len < 4)
				break;
			idx = d[0];
		Calib:
			if(idx < 0 || idx >= nelem(ctlr->calib.cmd))
				break;
			if(rbplant(ctlr, rx->i) < 0)
				break;
			if(ctlr->calib.cmd[idx] != nil)
				freeb(ctlr->calib.cmd[idx]);
			b->rp = d;
			b->wp = d + len;
			ctlr->calib.cmd[idx] = b;
			break;
		case 4:		/* init complete (>= 7000 family) */
			if(ctlr->family < 7000)
				break;
		case 103:	/* calibration done (Type5000 only) */
			ctlr->calib.done = 1;
			break;
		case 107:	/* calibration result (>= 7000 family) */
			if(ctlr->family < 7000)
				break;
			len -= 4;
			if(len < 0)
				break;
			idx = get16(d+2);
			if(idx < len)
				len = idx;
			idx = -1;
			switch(get16(d)){
			case 1:
				idx = &ctlr->calib.cfg - &ctlr->calib.cmd[0];
				break;
			case 2:
				idx = &ctlr->calib.nch - &ctlr->calib.cmd[0];
				break;
			case 4:
				if(len < 2)
					break;
				idx = &ctlr->calib.papd[get16(d+4) % nelem(ctlr->calib.papd)] - &ctlr->calib.cmd[0];
				break;
			case 5:
				if(len < 2)
					break;
				idx = &ctlr->calib.txp[get16(d+4) % nelem(ctlr->calib.txp)] - &ctlr->calib.cmd[0];
				break;
			}
			len += 4;
			goto Calib;
		case 130:	/* start scan */
		case 132:	/* stop scan */
			break;
		case 136:	/* NVM access (>= 7000 family) */
			if(ctlr->family < 7000)
				break;
			len -= 8;
			if(len < 0)
				break;
			if(ctlr->nvm.len < len)
				len = ctlr->nvm.len;
			ctlr->nvm.off = get16(d + 0);
			ctlr->nvm.ret = get16(d + 2);
			ctlr->nvm.type= get16(d + 4);
			ctlr->nvm.sts = get16(d + 6);
			d += 8;
			if(ctlr->nvm.ret < len)
				len = ctlr->nvm.ret;
			if(ctlr->nvm.buf != nil && len > 0)
				memmove(ctlr->nvm.buf, d, len);
			ctlr->nvm.buf = nil;
			ctlr->nvm.len = 0;
			break;
		case 156:	/* rx statistics */
		case 157:	/* beacon statistics */
		case 161:	/* state changed */
		case 162:	/* beacon missed */
		case 177:	/* mduart load notification */
			break;
		case 192:	/* rx phy */
			if(len >= 8){
				u32int dt = get32(d+4) - (u32int)ctlr->systime;
				ctlr->systime += dt;
			}
			break;
		case 195:	/* rx done */
			if(d + 2 > b->lim)
				break;
			d += d[1];
			d += 56;
		case 193:	/* mpdu rx done */
			if(d + 4 > b->lim)
				break;
			len = get16(d); d += 4;
			if(d + len + 4 > b->lim)
				break;
			if((d[len] & 3) != 3)
				break;
			if(ctlr->wifi == nil)
				break;
			if(rbplant(ctlr, rx->i) < 0)
				break;
			b->rp = d;
			b->wp = d + len;

			put64(d - 8, ctlr->systime);
			b->flag |= Btimestamp;

			wifiiq(ctlr->wifi, b);
			break;
		case 197:	/* rx compressed ba */
			break;
		}
		freeblist(bb);
		if(tx != nil && tx->n > 0){
			tx->n--;
			wakeup(tx);
		}
	}
	csr32w(ctlr, FhRxWptr, ((hw+Nrx-1) % Nrx) & ~7);
}

static void
iwlinterrupt(Ureg*, void *arg)
{
	u32int isr, fhisr;
	Ether *edev;
	Ctlr *ctlr;

	edev = arg;
	ctlr = edev->ctlr;
	ilock(ctlr);
	csr32w(ctlr, Imr, 0);
	isr = csr32r(ctlr, Isr);
	fhisr = csr32r(ctlr, FhIsr);
	if(isr == 0xffffffff || (isr & 0xfffffff0) == 0xa5a5a5a0){
		iunlock(ctlr);
		return;
	}
	if(isr == 0 && fhisr == 0)
		goto done;
	csr32w(ctlr, Isr, isr);
	csr32w(ctlr, FhIsr, fhisr);
	if((isr & (Iswrx | Ifhrx | Irxperiodic)) || (fhisr & Ifhrx))
		receive(ctlr);
	if(isr & Ierr){
		ctlr->broken = 1;
		print("#l%d: fatal firmware error\n", edev->ctlrno);
		dumpctlr(ctlr);
	}
	ctlr->wait.m |= isr;
	if(ctlr->wait.m & ctlr->wait.w)
		wakeup(&ctlr->wait);
done:
	csr32w(ctlr, Imr, ctlr->ie);
	iunlock(ctlr);
}

static void
iwlshutdown(Ether *edev)
{
	Ctlr *ctlr;

	ctlr = edev->ctlr;
	if(ctlr->power)
		poweroff(ctlr);
	ctlr->broken = 0;
}

static Ctlr *iwlhead, *iwltail;

static void
iwlpci(void)
{
	Pcidev *pdev;
	char *fwname;
	int family;
	
	pdev = nil;
	while(pdev = pcimatch(pdev, 0, 0)) {
		Ctlr *ctlr;
		void *mem;
		
		if(pdev->ccrb != 2 || pdev->ccru != 0x80)
			continue;
		if(pdev->vid != 0x8086)
			continue;
		if(pdev->mem[0].bar & 1)
			continue;

		switch(pdev->did){
		default:
			continue;
		case 0x0084:	/* WiFi Link 1000 */
		case 0x4229:	/* WiFi Link 4965 */
		case 0x4230:	/* WiFi Link 4965 */
		case 0x4232:	/* Wifi Link 5100 */
		case 0x4235:	/* Intel Corporation Ultimate N WiFi Link 5300 */
		case 0x4236:	/* WiFi Link 5300 AGN */
		case 0x4237:	/* Wifi Link 5100 AGN */
		case 0x4239:	/* Centrino Advanced-N 6200 */
		case 0x423d:	/* Wifi Link 5150 */
		case 0x423b:	/* PRO/Wireless 5350 AGN */
		case 0x0082:	/* Centrino Advanced-N 6205 */
		case 0x0085:	/* Centrino Advanced-N 6205 */
		case 0x422b:	/* Centrino Ultimate-N 6300 variant 1 */
		case 0x4238:	/* Centrino Ultimate-N 6300 variant 2 */
		case 0x08ae:	/* Centrino Wireless-N 100 */
		case 0x0083:	/* Centrino Wireless-N 1000 */
		case 0x008a:	/* Centrino Wireless-N 1030 */
		case 0x0891:	/* Centrino Wireless-N 2200 */
		case 0x0887:	/* Centrino Wireless-N 2230 */
		case 0x0888:	/* Centrino Wireless-N 2230 */
		case 0x0090:	/* Centrino Advanced-N 6030 */
		case 0x0091:	/* Centrino Advanced-N 6030 */
		case 0x088e:	/* Centrino Advanced-N 6235 */
		case 0x088f:	/* Centrino Advanced-N 6235 */
			family = 0;
			fwname = nil;
			break;
		case 0x24fd:	/* Wireless AC 8265 */
			family = 8000;
			fwname = "iwm-8265-34";
			break;
		}

		ctlr = malloc(sizeof(Ctlr));
		if(ctlr == nil) {
			print("iwl: unable to alloc Ctlr\n");
			continue;
		}
		ctlr->port = pdev->mem[0].bar & ~0xF;
		mem = vmap(ctlr->port, pdev->mem[0].size);
		if(mem == nil) {
			print("iwl: can't map %llux\n", ctlr->port);
			free(ctlr);
			continue;
		}
		ctlr->nic = mem;
		ctlr->pdev = pdev;
		ctlr->fwname = fwname;
		ctlr->family = family;

		if(iwlhead != nil)
			iwltail->link = ctlr;
		else
			iwlhead = ctlr;
		iwltail = ctlr;
	}
}

static int
iwlpnp(Ether* edev)
{
	Ctlr *ctlr;
	
	if(iwlhead == nil)
		iwlpci();
again:
	for(ctlr = iwlhead; ctlr != nil; ctlr = ctlr->link){
		if(ctlr->edev != nil)
			continue;
		if(edev->port == 0 || edev->port == ctlr->port){
			ctlr->edev = edev;
			break;
		}
	}

	if(ctlr == nil)
		return -1;

	edev->ctlr = ctlr;
	edev->port = ctlr->port;
	edev->irq = ctlr->pdev->intl;
	edev->tbdf = ctlr->pdev->tbdf;
	edev->arg = edev;
	edev->attach = iwlattach;
	edev->ifstat = iwlifstat;
	edev->ctl = iwlctl;
	edev->shutdown = iwlshutdown;
	edev->promiscuous = iwlpromiscuous;
	edev->multicast = iwlmulticast;
	edev->mbps = 54;

	pcienable(ctlr->pdev);
	if(iwlinit(edev) < 0){
		pcidisable(ctlr->pdev);
		ctlr->edev = (void*)-1;
		edev->ctlr = nil;
		goto again;
	}

	pcisetbme(ctlr->pdev);
	intrenable(edev->irq, iwlinterrupt, edev, edev->tbdf, edev->name);
	
	return 0;
}

void
etheriwllink(void)
{
	addethercard("iwl", iwlpnp);
}
