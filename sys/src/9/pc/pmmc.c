/*
 * pci mmc controller.
 *
 * initially written for X230 Ricoh MMC controller.
 * cmdinfo[] table stolen from bcm/emmc.c, thanks richard.
 *
 * for sdhc documentation see: https://www.sdcard.org/
 */
#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/sd.h"

/* registers */
enum {
	Rsdma	= 0x00,
	Rbsize	= 0x04,
	Rbcount	= 0x06,
	Rarg	= 0x08,
	Rmode	= 0x0C,
	Rcmd	= 0x0E,
	Rresp0	= 0x10,
	Rresp1	= 0x14,
	Rresp2	= 0x18,
	Rresp3	= 0x1C,
	Rdat0	= 0x20,
	Rdat1	= 0x22,
	Rpres	= 0x24,
	Rhc	= 0x28,
	Rpwr	= 0x29,
	Rbgc	= 0x2A,
	Rwkc	= 0x2B,
	Rclc	= 0x2C,
	Rtmc	= 0x2E,
	Rsrst	= 0x2F,
	Rnis	= 0x30,
	Reis	= 0x32,
	Rnie	= 0x34,
	Reie	= 0x36,
	Rnise	= 0x38,
	Reise	= 0x3A,
	Ra12	= 0x3C,
	Rcap	= 0x40,
	Rrcap	= 0x44,
	Rxcap	= 0x48,
	Rrxcap	= 0x4C,
	Rfea12	= 0x50,
	Rfeei	= 0x52,
	Radmasr	= 0x54,
	Radmaba	= 0x58,
	Rsists	= 0xFC,
	Rhcver	= 0xFE,
};

/* sts bits */
enum {
	Seint	= 1<<15,
	Snint	= 1<<8,
	Srem	= 1<<7,
	Sins	= 1<<6,
	Srrdy	= 1<<5,
	Swrdy	= 1<<4,
	Sdint	= 1<<3,
	Sbge	= 1<<2,
	Strac	= 1<<1,
	Scmdc	= 1<<0,

	Smask	= 0x81ff,
};

/* err bits */
enum {
	Ea12	= 1<<8,
	Elimit	= 1<<7,
	Edebit	= 1<<6,
	Edcrc	= 1<<5,
	Edtmo	= 1<<4,
	Ecidx	= 1<<3,
	Ecebit	= 1<<2,
	Eccrc	= 1<<1,
	Ectmo	= 1<<0,

	Emask	= 0x1ff,
};

/* present bits */
enum {
	Plsig	= 1<<24,
	Pldat3	= 1<<23,
	Pldat2	= 1<<22,
	Pldat1	= 1<<21,
	Pldat0	= 1<<20,

	Ppswit	= 1<<19,
	Pcrddt	= 1<<18,
	Pcrdst	= 1<<17,
	Pcrdin	= 1<<16,

	Pbufrd	= 1<<11,
	Pbufwr	= 1<<10,
	Ptrard	= 1<<9,
	Ptrawr	= 1<<8,
	Pdat	= 1<<2,

	Pinhbc	= 1<<1,
	Pinhbd	= 1<<0,
};

/* Rmode bits */
enum {
	Mblk	= 1<<5,
	Mrd	= 1<<4,
	Mwr	= 0<<4,
	Ma12	= 1<<2,
	Mcnt	= 1<<1,
	Mdma	= 1<<0,
};

/* command bits */
enum {
	Abort		= 3<<6,
	Isdata		= 1<<5,
	Ixchken		= 1<<4,
	Crcchken	= 1<<3,

	Respmask	= 3,
	Respnone	= 0,
	Resp48busy	= 3,
	Resp48		= 2,
	Resp136		= 1,
};

/* fake for cmdinfo */
enum {
	Blkcnten	= Mblk << 8,
	Multiblock	= Mcnt << 8,
	Card2host	= Mrd << 8,
	Host2card	= Mwr << 8,
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
[17] Resp48 | Isdata | Card2host | Ixchken | Crcchken,
[18] Resp48 | Isdata | Card2host | Multiblock | Blkcnten | Ixchken | Crcchken,
[24] Resp48 | Isdata | Host2card | Ixchken | Crcchken,
[25] Resp48 | Isdata | Host2card | Multiblock | Blkcnten | Ixchken | Crcchken,
[41] Resp48,
[55] Resp48 | Ixchken | Crcchken,
};

typedef struct Ctlr Ctlr;
struct Ctlr {
	Lock;

	Pcidev	*pdev;
	u8int	*mmio;

	int	change;

	u32int	waitsts;
	u32int	waitmsk;
	Rendez	r;

	struct {
		int	bcount;
		int	bsize;
	} io;
};

static Ctlr pmmc[1];

#define CR8(c, off)	*((u8int*)(c->mmio + off))
#define CR16(c, off)	*((u16int*)(c->mmio + off))
#define CR32(c, off)	*((u32int*)(c->mmio + off))

