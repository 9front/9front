/*
 * Intel WiFi Link driver.
 *
 * Written without any documentation but Damien Bergaminis
 * OpenBSD iwn(4) driver sources. Requires intel firmware
 * to be present in /lib/firmware/iwn-* on attach.
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/netif.h"

#include "etherif.h"
#include "wifi.h"

enum {

	Ntxlog		= 8,
	Ntx		= 1<<Ntxlog,
	Nrxlog		= 8,
	Nrx		= 1<<Nrxlog,

	Rstatsize	= 16,
	Rbufsize	= 4*1024,
	Rdscsize	= 8,

	Tbufsize	= 4*1024,
	Tdscsize	= 128,
	Tcmdsize	= 140,
};

/* registers */
enum {
	Cfg		= 0x000,	/* config register */
		MacSi		= 1<<8,
		RadioSi		= 1<<9,
		EepromLocked	= 1<<21,
		NicReady	= 1<<22,
		HapwakeL1A	= 1<<23,
		PrepareDone	= 1<<25,
		Prepare		= 1<<27,

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

	Led		= 0x094,
		LedBsmCtrl	= 1<<5,
		LedOn		= 0x38,
		LedOff		= 0x78,

	UcodeGp1Clr	= 0x05c,
		UcodeGp1RfKill		= 1<<1,
		UcodeGp1CmdBlocked	= 1<<2,
		UcodeGp1CtempStopRf	= 1<<3,

	ShadowRegCtrl	= 0x0a8,

	Giochicken	= 0x100,
		L1AnoL0Srx	= 1<<23,
		DisL0Stimer	= 1<<29,

	AnaPll		= 0x20c,

	Dbghpetmem	= 0x240,

	MemRaddr	= 0x40c,
	MemWaddr	= 0x410,
	MemWdata	= 0x418,
	MemRdata	= 0x41c,

	PrphWaddr	= 0x444,
	PrphRaddr	= 0x448,
	PrphWdata	= 0x44c,
	PrphRdata	= 0x450,

	HbusTargWptr	= 0x460,
};

/*
 * Flow-Handler registers.
 */
enum {
	FhTfbdCtrl0	= 0x1900,	// +q*8
	FhTfbdCtrl1	= 0x1904,	// +q*8

	FhKwAddr	= 0x197c,

	FhSramAddr	= 0x19a4,	// +q*4
	FhCbbcQueue	= 0x19d0,	// +q*4
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
};

/*
 * TX scheduler registers.
 */
enum {
	SchedBase		= 0xa02c00,
	SchedSramAddr		= SchedBase,
	SchedDramAddr5000	= SchedBase+0x008,
	SchedDramAddr4965	= SchedBase+0x010,
	SchedTxFact5000		= SchedBase+0x010,
	SchedTxFact4965		= SchedBase+0x01c,
	SchedQueueRdptr4965	= SchedBase+0x064,	// +q*4
	SchedQueueRdptr5000	= SchedBase+0x068,	// +q*4
	SchedQChainSel4965	= SchedBase+0x0d0,
	SchedIntrMask4965	= SchedBase+0x0e4,
	SchedQChainSel5000	= SchedBase+0x0e8,
	SchedQueueStatus4965	= SchedBase+0x104,	// +q*4
	SchedIntrMask5000	= SchedBase+0x108,
	SchedQueueStatus5000	= SchedBase+0x10c,	// +q*4
	SchedAggrSel5000	= SchedBase+0x248,
};

enum {
	SchedCtxOff4965		= 0x380,
	SchedCtxLen4965		= 416,
	SchedTransTblOff4965	= 0x500,

	SchedCtxOff5000		= 0x600,
	SchedCtxLen5000		= 512,
	SchedTransTblOff5000	= 0x7e0,
};

typedef struct FWInfo FWInfo;
typedef struct FWImage FWImage;
typedef struct FWSect FWSect;

typedef struct TXQ TXQ;
typedef struct RXQ RXQ;

typedef struct Ctlr Ctlr;

typedef struct Ctlrtype Ctlrtype;

struct FWSect
{
	uchar	*data;
	uint	size;
};

struct FWImage
{
	struct {
		FWSect	text;
		FWSect	data;
	} init, main, boot;

	uint	rev;
	uint	build;
	char	descr[64+1];
	uchar	data[];
};

struct FWInfo
{
	uchar	major;
	uchar	minjor;
	uchar	type;
	uchar	subtype;

