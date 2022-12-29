#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/sd.h"

enum {
	/* Controller registers */
	SDMAaddr		= 0x00>>2,
	Blksizecnt		= 0x04>>2,
	Arg1			= 0x08>>2,
	Cmdtm			= 0x0c>>2,
	Resp0			= 0x10>>2,
	Resp1			= 0x14>>2,
	Resp2			= 0x18>>2,
	Resp3			= 0x1c>>2,
	Data			= 0x20>>2,
	Status			= 0x24>>2,

	Control0		= 0x28>>2,
	Control1		= 0x2c>>2,

	Interrupt		= 0x30>>2,
	Irptmask		= 0x34>>2,
	Irpten			= 0x38>>2,

	Control2		= 0x3c>>2,
	Capability		= 0x40>>2,

	Mixctrl			= 0x48>>2,

	Forceirpt		= 0x50>>2,
	Dmadesc			= 0x58>>2,

	Vendorspec		= 0xC0>>2,

	/* Vendorspec */
	ClkEn			= 1<<14,
	PerEn			= 1<<13,
	HclkEn			= 1<<12,
	IpgEn			= 1<<11,
	Vsel			= 1<<1,

	/* Control0 (PROT_CTRL) */
	Dmaselect		= 3<<8,
		DmaSDMA		= 0<<8,
		DmaADMA1	= 1<<8,
		DmaADMA2	= 2<<8,
	EMODE			= 3<<4,
	BE			= 0<<4,
	HBE			= 1<<4,
	LE			= 2<<4,
	DwidthMask		= 3<<1,
		Dwidth8		= 2<<1,
		Dwidth4		= 1<<1,
		Dwidth1		= 0<<1,
	LED			= 1<<0,

	/* Control1 (SYS_CTRL) */
	Srstdata		= 1<<26,	/* reset data circuit */
	Srstcmd			= 1<<25,	/* reset command circuit */
	Srsthc			= 1<<24,	/* reset complete host controller */
	Datatoshift		= 16,		/* data timeout unit exponent */
	Datatomask		= 0xF0000,
	DTO			= 14,		/* data timeout exponent (guesswork) */
	SDCLKFSshift		= 8,
	DVSshift		= 4,

	/* Cmdtm */
	Indexshift		= 24,
	Suspend			= 1<<22,
	Resume			= 2<<22,
	Abort			= 3<<22,
	Isdata			= 1<<21,
	Ixchken			= 1<<20,
	Crcchken		= 1<<19,
	Respmask		= 3<<16,
	Respnone		= 0<<16,
	Resp136			= 1<<16,
	Resp48			= 2<<16,
	Resp48busy		= 3<<16,

	/* Mixctrl */
	Autocmd23		= 1<<7,
	Multiblock		= 1<<5,
	Host2card		= 0<<4,
	Card2host		= 1<<4,
	DdrEn			= 1<<3,
	Autocmd12		= 1<<2,
	Blkcnten		= 1<<1,
	Dmaen			= 1<<0,
	MixCmdMask 		= 0xFF ^ DdrEn,

	/* Interrupt */
	Admaerr		= 1<<28,
	Acmderr		= 1<<24,
	Denderr		= 1<<22,
	Dcrcerr		= 1<<21,
	Dtoerr		= 1<<20,
	Cbaderr		= 1<<19,
	Cenderr		= 1<<18,
	Ccrcerr		= 1<<17,
	Ctoerr		= 1<<16,
	Err		= Admaerr|Acmderr|Denderr|Dcrcerr|Dtoerr|Cbaderr|Cenderr|Ccrcerr|Ctoerr,

	Cardintr	= 1<<8,
	Cardinsert	= 1<<6,
	Readrdy		= 1<<5,
	Writerdy	= 1<<4,
	Dmaintr		= 1<<3,
	Datadone	= 1<<1,
	Cmddone		= 1<<0,

	/* Status */
	Bufread		= 1<<11,
	Bufwrite	= 1<<10,
	Readtrans	= 1<<9,
	Writetrans	= 1<<8,