static void
mmcinterrupt(Ureg*, void *arg)
{
	u16int nis, eis;
	Ctlr *c;

	c = arg;
	nis = CR16(c, Rnis);
	if((nis & Smask) == 0)
		return;		/* not for us */

	CR16(c, Rnis) = nis;	/* ack */
	ilock(c);
	eis = 0;
	if((nis & Seint) != 0){
		eis = CR16(c, Reis);
		CR16(c, Reis) = eis;	/* ack */
	}
	if((nis & Snint) != 0)
		CR16(c, Rnie) |= Snint;	/* ack */
	if((nis & (Srem|Sins)) != 0)
		c->change = 1;
	c->waitsts |= nis | (eis << 16);
	if((c->waitsts & c->waitmsk) != 0)
		wakeup(&c->r);
	iunlock(c);
}

static int
pmmcinit(void)
{
	Pcidev *p;

	p = nil;
	while((p = pcimatch(p, 0, 0)) != nil){
		if(p->ccrb == 8 && p->ccru == 5)
			break;
		if(p->vid == 0x1180){	/* Ricoh */
			if(p->did == 0xe822)	/* 5U822 SD/MMC */
				break;
			if(p->did == 0xe823)	/* 5U823 SD/MMC */
				break;
		}
	}

	if(p == nil || p->mem[0].size < 256)
		return -1;

	if(p->did == 0x1180 && p->vid == 0xe823){	/* Ricoh */
		/* Enable SD2.0 mode. */
		pcicfgw8(p, 0xf9, 0xfc);
		pcicfgw8(p, 0x150, 0x10);
		pcicfgw8(p, 0xf9, 0x00);

		/*
		 * Some SD/MMC cards don't work with the default base
		 * clock frequency of 200MHz.  Lower it to 50Hz.
		 */
		pcicfgw8(p, 0xfc, 0x01);
		pcicfgw8(p, 0xe1, 50);
		pcicfgw8(p, 0xfc, 0x00);
	}

	pmmc->mmio = vmap(p->mem[0].bar & ~0x0F, p->mem[0].size);
	if(pmmc->mmio == nil)
		return -1;
	pmmc->pdev = p;
	return 0;
}

static int
pmmcinquiry(char *inquiry, int inqlen)
{
	return snprint(inquiry, inqlen, "MMC Host Controller");
}

static void
softreset(Ctlr *c, int all)
{
	int i, m;

	m = all ? 1 : 6;
	CR8(c, Rsrst) = m;
	for(i=10; i>=0; i--){
		if((CR8(c, Rsrst) & m) == 0)
			break;
		delay(10);
		CR8(c, Rsrst) = 0;
	}
	if(i < 0) iprint("mmc: didnt reset\n");
}

static void
setpower(Ctlr *c, int on)
{
	enum {
		Vcap18	= 1<<26,	Vset18	= 0x05,
		Vcap30	= 1<<25,	Vset30	= 0x06,
		Vcap33	= 1<<24,	Vset33	= 0x07,
	};
	u32int cap, v;

	cap = CR32(c, Rcap);
	v = Vset18;
	if(cap & Vcap30)
		v = Vset30;
	if(cap & Vcap33)
		v = Vset33;
	CR8(c, Rpwr) = on ? ((v<<1) | 1) : 0;
}

static void
setclkfreq(Ctlr *c, int khz)
{
	u32int caps, intfreq;
	int i, div;

	if(khz == 0){
		CR16(c, Rclc) |= ~4;	/* sd clock disable */
		return;
	}

	caps = CR32(c, Rcap);
	intfreq = 1000*((caps >> 8) & 0x3f);
	for(div = 1; div <= 256; div <<= 1){
		if((intfreq / div) <= khz){
			div >>= 1;
			break;
		}
	}
	CR16(c, Rclc) = 0;
	CR16(c, Rclc) = div<<8;
	CR16(c, Rclc) |= 1;	/* int clock enable */
	for(i=1000; i>=0; i--){
		if(CR16(c, Rclc) & 2)	/* int clock stable */
			break;
		delay(10);
	}
	if(i < 0) iprint("mmc: clock didnt stabilize\n");
	CR16(c, Rclc) |= 4;	/* sd clock enable */
}

static void
resetctlr(Ctlr *c)
{
	u32int m;

	ilock(c);
	CR16(c, Rnise) = 0;	/* interrupts off */

	c->change = 0;
	c->waitmsk = c->waitsts = 0;
	softreset(c, 1);

	/* set timeout */
	CR8(c, Rtmc) = 0x0e;

	m = Srem | Sins | Srrdy | Swrdy | Sdint | Sbge | Strac | Scmdc;
	CR16(c, Rnie) = m;
	CR16(c, Reie) = Emask;
	CR16(c, Rnise) = m;
	CR16(c, Reise) = Emask;

	setpower(c, 1);	
	setclkfreq(c, 400);
	iunlock(c);
}

static int
waitcond(void *arg)
{
	Ctlr *c;

	c = arg;
	return (c->waitsts & c->waitmsk) != 0;
}

