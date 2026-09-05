#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
#include "usb.h"
#include "serial.h"

enum {
	SetCommReq = 0x02,
	SetLineReq = 0x20,
	BreakReq = 0x23,

	BreakOn = 0xffff,
	BreakOff = 0x0000,
	
	ParamReqSz = 7,

	Fncm = 0x1, /* call management */
	Fnacm = 0x2, /* abstract control management */
};

static Serialops acmops;
static int did;
static int acmsetparam(Serialport *);
static int acmmultiplex(Serial *);
static int acmsetbreak(Serialport *, int);

int acmprobe(Serial *ser)
{
	int i, acmcap;
	Usbdev *ud;
	uchar *b;
	Desc *dd;

	acmcap = did = -1;
	ud = ser->dev->usb;
	for(i = 0; i < nelem(ud->ddesc); i++)
		if((dd = ud->ddesc[i]) != nil){
			b = (uchar *)&dd->data;
			if(b[1] == Dfunction && b[2] == Fncm){
				did = b[4];
			}
			else if(b[1] == Dfunction && b[2] == Fnacm)
				acmcap = b[3];
		}

	if(did < 0 || acmcap < 0)
		return -1;

	ser->Serialops = acmops;

	return 0;	
}

static int
acmsetparam(Serialport *p)
{
	uchar buf[ParamReqSz];
	int res;
	Serial *ser;

	ser = p->s;

	PUT4(buf, p->baud);

	buf[4] = p->stop;
	buf[5] = p->parity;
	buf[6] = p->bits;

	dsprint(2, "serial: setparam: ");
	res = usbcmd(ser->dev, Rh2d | Rclass | Riface, SetLineReq,
		0, 0, buf, sizeof buf);
	if(res != ParamReqSz){
		if(res >= 0) werrstr("acmsetparam failed with res=%d", res);
		return -1;
	}

	return 0;
}

static int
acminit(Serialport *p)
{
	p->baud = 9600;
	p->stop = 1;
	p->bits = 8;
	acmops.setparam(p);
	return 0;
}

static int
acmwait4data(Serialport *p, uchar *data, int count)
{
	int fd, n;

	fd = p->epin->dfd;
	qunlock(p->s);
	while ((n = read(fd, data, count)) == 0)
		;
	return n;
}

static int
acmfindeps(Serial *ser, int ifc)
{
	Ep **eps, *ep, *epin, *epout, *epintr;
	Usbdev *ud;
	Conf *c;
	int i;

	eps = nil;
	ud = ser->dev->usb;
	c = ud->conf[0];
 	for(i = 0; i < nelem(c->iface); i++)
		if(c->iface[i] != nil && c->iface[i]->id == did){
			eps = c->iface[i]->ep;
			break;
		}

	epintr = epin = epout = nil;
	for(i = 0; i < Nep; i++){
		if((ep = eps[i]) == nil)
			break;
		if(ser->hasepintr && ep->type == Eintr && ep->dir == Ein && epintr == nil)
			epintr = ep;
		if(ep->type == Ebulk){
			if((ep->dir == Ein || ep->dir == Eboth) && epin == nil)
				epin = ep;
			if((ep->dir == Eout || ep->dir == Eboth) && epout == nil)
				epout = ep;
		}
	}
	if(epin == nil || epout == nil)
		return -1;
	if(openeps(&ser->p[ifc], epin, epout, nil) < 0)
		return -1;

	return 0;
}

static Serialops acmops = {
	.init = acminit,
	.setparam = acmsetparam,
	.wait4data = acmwait4data,
	.findeps = acmfindeps,
};