	Clkstable	= 1<<3,

	Datactive	= 1<<2,
	Datinhibit	= 1<<1,
	Cmdinhibit	= 1<<0,
};

/*
 * ADMA2 descriptor
 *	See SD Host Controller Simplified Specification Version 2.00
 */
typedef struct Adma Adma;
struct Adma {
	u32int	desc;
	u32int	addr;
};

enum {
	/* desc fields */
	Valid		= 1<<0,
	End		= 1<<1,
	Int		= 1<<2,
	Nop		= 0<<4,
	Tran		= 2<<4,
	Link		= 3<<4,
	OLength		= 16,
	/* maximum value for Length field */
	Maxdma		= 1<<12,
};

typedef struct Ctlr Ctlr;
struct Ctlr {
	u32int	*regs;
	int	irq;

	uint	extclk;
	Adma	*dma;

	Rendez	r;
};

static void usdhcinterrupt(Ureg*, void*);

#define RR(ctlr, reg)	((ctlr)->regs[reg])

static void
WR(Ctlr *ctlr, int reg, u32int val)
{
	if(0)print("WR %2.2ux %ux\n", reg<<2, val);
	coherence();
	ctlr->regs[reg] = val;
}

static Adma*
dmaalloc(void *addr, int len)
{
	int n;
	uintptr a;
	Adma *adma, *p;

	a = PADDR(addr);
	n = (len + Maxdma-1) / Maxdma;
	adma = sdmalloc(n * sizeof(Adma));
	for(p = adma; len > 0; p++){
		if(n == 1)
			p->desc = len<<OLength | End | Int | Valid | Tran;
		else
			p->desc = Maxdma<<OLength | Valid | Tran;
		p->addr = a;
		a += Maxdma;
		len -= Maxdma;
		n--;
	}
	cachedwbse(adma, (char*)p - (char*)adma);
	return adma;
}

static void
usdhcclk(Ctlr *ctlr, uint freq)
{
	uint pre_div = 1, post_div = 1, clk = ctlr->extclk;

	while(clk / (pre_div * 16) > freq && pre_div < 256)
		pre_div <<= 1;

	while(clk / (pre_div * post_div) > freq && post_div < 16)
		post_div++;

	WR(ctlr, Vendorspec, RR(ctlr, Vendorspec) & ~ClkEn);
	WR(ctlr, Control1, (pre_div>>1)<<SDCLKFSshift | (post_div-1)<<DVSshift | DTO<<Datatoshift);
	delay(10);
	WR(ctlr, Vendorspec, RR(ctlr, Vendorspec) | ClkEn | PerEn);
	while((RR(ctlr, Status) & Clkstable) == 0)
		;
}

static void
usdhcbus(SDio *io, int width, int speed)
{
	Ctlr *ctlr = io->aux;

	switch(width){
	case 1:
		WR(ctlr, Control0, (RR(ctlr, Control0) & ~DwidthMask) | Dwidth1);
		break;
	case 4:
		WR(ctlr, Control0, (RR(ctlr, Control0) & ~DwidthMask) | Dwidth4);
		break;
	case 8:
		WR(ctlr, Control0, (RR(ctlr, Control0) & ~DwidthMask) | Dwidth8);
		break;
	}

	if(speed)
		usdhcclk(ctlr, speed);
}

static int
datadone(void *arg)
{
	Ctlr *ctlr = arg;
	return RR(ctlr, Interrupt) & (Datadone|Err);
}

static void
usdhcreset(Ctlr *ctlr)
{
	if(0)print("usdhc control %8.8ux %8.8ux %8.8ux\n",
		RR(ctlr, Control0), RR(ctlr, Control1), RR(ctlr, Control2));

	WR(ctlr, Control1, Srsthc);
	delay(10);
	while(RR(ctlr, Control1) & Srsthc)
		;
	WR(ctlr, Control1, Srstdata);
	delay(10);
	WR(ctlr, Control1, 0);
}

