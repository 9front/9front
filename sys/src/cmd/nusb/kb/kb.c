/*
 * USB Human Interaction Device: keyboard and mouse.
 *
 * If there's no usb keyboard, it tries to setup the mouse, if any.
 * It should be started at boot time.
 *
 * Mouse events are converted to the format of mouse(3)
 * on mousein file.
 * Keyboard keycodes are translated to scan codes and sent to kbdfs(8)
 * on kbin file.
 *
 */

#include <u.h>
#include <libc.h>
#include <thread.h>
#include "usb.h"
#include "hid.h"

enum
{
	Awakemsg=0xdeaddead,
	Diemsg = 0xbeefbeef,
};

enum
{
	Kbdelay = 500,
	Kbrepeat = 100,
};

typedef struct KDev KDev;
struct KDev
{
	Dev*	dev;		/* usb device*/
	Dev*	ep;		/* endpoint to get events */
	int	infd;		/* used to send events to kernel */
	Channel*repeatc;	/* only for keyboard */
};

/*
 * Map for the logitech bluetooth mouse with 8 buttons and wheels.
 *	{ ptr ->mouse}
 *	{ 0x01, 0x01 },	// left
 *	{ 0x04, 0x02 },	// middle
 *	{ 0x02, 0x04 },	// right
 *	{ 0x40, 0x08 },	// up
 *	{ 0x80, 0x10 },	// down
 *	{ 0x10, 0x08 },	// side up
 *	{ 0x08, 0x10 },	// side down
 *	{ 0x20, 0x02 }, // page
 * besides wheel and regular up/down report the 4th byte as 1/-1
 */

/*
 * key code to scan code; for the page table used by
 * the logitech bluetooth keyboard.
 */
