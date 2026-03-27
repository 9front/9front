/*
 * USB Human Interaction Device: game controller.
 *
 * Game controller events are written to stdout.
 * May be used in conjunction with a script to
 * translate to keyboard events and pipe to emulator
 * (example /sys/src/games/nes/joynes).
 *
 */

#include <u.h>
#include <libc.h>
#include <thread.h>
#include "usb.h"
#include "hid.h"

typedef struct KDev KDev;
struct KDev
{
	Dev*	dev;		/* usb device*/
	Dev*	ep;		/* endpoint to get events */

	/* report descriptor */
	int	nrep;
	uchar	rep[512];
};

static void
kbfree(KDev *kd)
{
	if(kd->ep != nil)
		closedev(kd->ep);
	if(kd->dev != nil)
		closedev(kd->dev);
	free(kd);
}

static void
kbfatal(KDev *kd, char *sts)
{
	if(sts != nil)
		fprint(2, "%s: fatal: %s\n", argv0, sts);
	else
		fprint(2, "%s: exiting\n", argv0);
	kbfree(kd);
	threadexits(sts);
}

static int debug, kbd;
static double deadband;

static int
signext(int v, int bits)
{
	int s;

	s = sizeof(v)*8 - bits;
	v <<= s;
	v >>= s;
	return v;
}

static int
getbits(uchar *p, uchar *e, int bits, int off)
{
	int v, m;

	p += off/8;
	off %= 8;
	v = 0;
	m = 1;
	if(p < e){
		while(bits--){
			if(*p & (1<<off))
				v |= m;
			if(++off == 8){
				if(++p >= e)
					break;
				off = 0;
			}
			m <<= 1;
		}
	}
	return v;
}

enum {
	Ng	= RepCnt+1,
	UsgCnt	= Delim+1,	/* fake */
	Nl	= UsgCnt+1,
	Nu	= 256,
};

static uchar*
repparse1(uchar *d, uchar *e, int g[], int l[], int c,
	void (*f)(int t, int v, int g[], int l[], int c, void *a), void *a)
{
	int z, k, t, v, i;

	while(d < e){
		v = 0;
		t = *d++;
		z = t & 3, t >>= 2;
		k = t & 3, t >>= 2;
		switch(z){
		case 3:
			d += 4;
			if(d > e) continue;
			v = d[-4] | d[-3]<<8 | d[-2]<<16 | d[-1]<<24;
			break;
		case 2:
			d += 2;
			if(d > e) continue;
			v = d[-2] | d[-1]<<8;
			break;
		case 1:
			d++;
			if(d > e) continue;
			v = d[-1];
			break;
		}
		switch(k){
		case 0:	/* main item*/
			switch(t){
			case Collection:
				memset(l, 0, Nl*sizeof(l[0]));
				d = repparse1(d, e, g, l, v, f, a);
				continue;
			case CollectionEnd:
				return d;
			case Input:
			case Output:
			case Feature:
				if(l[UsgCnt] == 0 && l[UsagMin] != 0 && l[UsagMin] < l[UsagMax])
					for(i=l[UsagMin]; i<=l[UsagMax] && l[UsgCnt] < Nu; i++)
						l[Nl + l[UsgCnt]++] = i;
				for(i=0; i<g[RepCnt]; i++){
					if(i < l[UsgCnt])
						l[Usage] = l[Nl + i];
					(*f)(t, v, g, l, c, a);
				}
				break;
			}
			memset(l, 0, Nl*sizeof(l[0]));
			continue;
		case 1:	/* global item */
			if(t == Push){
				int w[Ng];
				memmove(w, g, sizeof(w));
				d = repparse1(d, e, w, l, c, f, a);
			} else if(t == Pop){
				return d;
			} else if(t < Ng){
				if(t == RepId)
					v &= 0xFF;
				else if(t == UsagPg)
					v &= 0xFFFF;
				else if(t != RepSize && t != RepCnt){
					v = signext(v, (z == 3) ? 32 : 8*z);
				}
				g[t] = v;
			}
			continue;
		case 2:	/* local item */
			if(l[Delim] != 0)
				continue;
			if(t == Delim){
				l[Delim] = 1;
			} else if(t < Delim){
				if(z != 3 && (t == Usage || t == UsagMin || t == UsagMax))
					v = (v & 0xFFFF) | (g[UsagPg] << 16);
				l[t] = v;
				if(t == Usage && l[UsgCnt] < Nu)
					l[Nl + l[UsgCnt]++] = v;
			}
			continue;
		case 3:	/* long item */
			if(t == 15)
				d += v & 0xFF;
			continue;
		}
	}
	return d;
}