static int
usdhc1init(SDio *io)
{
	static Ctlr ctlr[1] = {
		.regs = (u32int*)(VIRTIO+0xB40000),	/* USDHC1 */
		.irq = IRQusdhc1,
	};

	io->aux = ctlr;

	iomuxpad("pad_sd1_reset_b", "gpio2_io10", "~LVTTL ~HYS PUE ~ODE SLOW 255_OHM");

	/* assert reset */
	gpioout(GPIO_PIN(2, 10), 0);

	iomuxpad("pad_sd1_clk", "usdhc1_clk", "~LVTTL HYS PUE ~ODE FAST 45_OHM");
	iomuxpad("pad_sd1_cmd", "usdhc1_cmd", "~LVTTL HYS PUE ~ODE FAST 45_OHM");
	iomuxpad("pad_sd1_data0", "usdhc1_data0", "~LVTTL HYS PUE ~ODE FAST 45_OHM");
	iomuxpad("pad_sd1_data1", "usdhc1_data1", "~LVTTL HYS PUE ~ODE FAST 45_OHM");
	iomuxpad("pad_sd1_data2", "usdhc1_data2", "~LVTTL HYS PUE ~ODE FAST 45_OHM");
	iomuxpad("pad_sd1_data3", "usdhc1_data3", "~LVTTL HYS PUE ~ODE FAST 45_OHM");
	iomuxpad("pad_sd1_data4", "usdhc1_data4", "~LVTTL HYS PUE ~ODE FAST 45_OHM");
	iomuxpad("pad_sd1_data5", "usdhc1_data5", "~LVTTL HYS PUE ~ODE FAST 45_OHM");
	iomuxpad("pad_sd1_data6", "usdhc1_data6", "~LVTTL HYS PUE ~ODE FAST 45_OHM");
	iomuxpad("pad_sd1_data7", "usdhc1_data7", "~LVTTL HYS PUE ~ODE FAST 45_OHM");

	setclkgate("usdhc1.ipg_clk", 0);
	setclkgate("usdhc1.ipg_clk_perclk", 0);
	setclkrate("usdhc1.ipg_clk_perclk", "system_pll1_clk", 200*Mhz);
	setclkgate("usdhc1.ipg_clk_perclk", 1);
	setclkgate("usdhc1.ipg_clk", 1);

	ctlr->extclk = getclkrate("usdhc1.ipg_clk_perclk");
	if(ctlr->extclk <= 0){
		print("%s: usdhc1.ipg_clk_perclk not enabled\n", io->name);
		return -1;
	}

	/* release reset */
	gpioout(GPIO_PIN(2, 10), 1);

	usdhcreset(ctlr);

	return 0;
}

static int
usdhc2init(SDio *io)
{
	static Ctlr ctlr[1] = {
		.regs = (u32int*)(VIRTIO+0xB50000),	/* USDHC2 */
		.irq = IRQusdhc2,
	};

	io->aux = ctlr;

	iomuxpad("pad_sd2_clk", "usdhc2_clk", "~LVTTL ~HYS ~PUE ~ODE SLOW 75_OHM");
	iomuxpad("pad_sd2_cmd", "usdhc2_cmd", "~LVTTL HYS PUE ~ODE SLOW 75_OHM");
	iomuxpad("pad_sd2_data0", "usdhc2_data0", "~LVTTL HYS PUE ~ODE SLOW 75_OHM");
	iomuxpad("pad_sd2_data1", "usdhc2_data1", "~LVTTL HYS PUE ~ODE SLOW 75_OHM");
	iomuxpad("pad_sd2_data2", "usdhc2_data2", "~LVTTL HYS PUE ~ODE SLOW 75_OHM");
	iomuxpad("pad_sd2_data3", "usdhc2_data3", "~LVTTL HYS PUE ~ODE SLOW 75_OHM");

	setclkgate("usdhc2.ipg_clk", 0);
	setclkgate("usdhc2.ipg_clk_perclk", 0);
	setclkrate("usdhc2.ipg_clk_perclk", "system_pll1_clk", 200*Mhz);
	setclkgate("usdhc2.ipg_clk_perclk", 1);
	setclkgate("usdhc2.ipg_clk", 1);

	ctlr->extclk = getclkrate("usdhc2.ipg_clk_perclk");
	if(ctlr->extclk <= 0){
		print("%s: usdhc2.ipg_clk_perclk not enabled\n", io->name);
		return -1;
	}

	usdhcreset(ctlr);

	return 0;
}