	u32int	logptr;
	u32int	errptr;
	u32int	tstamp;
	u32int	valid;
};

struct TXQ
{
	uint	n;
	uint	i;
	Block	**b;
	uchar	*d;
	uchar	*c;

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

struct Ctlr {
	Lock;
	QLock;

	Ctlr *link;
	Pcidev *pdev;
	Wifi *wifi;

	int type;
	int port;
	int active;
	int attached;

	u32int ie;

	u32int *nic;
	uchar *kwpage;

	int channel;

	RXQ rx;
	TXQ tx[20];

	struct {
		Rendez;
		u32int	m;
		u32int	w;
		u32int	r;
	} wait;

	struct {
		uchar	type;
		uchar	step;
		uchar	dash;
		uchar	txantmask;
		uchar	rxantmask;
	} rfcfg;

	struct {
		u32int	crystal;
	} eeprom;

	struct {
		u32int	base;
		uchar	*s;
	} sched;

	FWInfo fwinfo;
	FWImage *fw;
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
	Type6005	= 11,
};

struct Ctlrtype
{
	char	*fwname;
};

static Ctlrtype ctlrtype[16] = {
	[Type4965] {
		.fwname = "iwn-4965",
	},
	[Type5300] {
		.fwname = "iwn-5000",
	},
	[Type5350] {
		.fwname = "iwn-5000",
	},
	[Type5150] {
		.fwname = "iwn-5150",
	},
	[Type5100] {
		.fwname = "iwn-5000",
	},
	[Type1000] {
		.fwname = "iwn-1000",
	},
	[Type6000] {
		.fwname = "iwn-6000",
	},
	[Type6050] {
		.fwname = "iwn-6050",
	},
	[Type6005] {
		.fwname = "iwn-6005",
	},
};

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

	if(len < 32)
		return;
	i = &ctlr->fwinfo;
	i->minjor = *d++;
	i->major = *d++;
	d += 2+8;
	i->type = *d++;
	i->subtype = *d++;
	d += 2;
	i->logptr = get32(d); d += 4;
	i->errptr = get32(d); d += 4;
	i->tstamp = get32(d); d += 4;
	i->valid = get32(d);
};

static void
dumpctlr(Ctlr *ctlr)
{
	u32int dump[13];
	int i;

	if(ctlr->fwinfo.errptr == 0){
		print("no error pointer\n");
		return;
	}
	for(i=0; i<nelem(dump); i++)
		dump[i] = memread(ctlr, ctlr->fwinfo.errptr + i*4);
	print(	"error:\tid %ux, pc %ux,\n"
		"\tbranchlink %.8ux %.8ux, interruptlink %.8ux %.8ux,\n"
		"\terrordata %.8ux %.8ux, srcline %ud, tsf %ux, time %ux\n",
		dump[1], dump[2],
		dump[4], dump[3], dump[6], dump[5],
		dump[7], dump[8], dump[9], dump[10], dump[11]);
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
	u32int w;
	int i;

	w = 0;
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
			return 0;
		delay(10);
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
			return 0;
		delay(10);
	}
	return "handover: timeout";
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

	/* Disable L0s exit timer (NMI bug workaround). */
	csr32w(ctlr, Giochicken, csr32r(ctlr, Giochicken) | DisL0Stimer);

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

	if(ctlr->type != Type4965 && ctlr->type <= Type1000)
		csr32w(ctlr, AnaPll, csr32r(ctlr, AnaPll) | 0x00880300);

	/* Wait for clock stabilization before accessing prph. */
	if((err = clockwait(ctlr)) != nil)
		return err;

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
	return 0;
}