/*
 * parse the report descriptor and call f for every (Input, Output
 * and Feature) main item as often as it would appear in the report
 * data packet.
 */
static void
repparse(uchar *d, uchar *e,
	void (*f)(int t, int v, int g[], int l[], int c, void *a), void *a)
{
	int l[Nl+Nu], g[Ng];

	memset(l, 0, sizeof(l));
	memset(g, 0, sizeof(g));
	repparse1(d, e, g, l, 0, f, a);
}

static int
setproto(KDev *f, Iface *iface)
{
	int proto;

	proto = Bootproto;
	f->nrep = usbcmd(f->dev, Rd2h|Rstd|Riface, Rgetdesc, Dreport<<8, iface->id, 
		f->rep, sizeof(f->rep));
	if(f->nrep > 0){
		if(debug){
			int i;

			fprint(2, "report descriptor:");
			for(i = 0; i < f->nrep; i++){
				if(i%8 == 0)
					fprint(2, "\n\t");
				fprint(2, "%#2.2ux ", f->rep[i]);
			}
			fprint(2, "\n");
		}
		proto = Reportproto;
	}

	/*
	 * if a HID's subclass code is 1 (boot mode), it will support
	 * setproto, otherwise it is not guaranteed to.
	 */
	if(Subclass(iface->csp) != 1)
		return 0;

	return usbcmd(f->dev, Rh2d|Rclass|Riface, Setproto, proto, iface->id, nil, 0);
}

static void
sethipri(void)
{
	char fn[64];
	int fd;

	snprint(fn, sizeof(fn), "/proc/%d/ctl", getpid());
	fd = open(fn, OWRITE);
	if(fd < 0)
		return;
	fprint(fd, "pri 13");
	close(fd);
}

typedef struct Joy Joy;
struct Joy
{
	int axes[Maxaxes];
	int oldaxes[Maxaxes];
	u64int btns;
	
	int	o;
	uchar	*e;
	uchar	p[128];
};

static void
joyparse(int t, int f, int g[], int l[], int, void *a)
{
	int v, i;
	Joy *p = a;
	u64int m;

	if(t != Input)
		return;
	if(g[RepId] != 0){
		if(p->p[0] != g[RepId]){
			p->o = 0;
			return;
		}
		if(p->o < 8)
			p->o = 8;	/* skip report id byte */
	}
	v = getbits(p->p, p->e, g[RepSize], p->o);
	if(g[LogiMin] < 0)
		v = signext(v, g[RepSize]);
	if((f & (Fvar|Farray)) == Fvar && v >= g[LogiMin] && v <= g[LogiMax]){
		switch(l[Usage]){
		case 0x010030:
		case 0x010031:
		case 0x010032:
		case 0x010033:
		case 0x010034:
		case 0x010035:
			i = l[Usage] - 0x010030;
			if((f & (Fabs|Frel)) == Fabs)
				p->axes[i] = (abs(v)<(g[LogiMax]*deadband))?0:v;
			else
				p->axes[i] += (abs(v)<(g[LogiMax]*deadband))?0:v;
			break;
		}
		if((l[Usage] >> 16) == 0x09){
			m = 1ULL << (l[Usage] & 0x3f);
			p->btns &= ~m;
			if(v != 0)
				p->btns |= m;
		}
	}
	p->o += g[RepSize];
}

static int
kbdbusy(uchar* buf, int n)
{
	int i;

	for(i = 1; i < n; i++)
		if(buf[i] == 0 || buf[i] != buf[0])
			return 0;
	return 1;
}

