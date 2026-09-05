#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
#include "usb.h"
#include "serial.h"

enum {
	Enable		= 0x00,

	Getbaud		= 0x1D,
	Setbaud		= 0x1E,
	Setflow		= 0x13,
		Flowdtron = 0x01,
		Flowctshs = 0x08,
		Flowrtson = 0x40,
		Flowrtshs = 0x80,
	Setlcr		= 0x03,
	Getlcr		= 0x04,
		Bitsmask	= 0x0F00,
		Bitsshift	= 8,
		Parmask		= 0x00F0,
		Parshift	= 4,
		Stopmask	= 0x000F,
		Stop1		= 0x0000,
		Stop1_5		= 0x0001,
		Stop2		= 0x0002,
	Setctrl		= 0x07,
		Dtron		= 0x0001,
		Rtson		= 0x0002,
};

static Cinfo slinfo[] = {
	{ 0x10c4, 0xea60, },		/* CP210x */
	{ 0x10c4, 0xea61, },		/* CP210x */
	{ 0,	0, },
};

static Serialops slops;

slprobe(Serial *ser)
{
	Usbdev *ud = ser->dev->usb;

	if(matchid(slinfo, ud->vid, ud->did) == nil)
		return -1;
	ser->Serialops = slops;
	return 0;
}

static int
slwrite(Serialport *p, int req, void *buf, int len)
{
	Serial *ser;

	ser = p->s;
	return usbcmd(ser->dev, Rh2d | Rvendor | Riface, req, 0, p->interfc,
		buf, len);
}

static int
slput(Serialport *p, uint op, uint val)
{
	Serial *ser;

	ser = p->s;
	return usbcmd(ser->dev, Rh2d | Rvendor | Riface, op, val, p->interfc,
		nil, 0);
}

static int
slread(Serialport *p, int req, void *buf, int len)
{
	Serial *ser;

	ser = p->s;
	return usbcmd(ser->dev, Rd2h | Rvendor | Riface, req, 0, p->interfc,
		buf, len);
}

static int
slgettype(Serialport *p)
{
	Serial *ser;
	uchar buf;
	int res;

	ser = p->s;
	res = usbcmd(ser->dev, Rd2h | Rvendor | Riface, 0xff, 0x370B, p->interfc,
		&buf, 1);

	if(res < 0)
		return 0xff;

	return buf;
}

static int
slinit(Serialport *p)
{
	Serial *ser;

	ser = p->s;
	dsprint(2, "slinit\n");

	strncpy(ser->driver, "silabs", sizeof(ser->driver));
	ser->type = slgettype(p);

	slput(p, Enable, 1);

	slops.getparam(p);

	/* p gets freed by closedev, the process has a reference */
	incref(ser->dev);
	return 0;
}

static int
slgetparam(Serialport *p)
{
	u16int lcr;

	slread(p, Getbaud, &p->baud, sizeof(p->baud));
	slread(p, Getlcr, &lcr, sizeof(lcr));
	p->bits = (lcr&Bitsmask)>>Bitsshift;
	p->parity = (lcr&Parmask)>>Parshift;
	p->stop = (lcr&Stopmask) == Stop1? 1 : 2;
	return 0;
}

static int
slsetparam(Serialport *p)
{
	u16int lcr;

	lcr = p->stop == 1? Stop1 : Stop2;
	lcr |= (p->bits<<Bitsshift) | (p->parity<<Parshift);
	slput(p, Setlcr, lcr);
	slwrite(p, Setbaud, &p->baud, sizeof(p->baud));
	return 0;
}

static int
slsendlines(Serialport *p)
{
	u16int ctrl;
	
	ctrl = (p->dtr? Dtron : 0) | Dtron << 8;
	ctrl |= (p->rts? Rtson : 0) | Rtson << 8;

	slput(p, Setctrl, ctrl);
	return 0;
}

static
slmodemctl(Serialport *p, int set)
{
	uchar flow[4*4];

	memset(flow, 0, sizeof flow);

	if(set){
		p->mctl = 1;
		PUT4(flow, (p->dtr?Flowdtron:0) | Flowctshs);
		PUT4(flow+4, Flowrtshs);
	}else{
		p->mctl = 0;
		PUT4(flow, (p->dtr)?Flowdtron:0);
		PUT4(flow+4, (p->rts)?Flowrtson:0);
	}
	slwrite(p, Setflow, flow, sizeof flow);
	return 0;
}

static int
wait4data(Serialport *p, uchar *data, int count)
{
	int fd, n;

	fd = p->epin->dfd;
	qunlock(p->s);
	while ((n = read(fd, data, count)) == 0)
		;
	return n;
}

static Serialops slops = {
	.init		= slinit,
	.getparam	= slgetparam,
	.setparam	= slsetparam,
	.sendlines	= slsendlines,
	.modemctl	= slmodemctl,
	.wait4data	= wait4data,
	.findeps	= findendpoints,
};
