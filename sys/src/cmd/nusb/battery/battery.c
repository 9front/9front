/*
 * USB Human Interaction Device: battery.
 */

#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include "usb.h"
#include "hid.h"

typedef struct HID HID;
struct HID
{
	Dev*	dev;		/* usb device*/
	Dev*	ep;		/* endpoint to get events */

	/* report descriptor */
	int	nrep;
	uchar	rep[512];
};

typedef struct Battery Battery;
struct Battery
{
	QLock;

	uchar	missing;
	uchar	critical;
	uchar	charging;
	uchar	discharging;

	char	*capunit;

	int	remcapacity;
	int	fullcapacity;
	int	designcapacity;
	int	warncapacity;

	int	voltage;
	int	designvoltage;

	int	remruntime;
};

static int debug;
static char *user;
static File *batfile;
static Battery battery = {
	.capunit = "%",
	.fullcapacity = 100,
};
static char buf[512];

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
setproto(HID *f, Iface *iface)
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
hidfree(HID *h)
{
	if(h->ep != nil)
		closedev(h->ep);
	if(h->dev != nil)
		closedev(h->dev);
	free(h);
}

static void
hidfatal(HID*, char *sts)
{
	if(sts != nil)
		fprint(2, "%s: fatal: %s\n", argv0, sts);
	else
		fprint(2, "%s: exiting\n", argv0);
	threadexitsall(sts);
}

typedef struct Parse Parse;
struct Parse
{
	int	o;
	uchar	*e;
	uchar	p[128];
};

static void
itemparse(int t, int f, int g[], int l[], int, void *a)
{
	Parse *p = a;
	int v;

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
		case 0x00850044:
			battery.charging = v != 0;
			battery.discharging = !battery.charging;
			break;
		case 0x00850045:
			battery.discharging = v != 0;
			battery.charging = !battery.discharging;
			break;
		case 0x00850066:
			battery.remcapacity = v;
			break;
		case 0x00850067:
			battery.fullcapacity = v;
			break;
		case 0x00850068:
			battery.remruntime = v;
			break;
		case 0x00850083:
			battery.designcapacity = v;
			break;
		case 0x0085008c:
			battery.warncapacity = v;
			break;
		case 0x008500d1:
			battery.missing = (v == 0);
			break;
		case 0x00840030:
			battery.voltage = v;
			break;
		case 0x00840040:
			battery.designvoltage = v;
			break;
		case 0x00840065:	/* OverLoad */
		case 0x00840066:	/* Overcharged */
		case 0x00840067:	/* Overtemp */
		case 0x0085004b:	/* NeedReplacement */
			break;
		case 0x00850042:	/* BelowRemCapLimit */
		case 0x00850043:	/* RemTimeLimitExpired */
		case 0x00840069:	/* ShutdownImminent */
			battery.critical |= (v != 0);
			break;	
		}
	}
	p->o += g[RepSize];
}

static void
hidwork(void *a)
{
	char	err[ERRMAX];
	int	c, nerrs;
	HID*	f = a;
	Parse	p;

	threadsetname("battery %s", f->ep->dir);

	memset(&p, 0, sizeof(p));

	nerrs = 0;
	for(;;){
		if(f->ep == nil)
			hidfatal(f, nil);
		if(f->ep->maxpkt < 1 || f->ep->maxpkt > sizeof(p.p))
			hidfatal(f, "weird hid maxpkt");

		memset(p.p, 0, sizeof(p.p));
		c = read(f->ep->dfd, p.p, f->ep->maxpkt);
		if(c <= 0){
			if(c < 0)
				rerrstr(err, sizeof(err));
			else
				strcpy(err, "zero read");
			if(++nerrs < 3){
				fprint(2, "%s: hid: %s: read: %s\n", argv0, f->ep->dir, err);
				continue;
			}
			hidfatal(f, err);
		}
		nerrs = 0;

		qlock(&battery);

		battery.missing = 0;
		battery.critical = 0;
		battery.charging = 0;
		battery.discharging = 0;

		p.o = 0;
		p.e = p.p + c;
		repparse(f->rep, f->rep+f->nrep, itemparse, &p);

		if(battery.fullcapacity == 0)
			battery.fullcapacity = 100;
		if(battery.designcapacity == 0)
			battery.designcapacity = battery.fullcapacity;

		qunlock(&battery);
	}
}

static void
hidstart(Dev *d, Ep *ep, void (*f)(void*))
{
	HID *kd;

	kd = emallocz(sizeof(HID), 1);
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
	if(kd->nrep == 0) 
		hidfatal(kd, "no report");
	proccreate(f, kd, Stack);
	return;
Err:
	hidfree(kd);
}

static void
fsread(Req *r)
{
	int hh, mm, ss;
	char *state;

	if(r->fid->file != batfile){
		respond(r, "bug");
		return;
	}

	qlock(&battery);

	state = "unknown";
	if(battery.missing)
		state = "missing";
	else if(battery.charging)
		state = "charging";
	else if(battery.critical)
		state = "critical";
	else if(battery.discharging)
		state = "discharging";

	ss = battery.remruntime;
	hh = ss / 3600;
	ss -= 3600 * (ss / 3600);
	mm = ss / 60;
	ss -= 60 * (ss / 60);

	snprint(buf, sizeof(buf), "%d %s %d %d %d %d %d mV %d %d %02d:%02d:%02d %s\n",
		(battery.remcapacity * 100) / battery.fullcapacity,
		battery.capunit, battery.remcapacity, battery.fullcapacity, battery.designcapacity,
		battery.warncapacity, battery.warncapacity,
		battery.voltage*1000, battery.designvoltage*1000,
		hh, mm, ss,
		state
	);

	qunlock(&battery);

	readstr(r, buf);
	respond(r, nil);
}

static void
usage(void)
{
	fprint(2, "usage: %s [-d] devid\n", argv0);
	threadexits("usage");
}	

void
threadmain(int argc, char* argv[])
{
	static Srv fs = {
		.read = fsread,
	};

	int i;
	Dev *d;
	Ep *ep;
	Usbdev *ud;

	ARGBEGIN{
	case 'd':
		debug++;
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
	for(i = 0; i < nelem(ud->ep); i++){
		if((ep = ud->ep[i]) == nil)
			continue;
		if(ep->type != Eintr || (ep->dir == Eout))
			continue;
		hidstart(d, ep, hidwork);
	}
	user = getuser();
	fs.tree = alloctree(user, "usb", DMDIR|0555, nil);
	batfile = createfile(fs.tree->root, "battery", user, 0444, nil);
	snprint(buf, sizeof buf, "%d.battery", d->id);
	threadpostsharesrv(&fs, nil, "usb", buf);
	threadexits(nil);
}
