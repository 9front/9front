#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/sd.h"

enum {
	Initfreq	= 400000,	/* initialisation frequency for MMC */
	SDfreq		= 25*Mhz,	/* standard SD frequency */
	SDfreqhs	= 50*Mhz,	/* highspeed frequency */
	DTO		= 14,		/* data timeout exponent (guesswork) */

	GoIdle		= 0,		/* mmc/sdio go idle state */
	MMCSelect	= 7,		/* mmc/sd card select command */
	Setbuswidth	= 6,		/* mmc/sd set bus width command */
	Switchfunc	= 6,		/* mmc/sd switch function command */
	Voltageswitch	= 11,		/* md/sdio switch to 1.8V */
	IORWdirect	= 52,		/* sdio read/write direct command */
	IORWextended	= 53,		/* sdio read/write extended command */
	Appcmd		= 55,		/* mmc/sd application command prefix */
};

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

static int cmdinfo[64] = {
[0]  Ixchken,
[2]  Resp136,
[3]  Resp48 | Ixchken | Crcchken,
[5]  Resp48,
[6]  Resp48 | Ixchken | Crcchken,
[7]  Resp48 | Ixchken | Crcchken,
[8]  Resp48 | Ixchken | Crcchken,
[9]  Resp136,
[11] Resp48 | Ixchken | Crcchken,
[13] Resp48 | Ixchken | Crcchken,
[16] Resp48,
[17] Resp48 | Isdata | Card2host | Ixchken | Crcchken,
[18] Resp48 | Isdata | Card2host | Multiblock | Blkcnten | Ixchken | Crcchken | Autocmd12,
[24] Resp48 | Isdata | Host2card | Ixchken | Crcchken,
[25] Resp48 | Isdata | Host2card | Multiblock | Blkcnten | Ixchken | Crcchken | Autocmd12,
[41] Resp48,
[52] Resp48 | Ixchken | Crcchken,
[53] Resp48 | Ixchken | Crcchken | Isdata,
[55] Resp48 | Ixchken | Crcchken,
};

typedef struct Adma Adma;
typedef struct Ctlr Ctlr;

/*
 * ADMA2 descriptor
 *	See SD Host Controller Simplified Specification Version 2.00
 */
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

struct Ctlr {
	Rendez	r;
	int	fastclock;
	uint	extclk;
	int	appcmd;
	Adma	*dma;
};

static Ctlr usdhc;

static void usdhcinterrupt(Ureg*, void*);

static u32int *regs = (u32int*)(VIRTIO+0xB50000);	/* USDHC2 */
#define RR(reg)	(regs[reg])

static void
WR(int reg, u32int val)
{
	if(0)print("WR %2.2ux %ux\n", reg<<2, val);
	coherence();
	regs[reg] = val;
}

static Adma*
dmaalloc(void *addr, int len)
{
	int n;
	uintptr a;
	Adma *adma, *p;

	a = (uintptr)addr;
	n = (len + Maxdma-1) / Maxdma;
	adma = sdmalloc(n * sizeof(Adma));
	for(p = adma; len > 0; p++){
		p->desc = Valid | Tran;
		if(n == 1)
			p->desc |= len<<OLength | End | Int;
		else
			p->desc |= Maxdma<<OLength;
		p->addr = PADDR(a);
		a += Maxdma;
		len -= Maxdma;
		n--;
	}
	cachedwbse(adma, (char*)p - (char*)adma);
	return adma;
}

static void
usdhcclk(uint freq)
{
	uint pre_div = 1, post_div = 1, clk = usdhc.extclk;

	while(clk / (pre_div * 16) > freq && pre_div < 256)
		pre_div <<= 1;

	while(clk / (pre_div * post_div) > freq && post_div < 16)
		post_div++;

	WR(Vendorspec, RR(Vendorspec) & ~ClkEn);
	WR(Control1, (pre_div>>1)<<SDCLKFSshift | (post_div-1)<<DVSshift | DTO<<Datatoshift);
	delay(10);
	WR(Vendorspec, RR(Vendorspec) | ClkEn | PerEn);
	while((RR(Status) & Clkstable) == 0)
		;
}

static int
datadone(void*)
{
	return RR(Interrupt) & (Datadone|Err);
}

