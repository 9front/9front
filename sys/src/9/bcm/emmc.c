/*
 * bcm2835 external mass media controller (mmc / sd host interface)
 *
 * Copyright © 2012 Richard Miller <r.miller@acm.org>
 */

/*
	Not officially documented: emmc can be connected to different gpio pins
		48-53 (SD card)
		22-27 (P1 header)
		34-39 (wifi - pi3 only)
	using ALT3 function to activate the required routing
*/

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/sd.h"

#define EMMCREGS	(VIRTIO+0x300000)

enum {
	Extfreq		= 100*Mhz,	/* guess external clock frequency if */
					/* not available from vcore */
};

enum {
	/* Controller registers */
	Arg2			= 0x00>>2,
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
	Forceirpt		= 0x50>>2,
	Boottimeout		= 0x70>>2,
	Dbgsel			= 0x74>>2,
	Exrdfifocfg		= 0x80>>2,
	Exrdfifoen		= 0x84>>2,
	Tunestep		= 0x88>>2,
	Tunestepsstd		= 0x8c>>2,
	Tunestepsddr		= 0x90>>2,
	Spiintspt		= 0xf0>>2,
	Slotisrver		= 0xfc>>2,

	/* Control0 */
	Hispeed			= 1<<2,	
	Dwidth4			= 1<<1,
	Dwidth1			= 0<<1,
	DwidthMask		= Dwidth4,

	/* Control1 */
	Srstdata		= 1<<26,	/* reset data circuit */
	Srstcmd			= 1<<25,	/* reset command circuit */
	Srsthc			= 1<<24,	/* reset complete host controller */
	Datatoshift		= 16,		/* data timeout unit exponent */
		DTO		= 14,		/* data timeout exponent (guesswork) */
	Datatomask		= 0xF0000,
	Clkfreq8shift		= 8,		/* SD clock base divider LSBs */
	Clkfreq8mask		= 0xFF00,
	Clkfreqms2shift		= 6,		/* SD clock base divider MSBs */
	Clkfreqms2mask		= 0xC0,
	Clkgendiv		= 0<<5,		/* SD clock divided */
	Clkgenprog		= 1<<5,		/* SD clock programmable */
	Clken			= 1<<2,		/* SD clock enable */
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
	Cardintr	= 1<<8,		/* not in Broadcom datasheet */
	Cardinsert	= 1<<6,		/* not in Broadcom datasheet */
	Readrdy		= 1<<5,
	Writerdy	= 1<<4,
	Datadone	= 1<<1,
	Cmddone		= 1<<0,

	/* Status */
	Bufread		= 1<<11,	/* not in Broadcom datasheet */
	Bufwrite	= 1<<10,	/* not in Broadcom datasheet */
	Readtrans	= 1<<9,
	Writetrans	= 1<<8,
	Datactive	= 1<<2,
	Datinhibit	= 1<<1,
	Cmdinhibit	= 1<<0,
};

typedef struct Ctlr Ctlr;
struct Ctlr {
	Rendez	cardr;
	Rendez	r;
	int	fastclock;
	ulong	extclk;
};

static Ctlr emmc;

static void emmcinterrupt(Ureg*, void*);

static void
WR(int reg, u32int val)
{
	u32int *r = (u32int*)EMMCREGS;

	if(0)print("WR %2.2ux %ux\n", reg<<2, val);
	microdelay(emmc.fastclock ? 2: 20);
	coherence();
	r[reg] = val;
}

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
emmcclk(uint freq)
{
	u32int *r = (u32int*)EMMCREGS;
	uint div;
	int i;

	div = emmc.extclk / (freq<<1);
	if(emmc.extclk / (div<<1) > freq)
		div++;
	WR(Control1, clkdiv(div) |
		DTO<<Datatoshift | Clkgendiv | Clken | Clkintlen);
	emmc.fastclock = freq > 400000;
	for(i = 0; i < 1000; i++){
		delay(1);
		if(r[Control1] & Clkstable)
			break;
	}
	if(i == 1000)
		print("emmc: can't set clock to %ud\n", freq);
}

static void
emmcbus(SDio*, int width, int speed)
{
	u32int *r = (u32int*)EMMCREGS;

	switch(width){
	case 1:
		WR(Control0, (r[Control0] & ~DwidthMask) | Dwidth1);
		break;
	case 4:
		WR(Control0, (r[Control0] & ~DwidthMask) | Dwidth4);
		break;
	}

	if(speed)
		emmcclk(speed);
}

static int
datadone(void*)
{
	u32int *r = (u32int*)EMMCREGS;
	int i;

	i = r[Interrupt];
	return i & (Datadone|Err);
}

static int
cardintready(void*)
{
	u32int *r = (u32int*)EMMCREGS;
	int i;

	i = r[Interrupt];
	return i & Cardintr;
}

static int
emmcinit(SDio*)
{
	u32int *r = (u32int*)EMMCREGS;
	ulong clk;

	clk = getclkrate(ClkEmmc);
	if(clk == 0){
		clk = Extfreq;
		print("emmc: assuming external clock %lud Mhz\n", clk/1000000);
	}
	emmc.extclk = clk;
	if(0)print("emmc control %8.8ux %8.8ux %8.8ux\n",
		r[Control0], r[Control1], r[Control2]);
	WR(Control1, Srsthc);
	delay(10);
	while(r[Control1] & Srsthc)
		;
	WR(Control1, Srstdata);
	delay(10);
	WR(Control1, 0);
	return 0;
}

