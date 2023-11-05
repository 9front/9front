/*
 * bcm2835 i2c controller
 *
 *	Only i2c1 is supported.
 *	i2c2 is reserved for HDMI.
 *	i2c0 SDA0/SCL0 pins are not routed to P1 connector (except for early Rev 0 boards)
 *
 * maybe hardware problems lurking, see: https://github.com/raspberrypi/linux/issues/254
 *
 * modified by adventuresin9@gmail.com to work with 9Front's port/devi2c
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"../port/error.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/i2c.h"

#define I2CREGS	(VIRTIO+0x804000)


typedef struct Ctlr Ctlr;
typedef struct Bsc Bsc;

/*
 * Registers for Broadcom Serial Controller (i2c compatible)
 */
struct Bsc {
	u32int	ctrl;
	u32int	stat;
	u32int	dlen;
	u32int	addr;
	u32int	fifo;
	u32int	clkdiv;		/* default 1500 => 100 KHz assuming 150Mhz input clock */
	u32int	delay;		/* default (48<<16)|48 falling:rising edge */
	u32int	clktimeout;	/* default 64 */
};

/*
 * Per-controller info
 */
struct Ctlr {
	Bsc	*regs;
	Rendez r;
	Lock ilock;
};

static Ctlr ctlr;

enum {
	/* ctrl */
	I2cen	= 1<<15,	/* I2c enable */
	Intr	= 1<<10,	/* interrupt on reception */
	Intt	= 1<<9,		/* interrupt on transmission */
	Intd	= 1<<8,		/* interrupt on done */
	Start	= 1<<7,		/* aka ST, start a transfer */
	Clear	= 1<<4,		/* clear fifo */
	Read	= 1<<0,		/* read transfer */
	Write	= 0<<0,		/* write transfer */

	/* stat */
	Clkt	= 1<<9,		/* clock stretch timeout */
	Err	= 1<<8,			/* NAK */
	Rxf	= 1<<7,			/* RX fifo full */
	Txe	= 1<<6,			/* TX fifo full */
	Rxd	= 1<<5,			/* RX fifo has data */
	Txd	= 1<<4,			/* TX fifo has space */
	Rxr	= 1<<3,			/* RX fiio needs reading */
	Txw	= 1<<2,			/* TX fifo needs writing */
	Done	= 1<<1,		/* transfer done */
	Ta	= 1<<0,			/* Transfer active */

	/* pin settings */
	SDA0Pin	= 2,
	SCL0Pin	= 3,
};

static void
i2cinterrupt(Ureg*, void*)
{
	Bsc *r;
	int st;

	ilock(&ctlr.ilock);
	r = ctlr.regs;
	st = 0;
	if((r->ctrl & Intr) && (r->stat & Rxd))
		st |= Intr;
	if((r->ctrl & Intt) && (r->stat & Txd))
		st |= Intt;
	if(r->stat & Done)
		st |= Intd;
	if(st){
		r->ctrl &= ~st;
		wakeup(&ctlr.r);
	}
	iunlock(&ctlr.ilock);
}

static int
i2cready(void *st)
{
	return (ctlr.regs->stat & (uintptr)st);
}

static int
i2cinit(I2Cbus*)
{
	ctlr.regs = (Bsc*)I2CREGS;
	ctlr.regs->clkdiv = getclkrate(ClkCore) / 100000;

	gpiosel(SDA0Pin, Alt0);
	gpiosel(SCL0Pin, Alt0);
	gpiopullup(SDA0Pin);
	gpiopullup(SCL0Pin);

	intrenable(IRQi2c, i2cinterrupt, nil, BUSUNKNOWN, "i2c");

	return 0;
}

/*
 *	Basic I²C driver for Raspberry Pi
 *	subaddressing wasn't reliable, so it is just not allowed
 *
 *	10 bit addressing is also disabled.
 */
static int
i2cio(I2Cdev *dev, uchar *pkt, int olen, int ilen)
{
	Bsc *r;
	uchar *p;
	int st;
	int o;
	int rw, len;
	uint addr;
	o = 0;

	if(dev->subaddr > 0){				/* subaddressing in not implemented */
		return -1;
	}

	if((pkt[0] & 0xF8) == 0xF0){		/* b11110xxx reserved for 10bit addressing*/
		return -1;
	}

	rw = pkt[0] & 1;					/* rw bit is first bit of pkt[0], read == 1 */
	addr = dev->addr;
	pkt++;								/* move past device addr packet */
	o++;								/* have to at least return processing the dev addr */

	/* 
	 * If 9Front is just running a probe
	 * return 1,
	 * else the controller throws an NAK error
	 * when doing a write with just the dev addr
	 */

	if((olen == 1) && (ilen == 0)){
		return 1;
	}

	r = ctlr.regs;
	r->ctrl = I2cen | Clear;
	r->addr = addr;
	r->stat = Clkt|Err|Done;

	len = (olen - 1) + ilen;
	r->dlen = len;
	r->ctrl = I2cen | Start | Intd | rw;

	p = pkt;
	st = rw == Read? Rxd : Txd;
	while(len > 0){
		while((r->stat & (st|Done)) == 0){
			r->ctrl |= rw == Read? Intr : Intt;
			sleep(&ctlr.r, i2cready, (void*)(st|Done));
		}
		if(r->stat & (Err|Clkt)){
			r->ctrl = 0;
			return -1;
		}
		if(rw == Read){
			do{
				*p++ = r->fifo;
				len--;
				o++;
			}while ((r->stat & Rxd) && len > 0);
		}else{
			do{
				r->fifo = *p++;
				len--;
				o++;
			}while((r->stat & Txd) && len > 0);
		}
	}

	while((r->stat & Done) == 0)
		sleep(&ctlr.r, i2cready, (void*)Done);
	if(r->stat & (Err|Clkt)){
		r->ctrl = 0;
		return -1;
	}
	r->ctrl = 0;
	return o;
}


void
i2cbcmlink(void)
{
	static I2Cbus i2c = {"i2c1", 400000, &ctlr, i2cinit, i2cio};
	addi2cbus(&i2c);
}