static int
usdhcinit(void)
{
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

	usdhc.extclk = getclkrate("usdhc2.ipg_clk_perclk");
	if(usdhc.extclk <= 0){
		print("usdhc: usdhc2.ipg_clk_perclk not enabled\n");
		return -1;
	}

	if(0)print("usdhc control %8.8ux %8.8ux %8.8ux\n",
		RR(Control0), RR(Control1), RR(Control2));

	WR(Control1, Srsthc);
	delay(10);
	while(RR(Control1) & Srsthc)
		;
	WR(Control1, Srstdata);
	delay(10);
	WR(Control1, 0);
	return 0;
}

static int
usdhcinquiry(char *inquiry, int inqlen)
{
	return snprint(inquiry, inqlen, "USDHC Host Controller");
}

static void
usdhcenable(void)
{
	WR(Control0, 0);
	delay(1);
	WR(Vendorspec, RR(Vendorspec) & ~Vsel);
	WR(Control0, LE | Dwidth1 | DmaADMA2);
	WR(Control1, 0);
	delay(1);
	WR(Vendorspec, RR(Vendorspec) | HclkEn | IpgEn);
	usdhcclk(Initfreq);
	WR(Irpten, 0);
	WR(Irptmask, ~(Cardintr|Dmaintr));
	WR(Interrupt, ~0);
	intrenable(IRQusdhc2, usdhcinterrupt, nil, BUSUNKNOWN, "usdhc2");
}

static int
usdhccmd(u32int cmd, u32int arg, u32int *resp)
{
	u32int c;
	int i;
	ulong now;

	/* using Autocmd12 */
	if(cmd == 12)
		return 0;

	assert(cmd < nelem(cmdinfo) && cmdinfo[cmd] != 0);
	c = (cmd << Indexshift) | cmdinfo[cmd];
	/*
	 * CMD6 may be Setbuswidth or Switchfunc depending on Appcmd prefix
	 */
	if(cmd == Switchfunc && !usdhc.appcmd)
		c |= Isdata|Card2host;
	if(c & Isdata)
		c |= Dmaen;
	if(cmd == IORWextended){
		if(arg & (1<<31))
			c |= Host2card;
		else
			c |= Card2host;
		if((RR(Blksizecnt)&0xFFFF0000) != 0x10000)
			c |= Multiblock | Blkcnten;
	}
	/*
	 * GoIdle indicates new card insertion: reset bus width & speed
	 */
	if(cmd == GoIdle){
		WR(Control0, (RR(Control0) & ~DwidthMask) | Dwidth1);
		usdhcclk(Initfreq);
	}
	if(RR(Status) & Cmdinhibit){
		print("usdhccmd: need to reset Cmdinhibit intr %ux stat %ux\n",
			RR(Interrupt), RR(Status));
		WR(Control1, RR(Control1) | Srstcmd);
		while(RR(Control1) & Srstcmd)
			;
		while(RR(Status) & Cmdinhibit)
			;
	}
	if((RR(Status) & Datinhibit) &&
	   ((c & Isdata) || (c & Respmask) == Resp48busy)){
		print("usdhccmd: need to reset Datinhibit intr %ux stat %ux\n",
			RR(Interrupt), RR(Status));
		WR(Control1, RR(Control1) | Srstdata);
		while(RR(Control1) & Srstdata)
			;
		while(RR(Status) & Datinhibit)
			;
	}
	while(RR(Status) & Datactive)
		;
	WR(Arg1, arg);
	if((i = (RR(Interrupt) & ~Cardintr)) != 0){
		if(i != Cardinsert)
			print("usdhc: before command, intr was %ux\n", i);
		WR(Interrupt, i);
	}
	WR(Mixctrl, (RR(Mixctrl) & ~MixCmdMask) | (c & MixCmdMask));
	WR(Cmdtm, c & ~0xFFFF);

	now = MACHP(0)->ticks;
	while(((i=RR(Interrupt))&(Cmddone|Err)) == 0)
		if(MACHP(0)->ticks - now > HZ)
			break;
	if((i&(Cmddone|Err)) != Cmddone){
		if((i&~(Err|Cardintr)) != Ctoerr)
			print("usdhc: cmd %ux arg %ux error intr %ux stat %ux\n", c, arg, i, RR(Status));
		WR(Interrupt, i);
		if(RR(Status)&Cmdinhibit){
			WR(Control1, RR(Control1)|Srstcmd);
			while(RR(Control1)&Srstcmd)
				;
		}
		error(Eio);
	}
	WR(Interrupt, i & ~(Datadone|Readrdy|Writerdy));
	switch(c & Respmask){
	case Resp136:
		resp[0] = RR(Resp0)<<8;
		resp[1] = RR(Resp0)>>24 | RR(Resp1)<<8;
		resp[2] = RR(Resp1)>>24 | RR(Resp2)<<8;
		resp[3] = RR(Resp2)>>24 | RR(Resp3)<<8;
		break;
	case Resp48:
	case Resp48busy:
		resp[0] = RR(Resp0);
		break;
	case Respnone:
		resp[0] = 0;
		break;
	}
	if((c & Respmask) == Resp48busy){
		WR(Irpten, RR(Irpten)|Datadone|Err);
		tsleep(&usdhc.r, datadone, 0, 1000);
		i = RR(Interrupt);
		if((i & Datadone) == 0)
			print("usdhcio: no Datadone in %x after CMD%d\n", i, cmd);
		if(i & Err)
			print("usdhcio: CMD%d error interrupt %ux\n",
				cmd, RR(Interrupt));
		if(i != 0) WR(Interrupt, i);
	}
	/*
	 * Once card is selected, use faster clock
	 */
	if(cmd == MMCSelect){
		usdhcclk(SDfreq);
		usdhc.fastclock = 1;
	}
	if(cmd == Setbuswidth){
		if(usdhc.appcmd){
			/*
			 * If card bus width changes, change host bus width
			 */
			switch(arg){
			case 0:
				WR(Control0, (RR(Control0) & ~DwidthMask) | Dwidth1);
				break;
			case 2:
				WR(Control0, (RR(Control0) & ~DwidthMask) | Dwidth4);
				break;
			}
		} else {
			/*
			 * If card switched into high speed mode, increase clock speed
			 */
			if((arg&0x8000000F) == 0x80000001){
				delay(1);
				usdhcclk(SDfreqhs);
				delay(1);
			}
		}
	}else if(cmd == IORWdirect && (arg & ~0xFF) == (1<<31|0<<28|7<<9)){
		switch(arg & 0x3){
		case 0:
			WR(Control0, (RR(Control0) & ~DwidthMask) | Dwidth1);
			break;
		case 2:
			WR(Control0, (RR(Control0) & ~DwidthMask) | Dwidth4);
			break;
		}
	}
	usdhc.appcmd = (cmd == Appcmd);
	return 0;
}