static int
emmcinquiry(SDio*, char *inquiry, int inqlen)
{
	u32int *r = (u32int*)EMMCREGS;
	uint ver;

	ver = r[Slotisrver] >> 16;
	return snprint(inquiry, inqlen,
		"Arasan eMMC SD Host Controller %2.2x Version %2.2x",
		ver&0xFF, ver>>8);
}

static void
emmcenable(SDio *io)
{
	emmcclk(400000);
	WR(Irpten, 0);
	WR(Irptmask, ~0);
	WR(Interrupt, ~0);
	intrenable(IRQmmc, emmcinterrupt, nil, BUSUNKNOWN, io->name);
}

static int
emmccardintr(SDio*, int wait)
{
	u32int *r = (u32int*)EMMCREGS;
	int i;

	WR(Interrupt, Cardintr);
	while(((i = r[Interrupt]) & Cardintr) == 0){
		if(!wait)
			return 0;
		WR(Irpten, r[Irpten] | Cardintr);
		sleep(&emmc.cardr, cardintready, 0);
	}
	WR(Interrupt, Cardintr);
	return i;
}

static int
emmccmd(SDio*, SDiocmd *cmd, u32int arg, u32int *resp)
{
	u32int *r = (u32int*)EMMCREGS;
	u32int c;
	int i;
	ulong now;

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
			c |= Isdata | Card2host;
		else
			c |= Isdata | Host2card;
		if(cmd->data > 2)
			c |= Multiblock | Blkcnten;
	}

	if((r[Status] & Datinhibit) &&
	   ((c & Isdata) || (c & Respmask) == Resp48busy)){
		print("emmccmd: need to reset Cmdinhibit intr %ux stat %ux\n",
			r[Interrupt], r[Status]);
		WR(Control1, r[Control1] | Srstcmd);
		while(r[Control1] & Srstcmd)
			;
		while(r[Status] & Cmdinhibit)
			;
	}
	if((c & Isdata || (c & Respmask) == Resp48busy) &&
	    r[Status] & Datinhibit){
		print("emmccmd: need to reset Datinhibit intr %ux stat %ux\n",
			r[Interrupt], r[Status]);
		WR(Control1, r[Control1] | Srstdata);
		while(r[Control1] & Srstdata)
			;
		while(r[Status] & Datinhibit)
			;
	}
	WR(Arg1, arg);
	if((i = (r[Interrupt] & ~Cardintr)) != 0){
		if(i != Cardinsert)
			print("emmc: before command, intr was %ux\n", i);
		WR(Interrupt, i);
	}
	WR(Cmdtm, c);
	now = MACHP(0)->ticks;
	while(((i=r[Interrupt])&(Cmddone|Err)) == 0)
		if(MACHP(0)->ticks - now > HZ)
			break;
	if((i&(Cmddone|Err)) != Cmddone){
		if((i&~(Err|Cardintr)) != Ctoerr)
			print("emmc: %s cmd %ux arg %ux error intr %ux stat %ux\n",
				cmd->name, c, arg, i, r[Status]);
		WR(Interrupt, i);
		if(r[Status]&Cmdinhibit){
			WR(Control1, r[Control1]|Srstcmd);
			while(r[Control1]&Srstcmd)
				;
		}
		error(Eio);
	}
	WR(Interrupt, i & ~(Datadone|Readrdy|Writerdy));
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
		WR(Irpten, r[Irpten]|Datadone|Err);
		tsleep(&emmc.r, datadone, 0, 3000);
		i = r[Interrupt];
		if((i & Datadone) == 0)
			print("emmcio: no Datadone after %s\n", cmd->name);
		if(i & Err)
			print("emmcio: %s error interrupt %ux\n",
				cmd->name, r[Interrupt]);
		WR(Interrupt, i);
	}
	return 0;
}

static void
emmciosetup(SDio*, int write, void *buf, int bsize, int bcount)
{
	USED(write);
	USED(buf);
	WR(Blksizecnt, bcount<<16 | bsize);
}

static void
emmcio(SDio*, int write, uchar *buf, int len)
{
	u32int *r = (u32int*)EMMCREGS;
	int i;

	assert((len&3) == 0);
	if(write)
		dmastart(DmaChanEmmc, DmaDevEmmc, DmaM2D,
			buf, &r[Data], len);
	else
		dmastart(DmaChanEmmc, DmaDevEmmc, DmaD2M,
			&r[Data], buf, len);
	if(dmawait(DmaChanEmmc) < 0)
		error(Eio);
	WR(Irpten, r[Irpten]|Datadone|Err);
	tsleep(&emmc.r, datadone, 0, 3000);
	i = r[Interrupt]&~Cardintr;
	if((i & Datadone) == 0){
		print("emmcio: %d timeout intr %ux stat %ux\n",
			write, i, r[Status]);
		WR(Interrupt, i);
		error(Eio);
	}
	if(i & Err){
		print("emmcio: %d error intr %ux stat %ux\n",
			write, r[Interrupt], r[Status]);
		WR(Interrupt, i);
		error(Eio);
	}
	if(i)
		WR(Interrupt, i);
}

static void
emmcinterrupt(Ureg*, void*)
{	
	u32int *r = (u32int*)EMMCREGS;
	int i;

	i = r[Interrupt];
	if(i&(Datadone|Err))
		wakeup(&emmc.r);
	if(i&Cardintr)
		wakeup(&emmc.cardr);
	WR(Irpten, r[Irpten] & ~i);
}

static void
emmcled(SDio*, int on)
{
	okay(on);
}

static SDio sdio = {
	"emmc",
	emmcinit,
	emmcenable,
	emmcinquiry,
	emmccmd,
	emmciosetup,
	emmcio,
	emmcbus,
	emmcled,
	emmccardintr,
};

void
emmclink(void)
{
	addmmcio(&sdio);
}