static int
usdhcinquiry(SDio*, char *inquiry, int inqlen)
{
	return snprint(inquiry, inqlen, "USDHC Host Controller");
}

static void
usdhcenable(SDio *io)
{
	Ctlr *ctlr = io->aux;

	WR(ctlr, Control0, 0);
	delay(1);
	WR(ctlr, Vendorspec, RR(ctlr, Vendorspec) & ~Vsel);
	WR(ctlr, Control0, LE | Dwidth1 | DmaADMA2);
	WR(ctlr, Control1, 0);
	delay(1);
	WR(ctlr, Vendorspec, RR(ctlr, Vendorspec) | HclkEn | IpgEn);
	usdhcclk(ctlr, 400000);
	WR(ctlr, Irpten, 0);
	WR(ctlr, Irptmask, ~(Cardintr|Dmaintr));
	WR(ctlr, Interrupt, ~0);
	intrenable(ctlr->irq, usdhcinterrupt, ctlr, BUSUNKNOWN, io->name);
}

extern SDiocmd STOP_TRANSMISSION;

static int
usdhccmd(SDio *io, SDiocmd *cmd, u32int arg, u32int *resp)
{
	Ctlr *ctlr = io->aux;
	u32int c;
	int i;
	ulong now;

	if(0)print("%s: %s (%ux)\n", io->name, cmd->name, arg);

	/* using Autocmd12 */
	if(cmd == &STOP_TRANSMISSION)
		return 0;

	c = (u32int)cmd->index << Indexshift;
	switch(cmd->resp){
	case 0:
		c |= Respnone;
		break;
	case 1:
		if(cmd->busy){
			c |= Resp48busy | Ixchken | Crcchken;
			break;
		}
	default:
		c |= Resp48 | Ixchken | Crcchken;
		break;
	case 2:
		c |= Resp136 | Crcchken;
		break;
	case 3:
		c |= Resp48;
		break;
	}
	if(cmd->data){
		if(cmd->data & 1)
			c |= Isdata | Card2host | Dmaen;
		else
			c |= Isdata | Host2card | Dmaen;
		if(cmd->data > 2)
			c |= Multiblock | Blkcnten | Autocmd12;
	}

	if(RR(ctlr, Status) & Cmdinhibit){
		print("%s: need to reset Cmdinhibit intr %ux stat %ux\n",
			io->name, RR(ctlr, Interrupt), RR(ctlr, Status));
		WR(ctlr, Control1, RR(ctlr, Control1) | Srstcmd);
		while(RR(ctlr, Control1) & Srstcmd)
			;
		while(RR(ctlr, Status) & Cmdinhibit)
			;
	}
	if((RR(ctlr, Status) & Datinhibit) &&
	   ((c & Isdata) || (c & Respmask) == Resp48busy)){
		print("%s: need to reset Datinhibit intr %ux stat %ux\n",
			io->name, RR(ctlr, Interrupt), RR(ctlr, Status));
		WR(ctlr, Control1, RR(ctlr, Control1) | Srstdata);
		while(RR(ctlr, Control1) & Srstdata)
			;
		while(RR(ctlr, Status) & Datinhibit)
			;
	}
	while(RR(ctlr, Status) & Datactive)
		;
	WR(ctlr, Arg1, arg);
	if((i = (RR(ctlr, Interrupt) & ~Cardintr)) != 0){
		if(i != Cardinsert)
			print("%s: before command, intr was %ux\n", io->name, i);
		WR(ctlr, Interrupt, i);
	}
	WR(ctlr, Mixctrl, (RR(ctlr, Mixctrl) & ~MixCmdMask) | (c & MixCmdMask));
	WR(ctlr, Cmdtm, c & ~0xFFFF);

	now = MACHP(0)->ticks;
	while(((i=RR(ctlr, Interrupt))&(Cmddone|Err)) == 0)
		if(MACHP(0)->ticks - now > HZ)
			break;
	if((i&(Cmddone|Err)) != Cmddone){
		if((i&Err) != Ctoerr)
			print("%s: %s cmd %ux arg %ux error intr %ux stat %ux\n",
				io->name, cmd->name, c, arg, i, RR(ctlr, Status));
		WR(ctlr, Interrupt, i);
		if(RR(ctlr, Status)&Cmdinhibit){
			WR(ctlr, Control1, RR(ctlr, Control1)|Srstcmd);
			while(RR(ctlr, Control1)&Srstcmd)
				;
		}
		error(Eio);
	}
	WR(ctlr, Interrupt, i & ~(Datadone|Readrdy|Writerdy));
	switch(c & Respmask){
	case Resp136:
		resp[0] = RR(ctlr, Resp0)<<8;
		resp[1] = RR(ctlr, Resp0)>>24 | RR(ctlr, Resp1)<<8;
		resp[2] = RR(ctlr, Resp1)>>24 | RR(ctlr, Resp2)<<8;
		resp[3] = RR(ctlr, Resp2)>>24 | RR(ctlr, Resp3)<<8;
		break;
	case Resp48:
	case Resp48busy:
		resp[0] = RR(ctlr, Resp0);
		break;
	case Respnone:
		resp[0] = 0;
		break;
	}
	return 0;
}

