/*
 * external mass media controller (mmc / sd host interface)
 *
 * derived from Richard Miller's bcm/emmc.c
 */

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
	SDfreq		= 25000000,	/* standard SD frequency */
	DTO		= 14,		/* data timeout exponent (guesswork) */

	MMCSelect	= 7,		/* mmc/sd card select command */
	Setbuswidth	= 6,		/* mmc/sd set bus width command */
};

enum {
	/* Controller registers */
	Sysaddr			= 0x00>>2,
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
	Capabilities		= 0x40>>2,
	Forceirpt		= 0x50>>2,
	Boottimeout		= 0x60>>2,
	Dbgsel			= 0x64>>2,
	Spiintspt		= 0xf0>>2,
	Slotisrver		= 0xfc>>2,

	/* Control0 */
	Dwidth4			= 1<<1,
	Dwidth1			= 0<<1,

	/* Control1 */
	Srstdata		= 1<<26,	/* reset data circuit */
	Srstcmd			= 1<<25,	/* reset command circuit */
	Srsthc			= 1<<24,	/* reset complete host controller */
	Datatoshift		= 16,		/* data timeout unit exponent */
	Datatomask		= 0xF0000,
	Clkfreq8shift		= 8,		/* SD clock base divider LSBs */
	Clkfreq8mask		= 0xFF00,
	Clkfreqms2shift		= 6,		/* SD clock base divider MSBs */
	Clkfreqms2mask		= 0xC0,
	Clkgendiv		= 0<<5,		/* SD clock divided */
	Clkgenprog		= 1<<5,		/* SD clock programmable */
	Clken			= 1<<2,		/* SD clock enable */
	Pllen			= 1<<3,
	Clkstable		= 1<<1,	
	Clkintlen		= 1<<0,		/* enable internal EMMC clocks */

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
	Multiblock		= 1<<5,
	Host2card		= 0<<4,
	Card2host		= 1<<4,
	Autocmd12		= 1<<2,
	Autocmd23		= 2<<2,
	Blkcnten		= 1<<1,
	Dmaen			= 1<<0,

	/* Interrupt */
	Acmderr		= 1<<24,
	Denderr		= 1<<22,
	Dcrcerr		= 1<<21,
	Dtoerr		= 1<<20,
	Cbaderr		= 1<<19,
	Cenderr		= 1<<18,
	Ccrcerr		= 1<<17,
	Ctoerr		= 1<<16,
	Err		= 1<<15,
	Cardintr	= 1<<8,
	Cardinsert	= 1<<6,
	Readrdy		= 1<<5,
	Writerdy	= 1<<4,
	Dmaintr		= 1<<3,
	Datadone	= 1<<1,
	Cmddone		= 1<<0,

	/* Status */
	Present		= 1<<18,
	Bufread		= 1<<11,
	Bufwrite	= 1<<10,
	Readtrans	= 1<<9,
	Writetrans	= 1<<8,
	Datactive	= 1<<2,
	Datinhibit	= 1<<1,
	Cmdinhibit	= 1<<0,
};

static int cmdinfo[64] = {
[0]  Ixchken,
[2]  Resp136,
[3]  Resp48 | Ixchken | Crcchken,
[6]  Resp48 | Ixchken | Crcchken,
[7]  Resp48busy | Ixchken | Crcchken,
[8]  Resp48 | Ixchken | Crcchken,
[9]  Resp136,
[12] Resp48busy | Ixchken | Crcchken,
[13] Resp48 | Ixchken | Crcchken,
[16] Resp48,
[17] Resp48 | Isdata | Card2host | Ixchken | Crcchken | Dmaen,
[18] Resp48 | Isdata | Card2host | Multiblock | Blkcnten | Ixchken | Crcchken | Dmaen,
[24] Resp48 | Isdata | Host2card | Ixchken | Crcchken | Dmaen,
[25] Resp48 | Isdata | Host2card | Multiblock | Blkcnten | Ixchken | Crcchken | Dmaen,
[41] Resp48,
[55] Resp48 | Ixchken | Crcchken,
};