static void
joywork(void *a)
{
	char	err[ERRMAX];
	int	i, c, nerrs;
	KDev*	f = a;
	Joy	p;
	u64int lastb;

	threadsetname("joy %s", f->ep->dir);
	sethipri();

	memset(&p, 0, sizeof(p));
	lastb = 0;

	nerrs = 0;
	for(;;){
		if(f->ep == nil)
			kbfatal(f, nil);
		if(f->ep->maxpkt < 1 || f->ep->maxpkt > sizeof(p.p))
			kbfatal(f, "joy: weird mouse maxpkt");

		memset(p.p, 0, sizeof(p.p));
		c = read(f->ep->dfd, p.p, f->ep->maxpkt);
		if(c <= 0){
			if(c < 0)
				rerrstr(err, sizeof(err));
			else
				strcpy(err, "zero read");
			if(++nerrs < 3){
				fprint(2, "%s: joy: %s: read: %s\n", argv0, f->ep->dir, err);
				continue;
			}
			kbfatal(f, err);
		}
		nerrs = 0;

		p.o = 0;
		p.e = p.p + c;
		repparse(f->rep, f->rep+f->nrep, joyparse, &p);
		for(i = 0; i < Maxaxes; i++){
			if(p.axes[i] != p.oldaxes[i])
				print("axis %d %d\n", i, p.axes[i]);
			p.oldaxes[i] = p.axes[i];
		}
		for(i = 0; i < 64; i++)
			if(((lastb ^ p.btns) & (1ULL<<i)) != 0)
				print("%s %d\n", (p.btns & (1ULL<<i)) != 0 ? "down" : "up", i);
		lastb = p.btns;
	}
}

static void 
xbox360(KDev *kd)
{
	static uchar descr[] = {
		0x05, 0x01,
		0x09, 0x05,
		0xa1, 0x01,
		0x75, 0x08,	
		0x95, 0x01,	
		0x81, 0x01,
		0x75, 0x08,
		0x95, 0x01,
		0x05, 0x01,
		0x09, 0x3b,
		0x81, 0x01,
		0x05, 0x01,	
		0x09, 0x01,
		0xa1, 0x00,
		0x75, 0x01,
		0x15, 0x00,
		0x25, 0x01,
		0x35, 0x00,	
		0x45, 0x01,	
		0x95, 0x04,
		0x05, 0x09,
		0x09, 0x0c,
		0x09, 0x0d,
		0x09, 0x0e,
		0x09, 0x0f,
		0x81, 0x02,
		0xc0,
		0x75, 0x01,
		0x15, 0x00,
		0x25, 0x01,
		0x35, 0x00,
		0x45, 0x01,
		0x95, 0x07,
		0x05, 0x09,
		0x09, 0x08,
		0x09, 0x07,
		0x09, 0x09,
		0x09, 0x0a,
		0x09, 0x05,
		0x09, 0x06,
		0x09, 0x0b,
		0x81, 0x02,
		0x75, 0x01,
		0x95, 0x01,
		0x81, 0x01,
		0x75, 0x01,
		0x15, 0x00,
		0x25, 0x01,
		0x35, 0x00,
		0x45, 0x01,
		0x95, 0x04,
		0x05, 0x09,
		0x19, 0x01,
		0x29, 0x04,
		0x81, 0x02,	
		0x75, 0x08,
		0x15, 0x00,
		0x26, 0xff, 0x00,
		0x35, 0x00,
		0x46, 0xff, 0x00,
		0x95, 0x02,
		0x05, 0x01,
		0x09, 0x32,
		0x09, 0x35,
		0x81, 0x02,
		0x75, 0x10,
		0x16, 0x00, 0x80,
		0x26, 0xff, 0x7f,
		0x36, 0x00, 0x80,
		0x46, 0xff, 0x7f,
		0x95, 0x04,
		0x05, 0x01,
		0x09, 0x30,
		0x09, 0x31,
		0x09, 0x33,
		0x09, 0x34,
		0x81, 0x02,
		0x75, 0x30,
		0x95, 0x01,
		0x81, 0x01,
		0xc0,
	};
	static uchar ledcmd[] = {1,3,0};
	uchar b[20];
	Dev *d = kd->dev;

	if(cistrcmp("shanwan", d->usb->vendor) == 0){
		usbcmd(d, Rd2h|Rvendor|Riface, 0x01, 0x100, 0, b, 20);
		usbcmd(d, Rd2h|Rvendor|Riface, 0x01, 0, 0, b, 8);
		usbcmd(d, Rd2h|Rvendor|Rdev, 0x01, 0, 0, b, 4);
	}

	memcpy(kd->rep, descr, kd->nrep = sizeof(descr));
	/* no blinken lights */
	usbcmd(d, Rh2d|Rclass|Riface, Setreport, Reportout, 0, ledcmd, 3);
}