static void
usdhciosetup(SDio *io, int write, void *buf, int bsize, int bcount)
{
	Ctlr *ctlr = io->aux;
	int len = bsize * bcount;
	assert(((uintptr)buf&3) == 0);
	assert((len&3) == 0);
	assert(bsize <= 2048);
	WR(ctlr, Blksizecnt, bcount<<16 | bsize);
	if(ctlr->dma)
		sdfree(ctlr->dma);
	ctlr->dma = dmaalloc(buf, len);
	if(write)
		cachedwbse(buf, len);
	else
		cachedwbinvse(buf, len);
	WR(ctlr, Dmadesc, PADDR(ctlr->dma));
}

static void
usdhcio(SDio *io, int write, uchar *buf, int len)
{
	Ctlr *ctlr = io->aux;
	u32int i;

	WR(ctlr, Irpten, RR(ctlr, Irpten) | Datadone|Err);
	while(waserror())
		;
	sleep(&ctlr->r, datadone, ctlr);
	poperror();
	WR(ctlr, Irpten, RR(ctlr, Irpten) & ~(Datadone|Err));
	i = RR(ctlr, Interrupt);
	if((i & (Datadone|Err)) != Datadone){
		print("%s: %s error intr %ux stat %ux\n", io->name,
			write? "write" : "read", i, RR(ctlr, Status));
		WR(ctlr, Interrupt, i);
		error(Eio);
	}
	WR(ctlr, Interrupt, i);
	if(!write)
		cachedinvse(buf, len);
}

static void
usdhcinterrupt(Ureg*, void *arg)
{
	Ctlr *ctlr = arg;
	u32int i;

	i = RR(ctlr, Interrupt);
	if(i&(Datadone|Err))
		wakeup(&ctlr->r);
	WR(ctlr, Irpten, RR(ctlr, Irpten) & ~i);
}

void
usdhclink(void)
{
	static SDio usdhc1 = {
		"usdhc1",
		usdhc1init,
		usdhcenable,
		usdhcinquiry,
		usdhccmd,
		usdhciosetup,
		usdhcio,
		usdhcbus,
		.nomultiwrite = 1,
	};
	static SDio usdhc2 = {
		"usdhc2",
		usdhc2init,
		usdhcenable,
		usdhcinquiry,
		usdhccmd,
		usdhciosetup,
		usdhcio,
		usdhcbus,
		.nomultiwrite = 1,
	};

	addmmcio(&usdhc2);
	addmmcio(&usdhc1);
}