static char sctab[256] = 
{
[0x00]	0x0,	0x0,	0x0,	0x0,	0x1e,	0x30,	0x2e,	0x20,
[0x08]	0x12,	0x21,	0x22,	0x23,	0x17,	0x24,	0x25,	0x26,
[0x10]	0x32,	0x31,	0x18,	0x19,	0x10,	0x13,	0x1f,	0x14,
[0x18]	0x16,	0x2f,	0x11,	0x2d,	0x15,	0x2c,	0x2,	0x3,
[0x20]	0x4,	0x5,	0x6,	0x7,	0x8,	0x9,	0xa,	0xb,
[0x28]	0x1c,	0x1,	0xe,	0xf,	0x39,	0xc,	0xd,	0x1a,
[0x30]	0x1b,	0x2b,	0x2b,	0x27,	0x28,	0x29,	0x33,	0x34,
[0x38]	0x35,	0x3a,	0x3b,	0x3c,	0x3d,	0x3e,	0x3f,	0x40,
[0x40]	0x41,	0x42,	0x43,	0x44,	0x57,	0x58,	0x63,	0x46,
[0x48]	0x77,	0x52,	0x47,	0x49,	0x53,	0x4f,	0x51,	0x4d,
[0x50]	0x4b,	0x50,	0x48,	0x45,	0x35,	0x37,	0x4a,	0x4e,
[0x58]	0x1c,	0x4f,	0x50,	0x51,	0x4b,	0x4c,	0x4d,	0x47,
[0x60]	0x48,	0x49,	0x52,	0x53,	0x56,	0x7f,	0x74,	0x75,
[0x68]	0x55,	0x59,	0x5a,	0x5b,	0x5c,	0x5d,	0x5e,	0x5f,
[0x70]	0x78,	0x79,	0x7a,	0x7b,	0x0,	0x0,	0x0,	0x0,
[0x78]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x71,
[0x80]	0x73,	0x72,	0x0,	0x0,	0x0,	0x7c,	0x0,	0x0,
[0x88]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x90]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x98]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xa0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xa8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xb0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xb8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xc0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xc8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xd0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xd8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xe0]	0x1d,	0x2a,	0x38,	0x7d,	0x61,	0x36,	0x64,	0x7e,
[0xe8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x73,	0x72,	0x71,
[0xf0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xf8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
};

static int kbdebug;
static int accel;

static int
setbootproto(KDev* f, int eid)
{
	int r, id;

	r = Rh2d|Rclass|Riface;
	id = f->dev->usb->ep[eid]->iface->id;
	return usbcmd(f->dev, r, Setproto, Bootproto, id, nil, 0);
}

static int
setleds(KDev* f, int, uchar leds)
{
	return usbcmd(f->dev, Rh2d|Rclass|Riface, Setreport, Reportout, 0, &leds, 1);
}

/*
 * Try to recover from a babble error. A port reset is the only way out.
 * BUG: we should be careful not to reset a bundle with several devices.
 */
static void
recoverkb(KDev *f)
{
	int i;

	close(f->dev->dfd);		/* it's for usbd now */
	devctl(f->dev, "reset");
	for(i = 0; i < 10; i++){
		sleep(500);
		if(opendevdata(f->dev, ORDWR) >= 0){
			setbootproto(f, f->ep->id);
			break;
		}
		/* else usbd still working... */
	}
}

static void
kbfree(KDev *kd)
{
	if(kd->infd >= 0)
		close(kd->infd);
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
	if(kd->repeatc != nil)
		sendul(kd->repeatc, Diemsg);
	kbfree(kd);
	threadexits(sts);
}

static void
kbprocname(KDev *kd, char *name)
{
	char buf[128];
	snprint(buf, sizeof(buf), "%s %s", name, kd->ep->dir);
	threadsetname(buf);
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

static int
scale(int x)
{
	int sign = 1;

	if(x < 0){
		sign = -1;
		x = -x;
	}
	switch(x){
	case 0:
	case 1:
	case 2:
	case 3:
		break;
	case 4:
		x = 6 + (accel>>2);
		break;
	case 5:
		x = 9 + (accel>>1);
		break;
	default:
		x *= MaxAcc;
		break;
	}
	return sign*x;
}

static short
s16(void *p)
{
	uchar *b = p;
	return b[0] | b[1]<<8;
}

static void
ptrwork(void* a)
{
	static char maptab[] = {0x0, 0x1, 0x4, 0x5, 0x2, 0x3, 0x6, 0x7};
	int x, y, z, b, c, nerrs, skiplead;
	char	err[ERRMAX], buf[64];
	char	mbuf[80];
	KDev*	f = a;

	kbprocname(f, "ptrwork");
	sethipri();

	skiplead = -1;
	nerrs = 0;
	for(;;){
		if(f->ep == nil)
			kbfatal(f, nil);
		if(f->ep->maxpkt < 3 || f->ep->maxpkt > sizeof buf)
			kbfatal(f, "mouse: weird mouse maxpkt");
		memset(buf, 0, sizeof buf);
		c = read(f->ep->dfd, buf, f->ep->maxpkt);
		if(c <= 0){
			if(c < 0)
				rerrstr(err, sizeof(err));
			else
				strcpy(err, "zero read");
			if(++nerrs < 3){
				fprint(2, "%s: mouse: %s: read: %s\n", argv0, f->ep->dir, err);
				if(strstr(err, "babble") != 0)
					recoverkb(f);
				continue;
			}
			kbfatal(f, err);
		}
		nerrs = 0;
		if(c < 3)
			continue;

		if(c > 4){
			/*
			 * horrible horrible hack. some mouse send a
			 * constant 0x01 lead byte. stop the hack as
			 * soon as buf[0] changes.
			 */
			if(skiplead == -1)
				skiplead = buf[0] & 0xFF;
			if(skiplead == 0x01 && skiplead == buf[0])
				memmove(buf, buf+1, --c);
			else
				skiplead = 0;
		}

		z = 0;
		if(c == 6 && f->dev->usb->vid == 0x1a7c){
			/* Evoluent vertical mouse */
			x = s16(&buf[1]);
			y = s16(&buf[3]);
			z = buf[5];
		} else {
			x = buf[1];
			y = buf[2];
			if(c > 3)
				z = buf[3];
		}
		if(accel){
			x = scale(x);
			y = scale(y);
		}
		b = maptab[buf[0] & 0x7];
		if(z > 0)	/* up */
			b |= 0x08;
		if(z < 0)	/* down */
			b |= 0x10;
		if(kbdebug > 1)
			fprint(2, "%s: m%11d %11d %11d\n", argv0, x, y, b);
		seprint(mbuf, mbuf+sizeof(mbuf), "m%11d %11d %11d", x, y,b);
		if(write(f->infd, mbuf, strlen(mbuf)) < 0)
			kbfatal(f, "mousein i/o");
	}
}

static void
putscan(int fd, uchar esc, uchar sc)
{
	uchar s[2] = {SCesc1, 0};

	s[1] = sc;
	if(esc && sc != 0)
		write(fd, s, 2);
	else if(sc != 0)
		write(fd, s+1, 1);
}

static void
putmod(int fd, uchar mods, uchar omods, uchar mask, uchar esc, uchar sc)
{
	uchar s[4], *p;

	p = s;
	if((mods&mask) && !(omods&mask)){
		if(esc)
			*p++ = SCesc1;
		*p++ = sc;
	}
	if(!(mods&mask) && (omods&mask)){
		if(esc)
			*p++ = SCesc1;
		*p++ = Keyup|sc;
	}
	if(p > s)
		write(fd, s, p - s);
}

static void
sleepproc(void* a)
{
	Channel *c = a;
	int ms;

	threadsetname("sleepproc");
	while((ms = recvul(c)) > 0)
		sleep(ms);
	chanfree(c);
}

static void
repeatproc(void* arg)
{
	KDev *f = arg;
	Channel *repeatc, *sleepc;
	int kbdinfd;
	ulong l, t;
	uchar esc1, sc;
	Alt a[3];

	repeatc = f->repeatc;
	kbdinfd = f->infd;
	threadsetname("repeatproc");
	
	sleepc = chancreate(sizeof(ulong), 0);
	if(sleepc != nil)
		proccreate(sleepproc, sleepc, Stack);

	a[0].c = repeatc;
	a[0].v = &l;
	a[0].op = CHANRCV;
	a[1].c = sleepc;
	a[1].v = &t;
	a[1].op = sleepc!=nil ? CHANSND : CHANNOP;
	a[2].c = nil;
	a[2].v = nil;
	a[2].op = CHANEND;

	l = Awakemsg;
	while(l != Diemsg){
		if(l == Awakemsg){
			l = recvul(repeatc);
			continue;
		}
		esc1 = l >> 8;
		sc = l;
		t = Kbdelay;
		if(alt(a) == 1){
			t = Kbrepeat;
			while(alt(a) == 1)
				putscan(kbdinfd, esc1, sc);
		}
	}
	if(sleepc != nil)
		sendul(sleepc, 0);
	chanfree(repeatc);
	threadexits(nil);
}

static void
stoprepeat(KDev *f)
{
	sendul(f->repeatc, Awakemsg);
}

static void
startrepeat(KDev *f, uchar esc1, uchar sc)
{
	ulong c;

	if(esc1)
		c = SCesc1 << 8 | (sc & 0xff);
	else
		c = sc;
	sendul(f->repeatc, c);
}

#define hasesc1(sc)	(((sc) > 0x47) || ((sc) == 0x38))

/*
 * This routine diffs the state with the last known state
 * and invents the scan codes that would have been sent
 * by a non-usb keyboard in that case. This also requires supplying
 * the extra esc1 byte as well as keyup flags.
 * The aim is to allow future addition of other keycode pages
 * for other keyboards.
 */
static uchar
putkeys(KDev *f, uchar buf[], uchar obuf[], int n, uchar dk)
{
	int i, j;
	uchar uk;
	int fd;

	fd = f->infd;
	putmod(fd, buf[0], obuf[0], Mctrl, 0, SCctrl);
	putmod(fd, buf[0], obuf[0], (1<<Mlshift), 0, SClshift);
	putmod(fd, buf[0], obuf[0], (1<<Mrshift), 0, SCrshift);
	putmod(fd, buf[0], obuf[0], Mcompose, 0, SCcompose);
	putmod(fd, buf[0], obuf[0], Maltgr, 1, SCcompose);

	/* Report key downs */
	for(i = 2; i < n; i++){
		for(j = 2; j < n; j++)
			if(buf[i] == obuf[j])
			 	break;
		if(j == n && buf[i] != 0){
			dk = sctab[buf[i]];
			putscan(fd, hasesc1(dk), dk);
			startrepeat(f, hasesc1(dk), dk);
		}
	}

	/* Report key ups */
	uk = 0;
	for(i = 2; i < n; i++){
		for(j = 2; j < n; j++)
			if(obuf[i] == buf[j])
				break;
		if(j == n && obuf[i] != 0){
			uk = sctab[obuf[i]];
			putscan(fd, hasesc1(uk), uk|Keyup);
		}
	}
	if(uk && (dk == 0 || dk == uk)){
		stoprepeat(f);
		dk = 0;
	}
	return dk;
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
kbdwork(void *a)
{
	uchar dk, buf[64], lbuf[64];
	int c, i, nerrs;
	char err[128];
	KDev *f = a;

	kbprocname(f, "kbdwork");

	f->repeatc = chancreate(sizeof(ulong), 0);
	if(f->repeatc == nil)
		kbfatal(f, "chancreate failed");
	proccreate(repeatproc, f, Stack);

	setleds(f, f->ep->id, 0);
	sethipri();

	memset(lbuf, 0, sizeof lbuf);
	dk = nerrs = 0;
	for(;;){
		if(f->ep == nil)
			kbfatal(f, nil);
		if(f->ep->maxpkt < 3 || f->ep->maxpkt > sizeof buf)
			kbfatal(f, "kbd: weird maxpkt");
		memset(buf, 0, sizeof buf);
		c = read(f->ep->dfd, buf, f->ep->maxpkt);
		if(c <= 0){
			if(c < 0)
				rerrstr(err, sizeof(err));
			else
				strcpy(err, "zero read");
			if(++nerrs < 3){
				fprint(2, "%s: kbd: %s: read: %s\n", argv0, f->ep->dir, err);
				if(strstr(err, "babble") != 0)
					recoverkb(f);
				continue;
			}
			kbfatal(f, err);
		}
		nerrs = 0;
		if(c < 3)
			continue;
		if(kbdbusy(buf + 2, c - 2))
			continue;
		if(usbdebug > 2 || kbdebug > 1){
			fprint(2, "kbd mod %x: ", buf[0]);
			for(i = 2; i < c; i++)
				fprint(2, "kc %x ", buf[i]);
			fprint(2, "\n");
		}
		dk = putkeys(f, buf, lbuf, f->ep->maxpkt, dk);
		memmove(lbuf, buf, c);
	}
}

static void
kbstart(Dev *d, Ep *ep, char *infile, void (*f)(void*))
{
	KDev *kd;

	kd = emallocz(sizeof(KDev), 1);
	kd->infd = open(infile, OWRITE);
	if(kd->infd < 0){
		fprint(2, "%s: %s: open: %r\n", argv0, d->dir);
		goto Err;
	}
	incref(d);
	kd->dev = d;
	if(setbootproto(kd, ep->id) < 0){
		fprint(2, "%s: %s: bootproto: %r\n", argv0, d->dir);
		goto Err;
	}
	kd->ep = openep(kd->dev, ep->id);
	if(kd->ep == nil){
		fprint(2, "%s: %s: openep %d: %r\n", argv0, d->dir, ep->id);
		goto Err;
	}
	if(opendevdata(kd->ep, OREAD) < 0){
		fprint(2, "%s: %s: opendevdata: %r\n", argv0, kd->ep->dir);
		goto Err;
	}
	procrfork(f, kd, Stack, RFNOTEG);
	return;
Err:
	kbfree(kd);
}

static void
usage(void)
{
	fprint(2, "usage: %s [-d] [-a n] devid\n", argv0);
	threadexits("usage");
}

void
threadmain(int argc, char* argv[])
{
	int i;
	Dev *d;
	Ep *ep;
	Usbdev *ud;

	ARGBEGIN{
	case 'a':
		accel = strtol(EARGF(usage()), nil, 0);
		break;
	case 'd':
		kbdebug++;
		break;
	default:
		usage();
	}ARGEND;
	if(argc != 1)
		usage();
	d = getdev(atoi(*argv));
	if(d == nil)
		sysfatal("getdev: %r");
	ud = d->usb;
	for(i = 0; i < nelem(ud->ep); i++){
		if((ep = ud->ep[i]) == nil)
			break;
		if(ep->type == Eintr && ep->dir == Ein && ep->iface->csp == KbdCSP)
			kbstart(d, ep, "/dev/kbin", kbdwork);
		if(ep->type == Eintr && ep->dir == Ein && ep->iface->csp == PtrCSP)
			kbstart(d, ep, "/dev/mousein", ptrwork);
	}
	threadexits(nil);
}