static u32int
intrwait(Ctlr *c, u32int mask, int tmo)
{
	u32int status;

	ilock(c);
	c->waitmsk = Seint | mask;
	iunlock(c);
	do {
		if(!waserror()){
			tsleep(&c->r, waitcond, c, 100);
			poperror();
		}
		mmcinterrupt(nil, c);
		if(waitcond(c))
			break;
		tmo -= 100;
	} while(tmo > 0);
	ilock(c);
	c->waitmsk = 0;
	status = c->waitsts;
	c->waitsts &= ~(status & mask);
	if((status & mask) == 0 || (status & Seint) != 0){
		/* abort command on timeout/error interrupt */
		softreset(c, 0);
		c->waitsts = 0;
		status = 0;
	}
	iunlock(c);

	return status & mask;
}


static void
pmmcenable(void)
{
	Pcidev *p;
	Ctlr *c;

	c = pmmc;
	p = c->pdev;
	resetctlr(c);
	intrenable(p->intl, mmcinterrupt, c, p->tbdf, "mmc");
}

static int
pmmccmd(u32int cmd, u32int arg, u32int *resp)
{
	u32int status;
	int i, mode;
	Ctlr *c;

	if(cmd >= nelem(cmdinfo) || cmdinfo[cmd] == 0)
		error(Egreg);
	mode = cmdinfo[cmd] >> 8;
	cmd = (cmd << 8) | (cmdinfo[cmd] & 0xFF);

	c = pmmc;

	if(c->change)
		resetctlr(c);
	if((CR32(c, Rpres) & Pcrdin) == 0)
		error("no card");

	status = Pinhbc;
	if((cmd & Isdata) != 0 || (cmd & Respmask) == Resp48busy)
		status |= Pinhbd;
	for(i=1000; (CR32(c, Rpres) & status) != 0 && i>=0; i--)
		delay(1);
	if(i < 0)
		error(Eio);

	ilock(c);
	if((mode & (Mcnt|Mblk)) == (Mcnt|Mblk)){
		CR16(c, Rbsize)  = c->io.bsize;
		CR16(c, Rbcount) = c->io.bcount;
	}
	CR32(c, Rarg) = arg;
	CR16(c, Rmode) = mode;
	CR16(c, Rcmd) = cmd;
	iunlock(c);

	if(!intrwait(c, Scmdc, 1000))
		error(Eio);

	switch(cmd & Respmask){
	case Resp48busy:
		resp[0] = CR32(c, Rresp0);
		if(!intrwait(c, Strac, 3000))
			error(Eio);
		break;
	case Resp48:
		resp[0] = CR32(c, Rresp0);
		break;
	case Resp136:
		resp[0] = CR32(c, Rresp0)<<8;
		resp[1] = CR32(c, Rresp0)>>24 | CR32(c, Rresp1)<<8;
		resp[2] = CR32(c, Rresp1)>>24 | CR32(c, Rresp2)<<8;
		resp[3] = CR32(c, Rresp2)>>24 | CR32(c, Rresp3)<<8;
		break;
	case Respnone:
		resp[0] = 0;
		break;
	}

	cmd >>= 8;
	if(cmd == 0x06){	/* buswidth */
		switch(arg){
		case 0:
			CR8(c, Rhc) &= ~2;
			break;
		case 2:
			CR8(c, Rhc) |= 2;
			break;
		}
	}

	return 0;
}

static void
pmmciosetup(int write, void *buf, int bsize, int bcount)
{
	Ctlr *c;

	USED(write);
	USED(buf);

	if(bsize == 0 || (bsize & 3) != 0)
		error(Egreg);

	c = pmmc;
	c->io.bsize = bsize;
	c->io.bcount = bcount;
}

static void
readblock(Ctlr *c, uchar *buf, int len)
{
	for(len >>= 2; len > 0; len--){
		*((u32int*)buf) = CR32(c, Rdat0);
		buf += 4;
	}
}

static void
writeblock(Ctlr *c, uchar *buf, int len)
{
	for(len >>= 2; len > 0; len--){
		CR32(c, Rdat0) = *((u32int*)buf);
		buf += 4;
	}
}

static void
pmmcio(int write, uchar *buf, int len)
{
	Ctlr *c;
	int n;

	c = pmmc;
	if(len != c->io.bsize*c->io.bcount)
		error(Egreg);
	while(len > 0){
		if(!intrwait(c, write ? Swrdy : Srrdy, 3000))
			error(Eio);
		n = len;
		if(n > c->io.bsize)
			n = c->io.bsize;
		if(write)
			writeblock(c, buf, n);
		else
			readblock(c, buf, n);
		len -= n;
		buf += n;
	}
	if(!intrwait(c, Strac, 1000))
		error(Eio);
}

SDio sdio = {
	"pmmc",
	pmmcinit,
	pmmcenable,
	pmmcinquiry,
	pmmccmd,
	pmmciosetup,
	pmmcio,
};