typedef struct Ctlr Ctlr;
struct Ctlr {
	Rendez	r;
	u32int	*regs;
	int	datadone;
	int	fastclock;
	ulong	extclk;
	int	irq;
};

static Ctlr emmc;

static uint
clkdiv(uint d)
{
	uint v;

	assert(d < 1<<10);
	v = (d << Clkfreq8shift) & Clkfreq8mask;
	v |= ((d >> 8) << Clkfreqms2shift) & Clkfreqms2mask;
	return v;
}

static void
interrupt(Ureg*, void*)
{	
	u32int *r;
	u32int i;

	r = emmc.regs;
	i = r[Interrupt];
	r[Interrupt] = i & (Datadone|Err);
	emmc.datadone = i;
	wakeup(&emmc.r);
}

static int
datadone(void*)
{
	return emmc.datadone;
}

static int
emmcinit(void)
{
	u32int *r;
	int i;

	emmc.extclk = getclkrate(ClkEmmc2);
	emmc.irq = IRQmmc;
	r = (u32int*)(VIRTIO + 0x340000);
	emmc.regs = r;
	r[Control1] = Srsthc;
	for(i = 0; i < 100; i++){
		delay(10);
		if((r[Control1] & Srsthc) == 0)
			return 0;
	}
	print("emmc: reset timeout!\n");
	return -1;
}

static int
emmcinquiry(char *inquiry, int inqlen)
{
	uint ver;

	ver = emmc.regs[Slotisrver] >> 16;
	return snprint(inquiry, inqlen,
		"eMMC SD Host Controller %2.2x Version %2.2x",
		ver&0xFF, ver>>8);
}

static void
emmcenable(void)
{
	int i;

	emmc.regs[Control1] = clkdiv(emmc.extclk / Initfreq - 1) | DTO << Datatoshift |
		Clkgendiv | Clken | Clkintlen;
	for(i = 0; i < 1000; i++){
		delay(1);
		if(emmc.regs[Control1] & Clkstable)
			break;
	}
	if(i == 1000)
		print("SD clock won't initialise!\n");

	emmc.regs[Control1] |= Pllen;
	for(i = 0; i < 1000; i++){
		delay(1);
		if(emmc.regs[Control1] & Clkstable)
			break;
	}
	if(i == 1000)
		print("PLL clock won't initialise!\n");

	emmc.regs[Control0] = (emmc.regs[Control0] & ~0xFF00) | 0xF00;	// VDD1 bus power to 3.3V
	emmc.regs[Irptmask] = ~(Dtoerr|Cardintr|Dmaintr);
	intrenable(emmc.irq, interrupt, nil, BUSUNKNOWN, sdio.name);
}

