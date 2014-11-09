/*
 * embedded controller (usually at ports 0x66/0x62)
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"

enum {
	/* registers */
	EC_SC	= 0,
	EC_DATA,

	/* Embedded Controller Status, EC_SC (R) */
	OBF	= 1<<0,
	IBF	= 1<<1,
	CMD	= 1<<3,
	BURST	= 1<<4,
	SCI_EVT	= 1<<5,
	SMI_EVT	= 1<<6,

	/* Embedded Controller Command Set */
	RD_EC	= 0x80,
	WR_EC	= 0x81,
	BE_EC	= 0x82,
	BD_EC	= 0x83,
	QR_EC	= 0x84,
};

static struct {
	Lock;
	int	init;
	int	port[2];	/* EC_SC and EC_DATA */
} ec;

static uchar
ecrr(int reg)
{
	return inb(ec.port[reg]);
}
static void
ecwr(int reg, uchar val)
{
	outb(ec.port[reg], val);
}

static int
ecwait(uchar mask, uchar val)
{
	int i, s;

	s = 0;
	for(i=0; i<1000; i++){
		s = ecrr(EC_SC);
		if((s & mask) == val)
			return 0;
		delay(1);
	}
	print("ec: wait timeout status=%x pc=%#p\n", s, getcallerpc(&mask));
	return -1;
}

int
ecinit(int cmdport, int dataport)
{
	print("ec: cmd %X, data %X\n", cmdport, dataport);

	if(ioalloc(cmdport, 1, 0, "ec.sc") < 0){
		print("ec: cant allocate cmd port %X\n", cmdport);
		return -1;
	}
	if(ioalloc(dataport, 1, 0, "ec.data") < 0){
		print("ec: cant allocate data port %X\n", dataport);
		iofree(cmdport);
		return -1;
	}

	lock(&ec);
	ec.port[EC_SC] = cmdport;
	ec.port[EC_DATA] = dataport;
	ec.init = 1;
	unlock(&ec);

	return 0;
}

int
ecread(uchar addr)
{
	int r;

	r = -1;
	lock(&ec);
	if(!ec.init)
		goto out;
	if(ecwait(IBF, 0))
		goto out;
	ecwr(EC_SC, RD_EC);
	if(ecwait(IBF, 0))
		goto out;
	ecwr(EC_DATA, addr);
	if(ecwait(OBF, OBF))
		goto out;
	r = ecrr(EC_DATA);
	ecwait(OBF, 0);
out:
	unlock(&ec);
	return r;
}

int
ecwrite(uchar addr, uchar val)
{
	int r;

	r = -1;
	lock(&ec);
	if(!ec.init)
		goto out;
	if(ecwait(IBF, 0))
		goto out;
	ecwr(EC_SC, WR_EC);
	if(ecwait(IBF, 0))
		goto out;
	ecwr(EC_DATA, addr);
	if(ecwait(IBF, 0))
		goto out;
	ecwr(EC_DATA, val);
	if(ecwait(IBF, 0))
		goto out;
	r = 0;
out:
	unlock(&ec);
	return r;
}