static void
usdhciosetup(int write, void *buf, int bsize, int bcount)
{
	int len = bsize * bcount;
	assert(((uintptr)buf&3) == 0);
	assert((len&3) == 0);
	assert(bsize <= 2048);
	WR(Blksizecnt, bcount<<16 | bsize);
	if(usdhc.dma)
		sdfree(usdhc.dma);
	usdhc.dma = dmaalloc(buf, len);
	if(write)
		cachedwbse(buf, len);
	else
		cachedwbinvse(buf, len);
	WR(Dmadesc, PADDR(usdhc.dma));
}

static void
usdhcio(int write, uchar *buf, int len)
{
	u32int i;

	WR(Irpten, RR(Irpten) | Datadone|Err);
	tsleep(&usdhc.r, datadone, 0, 3000);
	WR(Irpten, RR(Irpten) & ~(Datadone|Err));
	i = RR(Interrupt);
	if((i & (Datadone|Err)) != Datadone){
		print("sdhc: %s error intr %ux stat %ux\n",
			write? "write" : "read", i, RR(Status));
		WR(Interrupt, i);
		error(Eio);
	}
	WR(Interrupt, i);
	if(!write)
		cachedinvse(buf, len);
}

static void
usdhcinterrupt(Ureg*, void*)
{	
	u32int i;

	i = RR(Interrupt);
	if(i&(Datadone|Err))
		wakeup(&usdhc.r);
	WR(Irpten, RR(Irpten) & ~i);
}

SDio sdio = {
	"usdhc",
	usdhcinit,
	usdhcenable,
	usdhcinquiry,
	usdhccmd,
	usdhciosetup,
	usdhcio,
	.highspeed = 1,
};
