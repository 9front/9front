/*
 * I²C bitbang driver using GPIO pins.
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"../port/error.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/i2c.h"

typedef struct Ctlr Ctlr;
struct Ctlr
{
	uint	sda, scl;
	int	delay;
};

static void
Setpin(uint pin, int val)
{
	gpiosel(pin, val?Input:Output);
	gpioout(pin, val);
}

static int
Getpin(uint pin)
{
	return gpioin(pin);
}

static void
Delay(Ctlr *ctlr)
{
	microdelay(ctlr->delay);
}

static int
Stretch(Ctlr *ctlr)
{
	ulong to;

	to = µs();
	do {
		if(Getpin(ctlr->scl))
			return 0;
	} while(µs() - to < 25*1000);
	return -1;
}

static int
Writebit(Ctlr *ctlr, int bit)
{
	Setpin(ctlr->sda, bit);
	Delay(ctlr);
	Setpin(ctlr->scl, 1);
	Delay(ctlr);
	if(Stretch(ctlr) < 0)
		return -1;
	if(bit && !Getpin(ctlr->sda))
		return -2;
	Setpin(ctlr->scl, 0);
	return 0;
}

static int
Readbit(Ctlr *ctlr)
{
	int bit;

	Setpin(ctlr->sda, 1);
	Delay(ctlr);
	Setpin(ctlr->scl, 1);
	if(Stretch(ctlr) < 0)
		return -1;
	Delay(ctlr);
	bit = Getpin(ctlr->sda);
	Setpin(ctlr->scl, 0);
	return bit;
}

static int
Readbyte(Ctlr *ctlr, int nack)
{
	int byte, i, e;

	byte = 0;
	for(i=0; i<8; i++){
		if((e = Readbit(ctlr)) < 0)
			return e;
		byte <<= 1;
		byte |= e;
	}
	if((e = Writebit(ctlr, nack)) < 0)
		return e;
	return byte;
}

static int
Writebyte(Ctlr *ctlr, int byte)
{
	int i, e;

	for(i=0; i<8; i++){
		if((e = Writebit(ctlr, (byte>>7)&1)) < 0)
			return  e;
		byte <<= 1;
	}
	return Readbit(ctlr);
}

static int
Start(Ctlr *ctlr)
{
	if(!Getpin(ctlr->sda) || !Getpin(ctlr->scl))
		return -2;
	Setpin(ctlr->sda, 0);
	Delay(ctlr);
	Setpin(ctlr->scl, 0);
	return 0;
}

static int
Restart(Ctlr *ctlr)
{
	Setpin(ctlr->sda, 1);
	Delay(ctlr);
	Setpin(ctlr->scl, 1);
	if(Stretch(ctlr) < 0)
		return -1;
	Delay(ctlr);
	return Start(ctlr);
}

static int
Stop(Ctlr *ctlr)
{
	Setpin(ctlr->sda, 0);
	Delay(ctlr);
	Setpin(ctlr->scl, 1);
	if(Stretch(ctlr) < 0){
		Setpin(ctlr->sda, 1);
		return -1;
	}
	Delay(ctlr);
	Setpin(ctlr->sda, 1);
	Delay(ctlr);
	return 0;
}

static int
init(I2Cbus *bus)
{
	Ctlr *ctlr = bus->ctlr;

	ctlr->delay = 1 + (1000000 / (2 * bus->speed));
	Setpin(ctlr->sda, 1);
	Setpin(ctlr->scl, 1);
	gpiopullup(ctlr->sda);
	gpiopullup(ctlr->scl);
	return 0;
}

static int
io(I2Cdev *dev, uchar *pkt, int olen, int ilen)
{
	I2Cbus *bus = dev->bus;
	Ctlr *ctlr = bus->ctlr;
	int i, o, v, alen;

	alen = olen > 0;
	if(olen > alen && (pkt[0] & 0xF8) == 0xF0)
		alen++;

	if((v = Start(ctlr)) < 0)
		return -1;
	if(olen > alen)
		pkt[0] &= ~1;
	for(o=0; o<olen; o++){
		if((v = Writebyte(ctlr, pkt[o])) != 0)
			goto Stop;
	}
	if(ilen <= 0 || olen <= 0)
		goto Stop;
	if((pkt[0]&1) == 0){
		if((v = Restart(ctlr)) < 0)
			goto Stop;
		pkt[0] |= 1;
		for(i=0; i<alen; i++){
			if((v = Writebyte(ctlr, pkt[i])) != 0)
				goto Stop;
		}
	}
	for(i=1; i<=ilen; i++){
		if((v = Readbyte(ctlr, i==ilen)) < 0)
			goto Stop;
		pkt[o++] = v;
	}
Stop:
	if(v == -2)	/* arbitration lost */
		return -1;
	Stop(ctlr);
	return o;
}

void
i2cgpiolink(void)
{
	static Ctlr ctlr1 = {
		.sda = 2,
		.scl = 3,
	};
	static I2Cbus i2c1 = {"i2c1", 100*1000, &ctlr1, init, io};
	addi2cbus(&i2c1);
}