/* apply quirks for special devices */
static void
quirks(KDev *kd)
{
	int ret;
	Dev *d;
	uchar buf[17];

	d = kd->dev;

	/* sony dualshock 3 (ps3) controller requires special enable command */
	if(d->usb->vid == 0x054c && d->usb->did == 0x0268){
		ret = usbcmd(d, Rd2h|Rclass|Riface, Getreport, (0x3<<8) | 0xF2, 0, buf, sizeof(buf));
		if(ret < 0)
			sysfatal("failed to enable ps3 controller: %r");
	}

	/* XBox360 controller and compatible return no HID descriptor, so we provide one */
	if(d->usb->vid == 0x045e && d->usb->did == 0x028e
	|| d->usb->vid == 0x0f0d && d->usb->did == 0x00ed
	|| d->usb->vid == 0x1bad && d->usb->did == 0xf03a
	|| d->usb->vid == 0x2dc8 && d->usb->did == 0x310a){
		xbox360(kd);
	}
}

static void
kbstart(Dev *d, Ep *ep, void (*f)(void*))
{
	KDev *kd;

	kd = emallocz(sizeof(KDev), 1);
	incref(d);
	kd->dev = d;
	if(setproto(kd, ep->iface) < 0){
		fprint(2, "%s: %s: setproto: %r\n", argv0, d->dir);
		goto Err;
	}
	kd->ep = openep(kd->dev, ep);
	if(kd->ep == nil){
		fprint(2, "%s: %s: openep %d: %r\n", argv0, d->dir, ep->id);
		goto Err;
	}
	if(opendevdata(kd->ep, OREAD) < 0){
		fprint(2, "%s: %s: opendevdata: %r\n", argv0, kd->ep->dir);
		goto Err;
	}
	quirks(kd);
	if(kd->nrep == 0) 
		kbfatal(kd, "no report");

	f(kd);
	return;
Err:
	kbfree(kd);
}

static void
usage(void)
{
	fprint(2, "usage: %s [-d] [-b deadband] devid\n", argv0);
	threadexits("usage");
}

void
threadmain(int argc, char* argv[])
{
	int i;
	Dev *d;
	Ep *ep;
	Usbdev *ud;
	char *b;

	ARGBEGIN{
	case 'd':
		debug++;
		break;
	case 'b':
		b = EARGF(usage());
		deadband = atof(b);
		if(deadband > 0.0 && deadband < 1.0)
			break;
	default:
		usage();
	}ARGEND;
	if(argc != 1)
		usage();
	d = getdev(*argv);
	if(d == nil)
		sysfatal("getdev: %r");
	ud = d->usb;
	ep = nil;
	for(i = 0; i < nelem(ud->ep); i++){
		if((ep = ud->ep[i]) == nil)
			continue;
		if(ep->type != Eintr || (ep->dir == Eout))
			continue;
		if(ep->iface->csp == JoyCSP) 
			break;
		if(ep->iface->csp == Xbox360CSP)
			break;
	}
	if(ep == nil)
		sysfatal("no suitable endpoint found");
	kbstart(d, ep, joywork);
	threadexits(nil);
}