static int
iwlinit(Ether *edev)
{
	Ctlr *ctlr;
	char *err;
	uchar b[2];
	uint u;

	ctlr = edev->ctlr;
	if((err = handover(ctlr)) != nil)
		goto Err;
	if((err = poweron(ctlr)) != nil)
		goto Err;
	if((csr32r(ctlr, EepromGp) & 0x7) == 0){
		err = "bad rom signature";
		goto Err;
	}
	if((err = eepromlock(ctlr)) != nil)
		goto Err;
	if((err = eepromread(ctlr, edev->ea, sizeof(edev->ea), 0x15)) != nil){
		eepromunlock(ctlr);
		goto Err;
	}
	if(ctlr->type != Type4965){
		if((err = eepromread(ctlr, b, 2, 0x048)) != nil){
			eepromunlock(ctlr);
			goto Err;
		}
		u = get16(b);
		ctlr->rfcfg.type = u & 3;	u >>= 2;
		ctlr->rfcfg.step = u & 3;	u >>= 2;
		ctlr->rfcfg.dash = u & 3;	u >>= 4;
		ctlr->rfcfg.txantmask = u & 15;	u >>= 4;
		ctlr->rfcfg.rxantmask = u & 15;
		if((err = eepromread(ctlr, b, 4, 0x128)) != nil){
			eepromunlock(ctlr);
			goto Err;
		}
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

	ctlr->ie = 0;
	csr32w(ctlr, Isr, ~0);	/* clear pending interrupts */
	csr32w(ctlr, Imr, 0);	/* no interrupts for now */

	return 0;
Err:
	print("iwlinit: %s\n", err);
	return -1;
}

static char*
crackfw(FWImage *i, uchar *data, uint size, int alt)
{
	uchar *p, *e;
	FWSect *s;

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
		i->descr[sizeof(i->descr)-1] = 0;
		p += 64;
		i->rev = get32(p); p += 4;
		i->build = get32(p); p += 4;
		altmask = get32(p); p += 4;
		altmask |= (uvlong)get32(p) << 32; p += 4;
		while(alt > 0 && (altmask & (1ULL<<alt)) == 0)
			alt--;
		while(p < e){
			FWSect dummy;

			if((p + 2+2+4) > e)
				goto Tooshort;
			switch(get16(p)){
			case 1:	s = &i->main.text; break;
			case 2: s = &i->main.data; break;
			case 3: s = &i->init.text; break;
			case 4: s = &i->init.data; break;
			case 5: s = &i->boot.text; break;
			default:s = &dummy;
			}
			p += 2;
			if(get16(p) != alt)
				s = &dummy;
			p += 2;
			s->size = get32(p); p += 4;
			s->data = p;
			if((p + s->size) > e)
				goto Tooshort;
			p += (s->size + 3) & ~3;
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

typedef struct Irqwait Irqwait;
struct Irqwait {
	Ctlr	*ctlr;
	u32int	mask;
};

static int
gotirq(void *arg)
{
	Irqwait *w;
	Ctlr *ctlr;

	w = arg;
	ctlr = w->ctlr;
	ctlr->wait.r = ctlr->wait.m & w->mask;
	if(ctlr->wait.r){
		ctlr->wait.m &= ~ctlr->wait.r;
		return 1;
	}
	ctlr->wait.w = w->mask;
	return 0;
}

static u32int
irqwait(Ctlr *ctlr, u32int mask, int timeout)
{
	Irqwait w;

	w.ctlr = ctlr;
	w.mask = mask;
	tsleep(&ctlr->wait, gotirq, &w, timeout);
	ctlr->wait.w = 0;
	return ctlr->wait.r & mask;
}

static char*
loadfirmware1(Ctlr *ctlr, u32int dst, uchar *data, int size)
{
	uchar *dma;
	char *err;

	dma = mallocalign(size, 16, 0, 0);
	if(dma == nil)
		return "no memory for dma";
	memmove(dma, data, size);
	coherence();
	if((err = niclock(ctlr)) != 0){
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
	if(irqwait(ctlr, Ifhtx|Ierr, 5000) != Ifhtx){
		free(dma);
		return "dma error / timeout";
	}
	free(dma);
	return 0;
}

static char*
bootfirmware(Ctlr *ctlr)
{
	int i, n, size;
	uchar *p, *dma;
	FWImage *fw;
	char *err;

	dma = nil;
	fw = ctlr->fw;

	if(fw->boot.text.size == 0){
		if((err = loadfirmware1(ctlr, 0x00000000, fw->main.text.data, fw->main.text.size)) != nil)
			return err;
		if((err = loadfirmware1(ctlr, 0x00800000, fw->main.data.data, fw->main.data.size)) != nil)
			return err;
		goto bootmain;
	}

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
		return "bootfirmware: bootcode timeout";
	}

	prphwrite(ctlr, BsmWrCtrl, 1<<30);
	nicunlock(ctlr);

	csr32w(ctlr, Reset, 0);
	if(irqwait(ctlr, Ierr|Ialive, 5000) != Ialive){
		free(dma);
		return "init firmware boot failed";
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

bootmain:
	csr32w(ctlr, Reset, 0);
	if(irqwait(ctlr, Ierr|Ialive, 5000) != Ialive){
		free(dma);
		return "main firmware boot failed";
	}
	free(dma);
	return nil;
}

static int
txqready(void *arg)
{
	TXQ *q = arg;
	return q->n < Ntx;
}

static void
qcmd(Ctlr *ctlr, uint qid, uint code, uchar *data, int size, Block *block)
{
	uchar *d, *c;
	TXQ *q;

	assert(qid < nelem(ctlr->tx));
	assert(size <= Tcmdsize-4);

	ilock(ctlr);
	q = &ctlr->tx[qid];
	while(q->n >= Ntx){
		iunlock(ctlr);
		eqlock(q);
		if(waserror()){
			qunlock(q);
			nexterror();
		}
		tsleep(q, txqready, q, 10);
		qunlock(q);
		ilock(ctlr);
	}
	q->n++;

	q->b[q->i] = block;
	c = q->c + q->i * Tcmdsize;
	d = q->d + q->i * Tdscsize;

	/* build command */
	c[0] = code;
	c[1] = 0;	/* flags */
	c[2] = q->i;
	c[3] = qid;

	memmove(c+4, data, size);

	size += 4;

	/* build descriptor */
	*d++ = 0;
	*d++ = 0;
	*d++ = 0;
	*d++ = 1 + (block != nil); /* nsegs */
	put32(d, PCIWADDR(c));	d += 4;
	put16(d, size << 4); d += 2;
	if(block != nil){
		size = BLEN(block);
		if(size > Tbufsize)
			size = Tbufsize;
		put32(d, PCIWADDR(block->rp)); d += 4;
		put16(d, size << 4);
	}

	coherence();

	q->i = (q->i+1) % Ntx;
	csr32w(ctlr, HbusTargWptr, (qid<<8) | q->i);

	iunlock(ctlr);
}

static void
cmd(Ctlr *ctlr, uint code, uchar *data, int size)
{
	qcmd(ctlr, 4, code, data, size, nil);
}

static void
setled(Ctlr *ctlr, int which, int on, int off)
{
	uchar c[8];

	csr32w(ctlr, Led, csr32r(ctlr, Led) & ~LedBsmCtrl);

	memset(c, 0, sizeof(c));
	put32(c, 10000);
	c[4] = which;
	c[5] = on;
	c[6] = off;
	cmd(ctlr, 72, c, sizeof(c));
}

/*
 * initialization which runs after the firmware has been booted up
 */
static void
postboot(Ctlr *ctlr)
{
	uint ctxoff, ctxlen, dramaddr, txfact;
	uchar c[8];
	char *err;
	int i, q;

	/* main led turn on! (verify that firmware processes commands) */
	setled(ctlr, 2, 0, 1);

	if((err = niclock(ctlr)) != nil)
		error(err);

	if(ctlr->type != Type4965){
		dramaddr = SchedDramAddr5000;
		ctxoff = SchedCtxOff5000;
		ctxlen = SchedCtxLen5000;
		txfact = SchedTxFact5000;
	} else {
		dramaddr = SchedDramAddr4965;
		ctxoff = SchedCtxOff4965;
		ctxlen = SchedCtxLen4965;
		txfact = SchedTxFact4965;
	}

	ctlr->sched.base = prphread(ctlr, SchedSramAddr);
	for(i=0; i < ctxlen/4; i++)
		memwrite(ctlr, ctlr->sched.base + ctxoff + i*4, 0);

	prphwrite(ctlr, dramaddr, PCIWADDR(ctlr->sched.s)>>10);

	csr32w(ctlr, FhTxChicken, csr32r(ctlr, FhTxChicken) | 2);

	if(ctlr->type != Type4965){
		/* Enable chain mode for all queues, except command queue 4. */
		prphwrite(ctlr, SchedQChainSel5000, 0xfffef);
		prphwrite(ctlr, SchedAggrSel5000, 0);

		for(q=0; q<nelem(ctlr->tx); q++){
			prphwrite(ctlr, SchedQueueRdptr5000 + q*4, 0);
			csr32w(ctlr, HbusTargWptr, q << 8);

			memwrite(ctlr, ctlr->sched.base + ctxoff + q*8, 0);
			/* Set scheduler window size and frame limit. */
			memwrite(ctlr, ctlr->sched.base + ctxoff + q*8 + 4, 64<<16 | 64);
		}
		/* Enable interrupts for all our 20 queues. */
		prphwrite(ctlr, SchedIntrMask5000, 0xfffff);
	} else {
		/* Disable chain mode for all our 16 queues. */
		prphwrite(ctlr, SchedQChainSel4965, 0);

		for(q=0; q<16; q++) {
			prphwrite(ctlr, SchedQueueRdptr4965 + q*4, 0);
			csr32w(ctlr, HbusTargWptr, q << 8);

			/* Set scheduler window size. */
			memwrite(ctlr, ctlr->sched.base + ctxoff + q*8, 64);
			/* Set scheduler window size and frame limit. */
			memwrite(ctlr, ctlr->sched.base + ctxoff + q*8 + 4, 64<<16);
		}
		/* Enable interrupts for all our 16 queues. */
		prphwrite(ctlr, SchedIntrMask4965, 0xffff);
	}

	/* Identify TX FIFO rings (0-7). */
	prphwrite(ctlr, txfact, 0xff);

	/* Mark TX rings (4 EDCA + cmd + 2 HCCA) as active. */
	for(q=0; q<7; q++){
		static uchar qid2fifo[] = { 3, 2, 1, 0, 7, 5, 6 };
		if(ctlr->type != Type4965)
			prphwrite(ctlr, SchedQueueStatus5000 + q*4, 0x00ff0018 | qid2fifo[q]);
		else
			prphwrite(ctlr, SchedQueueStatus4965 + q*4, 0x0007fc01 | qid2fifo[q]);
	}
	nicunlock(ctlr);

	if(ctlr->type != Type5150){
		memset(c, 0, sizeof(c));
		c[0] = 15;	/* code */
		c[1] = 0;	/* grup */
		c[2] = 1;	/* ngroup */
		c[3] = 1;	/* isvalid */
		put16(c+4, ctlr->eeprom.crystal);
		cmd(ctlr, 176, c, 8);
	}

	if(ctlr->type != Type4965){
		put32(c, ctlr->rfcfg.txantmask & 7);
		cmd(ctlr, 152, c, 4);
	}
}

static void
addnode(Ctlr *ctlr, uchar id, uchar *addr)
{
	uchar c[Tcmdsize], *p;

	memset(p = c, 0, sizeof(c));
	*p++ = 0;	/* control (1 = update) */
	p += 3;		/* reserved */
	memmove(p, addr, 6);
	p += 6;
	p += 2;		/* reserved */
	*p++ = id;	/* node id */
	p++;		/* flags */
	p += 2;		/* reserved */
	p += 2;		/* kflags */
	p++;		/* tcs2 */
	p++;		/* reserved */
	p += 5*2;	/* ttak */
	p++;		/* kid */
	p++;		/* reserved */
	p += 16;	/* key */
	if(ctlr->type != Type4965){
		p += 8;		/* tcs */
		p += 8;		/* rxmic */
		p += 8;		/* txmic */
		p += 4;		/* htflags */
		p += 4;		/* mask */
		p += 2;		/* disable tid */
		p += 2;		/* reserved */
		p++;		/* add ba tid */
		p++;		/* del ba tid */
		p += 2;		/* add ba ssn */
		p += 4;		/* reserved */
	}
	cmd(ctlr, 24, c, p - c);
}

void
rxon(Ether *edev, Wnode *bss)
{
	uchar c[Tcmdsize], *p;
	Ctlr *ctlr;

	ctlr = edev->ctlr;
	memset(p = c, 0, sizeof(c));
	memmove(p, edev->ea, 6); p += 8;	/* myaddr */
	memmove(p, (bss != nil) ? bss->bssid : edev->bcast, 6);
	p += 8;					/* bssid */
	memmove(p, edev->ea, 6); p += 8;	/* wlap */
	*p++ = 3;				/* mode (STA) */
	*p++ = 0;				/* air (?) */
	/* rxchain */
	put16(p, ((ctlr->rfcfg.rxantmask & 7)<<1) | (2<<10) | (2<<12));
	p += 2;
	*p++ = 0xff;				/* ofdm mask (not yet negotiated) */
	*p++ = 0x0f;				/* cck mask (not yet negotiated) */
	if(bss != nil)
		put16(p, bss->aid & ~0xc000);
	p += 2;					/* aid */
	put32(p, (1<<15)|(1<<30)|(1<<0));	/* flags (TSF | CTS_TO_SELF | 24GHZ) */
	p += 4;
	put32(p, 8|4|1);			/* filter (NODECRYPT|MULTICAST|PROMISC) */
	p += 4;
	*p++ = bss != nil ? bss->channel : ctlr->channel;
	p++;					/* reserved */
	*p++ = 0xff;				/* ht single mask */
	*p++ = 0xff;				/* ht dual mask */
	if(ctlr->type != Type4965){
		*p++ = 0xff;			/* ht triple mask */
		p++;				/* reserved */
		put16(p, 0); p += 2;		/* acquisition */
		p += 2;				/* reserved */
	}
	cmd(ctlr, 16, c, p - c);

	addnode(ctlr, (ctlr->type != Type4965) ? 15 : 31, edev->bcast);
	if(bss != nil)
		addnode(ctlr, 0, bss->bssid);
}

static struct ratetab {
	uchar	rate;
	uchar	plcp;
	uchar	flags;
} ratetab[] = {
	{   2,  10, 1<<1 },
	{   4,  20, 1<<1 },
	{  11,  55, 1<<1 },
	{  22, 110, 1<<1 },
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

static void
transmit(Wifi *wifi, Wnode *, Block *b)
{
	uchar c[Tcmdsize], *p;
	Ctlr *ctlr;

	ctlr = wifi->ether->ctlr;

	memset(p = c, 0, sizeof(c));
	put16(p, BLEN(b));
	p += 2;
	p += 2;		/* lnext */
	put32(p, 0);	/* flags */
	p += 4;
	put32(p, 0);
	p += 4;		/* scratch */

	/* BUG: hardcode 11Mbit */
	*p++ = ratetab[2].plcp;			/* plcp */
	*p++ = ratetab[2].flags | (1<<6);	/* rflags */

	p += 2;		/* xflags */

	/* BUG: we always use broadcast node! */
	*p++ = (ctlr->type != Type4965) ? 15 : 31;

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
	qcmd(ctlr, 0, 28, c, p - c, b);
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

static long
iwlctl(Ether *edev, void *buf, long n)
{
	Ctlr *ctlr;

	ctlr = edev->ctlr;
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
	char buf[64];
	int i;

	ctlr = edev->ctlr;
	ctlr->channel = 3;
	for(i = 0; i < edev->nopt; i++){
		if(strncmp(edev->opt[i], "channel=", 8) == 0)
			ctlr->channel = atoi(edev->opt[i]+8);
		else
		if(strncmp(edev->opt[i], "essid=", 6) == 0){
			snprint(buf, sizeof(buf), "essid %s", edev->opt[i]+6);
			if(!waserror()){
				wifictl(ctlr->wifi, buf, strlen(buf));
				poperror();
			}
		}
	}
}

static void
iwlattach(Ether *edev)
{
	FWImage *fw;
	Ctlr *ctlr;
	char *err;
	RXQ *rx;
	TXQ *tx;
	int i, q;

	ctlr = edev->ctlr;
	eqlock(ctlr);
	if(waserror()){
		qunlock(ctlr);
		nexterror();
	}
	if(ctlr->attached == 0){
		if((csr32r(ctlr, Gpc) & RfKill) == 0){
			print("#l%d: wifi disabled by switch\n", edev->ctlrno);
			error("wifi disabled by switch");
		}

		if(ctlr->wifi == nil)
			ctlr->wifi = wifiattach(edev, transmit);

		if(ctlr->fw == nil){
			fw = readfirmware(ctlrtype[ctlr->type].fwname);
			print("#l%d: firmware: %s, rev %ux, build %ud, size %ux+%ux+%ux+%ux+%ux\n",
				edev->ctlrno,
				ctlrtype[ctlr->type].fwname,
				fw->rev, fw->build,
				fw->main.text.size, fw->main.data.size,
				fw->init.text.size, fw->init.data.size,
				fw->boot.text.size);
			ctlr->fw = fw;
		}

		rx = &ctlr->rx;
		rx->i = 0;
		if(rx->b == nil)
			rx->b = malloc(sizeof(Block*) * Nrx);
		if(rx->p == nil)
			rx->p = mallocalign(sizeof(u32int) * Nrx, 256, 0, 0);
		if(rx->s == nil)
			rx->s = mallocalign(Rstatsize, 16, 0, 0);
		if(rx->b == nil || rx->p == nil || rx->s == nil)
			error("no memory for rx ring");
		memset(rx->s, 0, Rstatsize);
		for(i=0; i<Nrx; i++){
			rx->p[i] = 0;
			if(rx->b[i] != nil){
				freeb(rx->b[i]);
				rx->b[i] = nil;
			}
			if(rbplant(ctlr, i) < 0)
				error("no memory for rx descriptors");
		}

		for(q=0; q<nelem(ctlr->tx); q++){
			tx = &ctlr->tx[q];
			tx->i = 0;
			tx->n = 0;
			if(tx->b == nil)
				tx->b = malloc(sizeof(Block*) * Ntx);
			if(tx->d == nil)
				tx->d = mallocalign(Tdscsize * Ntx, 256, 0, 0);
			if(tx->c == nil)
				tx->c = mallocalign(Tcmdsize * Ntx, 4, 0, 0);
			if(tx->b == nil || tx->d == nil || tx->c == nil)
				error("no memory for tx ring");
			memset(tx->d, 0, Tdscsize * Ntx);
		}

		if(ctlr->sched.s == nil)
			ctlr->sched.s = mallocalign(512 * nelem(ctlr->tx) * 2, 1024, 0, 0);
		if(ctlr->kwpage == nil)
			ctlr->kwpage = mallocalign(4096, 4096, 0, 0);

		if((err = niclock(ctlr)) != nil)
			error(err);
		prphwrite(ctlr, ApmgPs, (prphread(ctlr, ApmgPs) & ~PwrSrcMask) | PwrSrcVMain);
		nicunlock(ctlr);

		csr32w(ctlr, Cfg, csr32r(ctlr, Cfg) | RadioSi | MacSi);

		if((err = niclock(ctlr)) != nil)
			error(err);
		prphwrite(ctlr, ApmgPs, prphread(ctlr, ApmgPs) | EarlyPwroffDis);
		nicunlock(ctlr);

		if((err = niclock(ctlr)) != nil)
			error(err);
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
		nicunlock(ctlr);

		if((err = niclock(ctlr)) != nil)
			error(err);

		prphwrite(ctlr, SchedTxFact5000, 0);

		csr32w(ctlr, FhKwAddr, PCIWADDR(ctlr->kwpage) >> 4);

		for(q=0; q<nelem(ctlr->tx); q++)
			if(q < 15 || ctlr->type != Type4965)
				csr32w(ctlr, FhCbbcQueue + q*4, PCIWADDR(ctlr->tx[q].d) >> 8);
		nicunlock(ctlr);

		for(i=0; i<8; i++)
			if(i < 7 || ctlr->type != Type4965)
				csr32w(ctlr, FhTxConfig + i*32, FhTxConfigDmaEna | FhTxConfigDmaCreditEna);

		csr32w(ctlr, UcodeGp1Clr, UcodeGp1RfKill);
		csr32w(ctlr, UcodeGp1Clr, UcodeGp1CmdBlocked);

		ctlr->ie = Idefmask;
		csr32w(ctlr, Imr, ctlr->ie);
		csr32w(ctlr, Isr, ~0);

		if(ctlr->type >= Type6000)
			csr32w(ctlr, ShadowRegCtrl, csr32r(ctlr, ShadowRegCtrl) | 0x800fffff);

		bootfirmware(ctlr);
		postboot(ctlr);

		setoptions(edev);

		rxon(edev, nil);

		edev->prom = 1;
		edev->link = 1;
		ctlr->attached = 1;
	}
	qunlock(ctlr);
	poperror();
}

static void
receive(Ctlr *ctlr)
{
	Block *b, *bb;
	uchar *d, *dd, *cc;
	RXQ *rx;
	TXQ *tx;
	uint hw;

	rx = &ctlr->rx;
	if(rx->s == nil || rx->b == nil)
		return;
	for(hw = get16(rx->s) % Nrx; rx->i != hw; rx->i = (rx->i + 1) % Nrx){
		uchar type, flags, idx, qid;
		u32int len;

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

		if((qid & 0x80) == 0 && qid < nelem(ctlr->tx)){
			tx = &ctlr->tx[qid];
			if(tx->n > 0){
				bb = tx->b[idx];
				if(bb != nil){
					tx->b[idx] = nil;
					freeb(bb);
				}
				/* paranoia: clear tx descriptors */
				dd = tx->d + idx*Tdscsize;
				cc = tx->c + idx*Tcmdsize;
				memset(dd, 0, Tdscsize);
				memset(cc, 0, Tcmdsize);
				tx->n--;

				wakeup(tx);
			}
		}

		len &= 0x3fff;
		if(len < 4 || type == 0)
			continue;

		len -= 4;
		switch(type){
		case 1:		/* microcontroller ready */
			setfwinfo(ctlr, d, len);
			break;
		case 24:	/* add node done */
			break;
		case 28:	/* tx done */
			break;
		case 102:	/* calibration result (Type5000 only) */
			break;
		case 103:	/* calibration done (Type5000 only) */
			break;
		case 130:	/* start scan */
			break;
		case 132:	/* stop scan */
			break;
		case 156:	/* rx statistics */
			break;
		case 157:	/* beacon statistics */
			break;
		case 161:	/* state changed */
			break;
		case 162:	/* beacon missed */
			break;
		case 192:	/* rx phy */
			break;
		case 195:	/* rx done */
			if(d + 60 > b->lim)
				break;
			d += 60;
		case 193:	/* mpdu rx done */
			if(d + 4 > b->lim)
				break;
			len = get16(d); d += 4;
			if(d + len + 4 > b->lim)
				break;
			if((get32(d + len) & 3) != 3)
				break;
			if(ctlr->wifi == nil)
				break;
			if(rbplant(ctlr, rx->i) < 0)
				break;
			b->rp = d;
			b->wp = d + len;
			wifiiq(ctlr->wifi, b);
			continue;
		case 197:	/* rx compressed ba */
			break;
		}
		/* paranoia: clear the descriptor */
		memset(b->rp, 0, Rdscsize);
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
		iprint("#l%d: fatal firmware error\n", edev->ctlrno);
		dumpctlr(ctlr);
	}
	ctlr->wait.m |= isr;
	if(ctlr->wait.m & ctlr->wait.w){
		ctlr->wait.r = ctlr->wait.m & ctlr->wait.w;
		ctlr->wait.m &= ~ctlr->wait.r;
		wakeup(&ctlr->wait);
	}
done:
	csr32w(ctlr, Imr, ctlr->ie);
	iunlock(ctlr);
}

static Ctlr *iwlhead, *iwltail;

static void
iwlpci(void)
{
	Pcidev *pdev;
	
	pdev = nil;
	while(pdev = pcimatch(pdev, 0, 0)) {
		Ctlr *ctlr;
		void *mem;
		
		if(pdev->ccrb != 2 || pdev->ccru != 0x80)
			continue;
		if(pdev->vid != 0x8086)
			continue;

		switch(pdev->did){
		default:
			continue;
		case 0x4236:	/* WiFi Link 5300 AGN */
		case 0x4237:	/* Wifi Link 5200 AGN */
			break;
		}

		/* Clear device-specific "PCI retry timeout" register (41h). */
		if(pcicfgr8(pdev, 0x41) != 0)
			pcicfgw8(pdev, 0x41, 0);

		/* Clear interrupt disable bit. Hardware bug workaround. */
		if(pdev->pcr & 0x400){
			pdev->pcr &= ~0x400;
			pcicfgw16(pdev, PciPCR, pdev->pcr);
		}

		pcisetbme(pdev);
		pcisetpms(pdev, 0);

		ctlr = malloc(sizeof(Ctlr));
		if(ctlr == nil) {
			print("iwl: unable to alloc Ctlr\n");
			continue;
		}
		ctlr->port = pdev->mem[0].bar & ~0x0F;
		mem = vmap(pdev->mem[0].bar & ~0x0F, pdev->mem[0].size);
		if(mem == nil) {
			print("iwl: can't map %8.8luX\n", pdev->mem[0].bar);
			free(ctlr);
			continue;
		}
		ctlr->nic = mem;
		ctlr->pdev = pdev;
		ctlr->type = (csr32r(ctlr, Rev) >> 4) & 0xF;

		if(ctlrtype[ctlr->type].fwname == nil){
			print("iwl: unsupported controller type %d\n", ctlr->type);
			vunmap(mem, pdev->mem[0].size);
			free(ctlr);
			continue;
		}

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
	edev->irq = ctlr->pdev->intl;
	edev->tbdf = ctlr->pdev->tbdf;
	edev->arg = edev;
	edev->interrupt = iwlinterrupt;
	edev->attach = iwlattach;
	edev->ifstat = iwlifstat;
	edev->ctl = iwlctl;
	edev->promiscuous = nil;
	edev->multicast = nil;
	edev->mbps = 10;

	if(iwlinit(edev) < 0){
		edev->ctlr = nil;
		goto again;
	}
	
	return 0;
}

void
etheriwllink(void)
{
	addethercard("iwl", iwlpnp);
}
