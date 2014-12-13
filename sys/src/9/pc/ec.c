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

static long
ecarchread(Chan*, void *a, long n, vlong off)
{
	int port, v;
	uchar *p;

	if(off < 0 || off >= 256)
		return 0;
	if(off+n > 256)
		n = 256 - off;
	p = a;
	for(port = off; port < off+n; port++){
		if((v = ecread(port)) < 0)
			error(Eio);
		*p++ = v;
	}
	return n;
}

static long
ecarchwrite(Chan*, void *a, long n, vlong off)
{
	int port;
	uchar *p;

	if(off < 0 || off+n > 256)
		error(Ebadarg);
	p = a;
	for(port = off; port < off+n; port++)
		if(ecwrite(port, *p++) < 0)
			error(Eio);
	return n;
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

	addarchfile("ec", 0660, ecarchread, ecarchwrite);

	return 0;
}