static int
emmccmd(u32int cmd, u32int arg, u32int *resp)
{
	ulong now;
	u32int *r;
	u32int c;
	u32int i;

	assert(cmd < nelem(cmdinfo) && cmdinfo[cmd] != 0);
	c = (cmd << Indexshift) | cmdinfo[cmd];

	r = emmc.regs;
	if(r[Status] & Cmdinhibit){
		print("emmccmd: need to reset Cmdinhibit intr %ux stat %ux\n",
			r[Interrupt], r[Status]);
		r[Control1] |= Srstcmd;
		while(r[Control1] & Srstcmd)
			;
		while(r[Status] & Cmdinhibit)
			;
	}
	if((c & Isdata || (c & Respmask) == Resp48busy) &&
	    r[Status] & Datinhibit){
		print("emmccmd: need to reset Datinhibit intr %ux stat %ux\n",
			r[Interrupt], r[Status]);
		r[Control1] |= Srstdata;
		while(r[Control1] & Srstdata)
			;
		while(r[Status] & Datinhibit)
			;
	}
	r[Arg1] = arg;
	if((i = r[Interrupt]) != 0){
		if(i != Cardinsert)
			print("emmc: before command, intr was %ux\n", i);
		r[Interrupt] = i;
	}
	coherence();
	r[Cmdtm] = c;
	coherence();
	now = m->ticks;
	while(((i=r[Interrupt])&(Cmddone|Err)) == 0)
		if((long)(m->ticks-now) > HZ)
			break;
	if((i&(Cmddone|Err)) != Cmddone){
		if((i&~Err) != Ctoerr)
			print("emmc: cmd %ux error intr %ux stat %ux\n", c, i, r[Status]);
		r[Interrupt] = i;
		if(r[Status]&Cmdinhibit){
			r[Control1] |= Srstcmd;
			while(r[Control1]&Srstcmd)
				;
		}
		error(Eio);
	}
	r[Interrupt] = i & ~(Datadone|Readrdy|Writerdy);
	switch(c & Respmask){
	case Resp136:
		resp[0] = r[Resp0]<<8;
		resp[1] = r[Resp0]>>24 | r[Resp1]<<8;
		resp[2] = r[Resp1]>>24 | r[Resp2]<<8;
		resp[3] = r[Resp2]>>24 | r[Resp3]<<8;
		break;
	case Resp48:
	case Resp48busy:
		resp[0] = r[Resp0];
		break;
	case Respnone:
		resp[0] = 0;
		break;
	}
	if((c & Respmask) == Resp48busy){
		r[Irpten] = Datadone|Err;
		tsleep(&emmc.r, datadone, 0, 3000);
		i = emmc.datadone;
		emmc.datadone = 0;
		r[Irpten] = 0;
		if((i & Datadone) == 0)
			print("emmcio: no Datadone after CMD%d\n", cmd);
		if(i & Err)
			print("emmcio: CMD%d error interrupt %ux\n",
				cmd, r[Interrupt]);
		r[Interrupt] = i;
	}
	/*
	 * Once card is selected, use faster clock
	 */
	if(cmd == MMCSelect){
		delay(10);
		r[Control1] = clkdiv(emmc.extclk / SDfreq - 1) |
			DTO << Datatoshift | Clkgendiv | Clken | Clkintlen;
		for(i = 0; i < 1000; i++){
			delay(1);
			if(r[Control1] & Clkstable)
				break;
		}
		delay(10);
		emmc.fastclock = 1;
	}
	/*
	 * If card bus width changes, change host bus width
	 */
	if(cmd == Setbuswidth)
		switch(arg){
		case 0:
			r[Control0] &= ~Dwidth4;
			break;
		case 2:
			r[Control0] |= Dwidth4;
			break;
		}
	return 0;
}

static void
emmciosetup(int, void *buf, int bsize, int bcount)
{
	u32int *r;
	int len;

	len = bsize*bcount;
	if(len > (0x1000<<7))
		error(Etoobig);

	dmaflush(1, buf, len);

	r = emmc.regs;
	r[Sysaddr] = dmaaddr(buf);
	r[Blksizecnt] = 7<<12 | bcount<<16 | bsize;
	r[Irpten] = Datadone|Err;
}

static void
emmcio(int write, uchar *buf, int len)
{
	u32int *r;
	int i;

	tsleep(&emmc.r, datadone, 0, 3000);
	i = emmc.datadone;
	emmc.datadone = 0;

	r = emmc.regs;
	r[Irpten] = 0;
	if((i & Datadone) == 0){
		print("emmcio: %d timeout intr %ux stat %ux\n",
			write, i, r[Status]);
		r[Interrupt] = i;
		error(Eio);
	}
	if(i & Err){
		print("emmcio: %d error intr %ux stat %ux\n",
			write, r[Interrupt], r[Status]);
		r[Interrupt] = i;
		error(Eio);
	}
	if(i)
		r[Interrupt] = i;

	if(!write)
		dmaflush(0, buf, len);
}

SDio sdio = {
	"emmc2",
	emmcinit,
	emmcenable,
	emmcinquiry,
	emmccmd,
	emmciosetup,
	emmcio,
};
