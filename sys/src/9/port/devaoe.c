/*
 *	© 2005-13 coraid
 *	aoe storage initiator
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"
#include "../port/netif.h"
#include "../port/etherif.h"
#include "../ip/ip.h"
#include "../port/aoe.h"
#include <fis.h>

#pragma	varargck argpos	eventlog	1

#define dprint(...)	if(debug) eventlog(__VA_ARGS__); else USED(debug);
#define uprint(...)	snprint(up->genbuf, sizeof up->genbuf, __VA_ARGS__);

enum {
	Typebits		= 4,
	Unitbits		= 12,
	L3bits		= 4,
	Maxtype		= (1<<Typebits)-1,
	Maxunits	= (1<<Unitbits)-1,
	Maxl3		= (1<<L3bits)-1,
	Maxframes	= 128,
	Maxmtu		= 100000,
	Ndevlink	= 6,
	Nea		= 6,
	Nnetlink	= 6,
};

#define TYPE(q)		((ulong)(q).path & Maxtype)
#define UNIT(q)		(((ulong)(q).path>>Typebits) & Maxunits)
#define L(q)		(((ulong)(q).path>>Typebits+Unitbits) & Maxl3)
#define QID(u, t) 	((u)<<Typebits | (t))
#define Q3(l, u, t)	((l)<<Typebits+Unitbits | QID(u, t))
#define UP(d)		((d)->flag & Dup)

#define	Ticks		MACHP(0)->ticks
#define	Ms2tk(t)	(((t)*HZ)/1000)
#define	Tk2ms(t)	(((t)*1000)/HZ)

enum {
	Qzero,
	Qtopdir		= 1,
	Qtopbase,
	Qtopctl		= Qtopbase,
	Qtoplog,
	Qtopend,

	Qunitdir,
	Qunitbase,
	Qctl		= Qunitbase,
	Qdata,
	Qconfig,
	Qident,

	Qdevlinkdir,
	Qdevlinkbase,
	Qdevlink	= Qdevlinkbase,
	Qdevlinkend,

	Qtopfiles	= Qtopend-Qtopbase,
	Qdevlinkfiles	= Qdevlinkend-Qdevlinkbase,

	Eventlen 	= 256,
	Nevents 	= 64,

	Fread		= 0,
	Fwrite,
	Tfree		= -1,
	Tmgmt,

	/* round trip bounds, timeouts, in ticks */
	Rtmax		= Ms2tk(320),
	Rtmin		= Ms2tk(20),
	Srbtimeout	= 45*HZ,

	Dbcnt		= 1024,

	Crd		= 0x20,
	Crdext		= 0x24,
	Cwr		= 0x30,
	Cwrext		= 0x34,
	Cid		= 0xec,

	Alloc		= 0x01234567,
	Free		= 0x89abcdef,
};

enum {
	Read,
	Write,
};

/*
 * unified set of flags
 * a Netlink + Aoedev most both be jumbo capable
 * to send jumbograms to that interface.
 */
enum {
	Dup	= 1<<0,
	Djumbo	= 1<<1,
	Dnofail	= 1<<2,
};

static char *flagname[] = {
	"up",
	"jumbo",
	"nofail",
};

typedef struct {
	uchar	flag;
	uint	lostjumbo;

	Chan	*cc;
	Chan	*dc;
	Chan	*mtu;		/* open early to prevent bind issues. */
	char	path[Maxpath];
	uchar	ea[Eaddrlen];
} Netlink;

typedef struct {
	Netlink	*nl;
	int	nea;
	ulong	eaidx;
	uchar	eatab[Nea][Eaddrlen];
	int	datamtu;
	ulong	npkt;
	ulong	resent;
	uchar	flag;

	ulong	rttavg;
	ulong	mintimer;
} Devlink;

typedef struct Srb Srb;
struct Srb {
	Rendez;
	uint	state;
	Srb	*next;
	ulong	ticksent;
	ulong	len;
	vlong	sector;
	short	write;
	short	nout;
	char	*error;
	void	*dp;
	void	*data;
};

typedef struct {
	int	tag;
	ulong	bcnt;
	ulong	dlen;
	vlong	lba;
	ulong	ticksent;
	int	nhdr;
	uchar	hdr[ETHERMINTU];
	void	*dp;
	Devlink	*dl;
	Netlink	*nl;
	int	eaidx;
	Srb	*srb;
} Frame;

typedef struct Aoedev Aoedev;
struct Aoedev {
	QLock;
	Aoedev	*next;

	ulong	vers;

	int	ndl;
	ulong	dlidx;
	Devlink	*dl;
	Devlink	dltab[Ndevlink];

	uchar	flag;
	ushort	fwver;
	int	nopen;
	uint	major;
	uint	minor;
	int	unit;
	int	lasttag;
	int	nframes;
	Frame	*frames;
	vlong	bsize;
	vlong	realbsize;

	uint	maxbcnt;
	uint	maxmtu;
	ulong	lostjumbo;
	ushort	nout;
	ushort	maxout;
	ulong	lastwadj;
	Srb	*head;
	Srb	*tail;
	Srb	*inprocess;

	Sfis;
	char	serial[20+1];
	char	firmware[8+1];
	char	model[40+1];
	int	nconfig;
	uchar	config[1024];
	uchar	ident[512];
};

#pragma	varargck type	"æ"	Aoedev*

static struct {
	Lock;
	QLock;
	Rendez;
	char	buf[Eventlen*Nevents];
	char	*rp;
	char	*wp;
} events;

static struct {
	RWlock;
	int	nd;
	Aoedev	*d;
} devs;

static struct {
	Lock;
	int	reader[Nnetlink];	/* reader is running. */
	Rendez	rendez[Nnetlink];	/* confirm exit. */
	Netlink	nl[Nnetlink];
} netlinks;

extern	Dev 	aoedevtab;
static	Ref 	units;
static	Ref	drivevers;
static	int	debug;
static	int	autodiscover	= 1;
static	int	rediscover;
extern	char 	Enotup[] 	= "aoe device is down";

static Srb*
srballoc(ulong sz)
{
	Srb *srb;

	srb = smalloc(sizeof *srb+sz);
	srb->state = Alloc;
	srb->dp = srb->data = srb+1;
	srb->ticksent = Ticks;
	return srb;
}

static Srb*
srbkalloc(void *db, ulong)
{
	Srb *srb;

	srb = smalloc(sizeof *srb);
	srb->state = Alloc;
	srb->dp = srb->data = db;
	srb->ticksent = Ticks;
	return srb;
}

static int
srbready(void *v)
{
	Srb *s;

	s = v;
	return s->nout == 0 && (s->len == 0 || s->error != nil);
}

static void
srbfree(Srb *srb)
{
	int n;

	for(n = 0; srb->state != Free; n++)
		sched();
	free(srb);
}

/* under Aoedev qlock() so setting of srb->state is safe */
static void
srbwakeup(Srb *srb)
{
	if(srbready(srb)){
		assert(srb->state == Alloc);
		wakeup(srb);
		srb->state = Free;
	}
}

static void
srbcleanout(Aoedev *d, Srb *srb)
{
	Srb *x, **ll;

	if(srb == d->inprocess)
		d->inprocess = nil;
	else
		for(ll = &d->head; (x = *ll) != nil; ll = &x->next){
			d->tail = x;
			if(x == srb)
				*ll = x->next;
		}
}

static void
srberror(Aoedev *d, Srb *srb, char *s)
{
	srbcleanout(d, srb);
	srb->error = s;
	srbwakeup(srb);
}

static void
frameerror(Aoedev *d, Frame *f, char *s)
{
	Srb *srb;

	if(f->tag == Tfree)
		return;
	srb = f->srb;
	f->srb = nil;
	f->tag = Tfree;		/* don't get fooled by way-slow responses */
	if(srb == nil)
		return;
	srb->nout--;
	srberror(d, srb, s);
	d->nout--;
}

static char*
unitname(Aoedev *d)
{
	uprint("%ud.%ud", d->major, d->minor);
	return up->genbuf;
}

static long
eventlogread(void *a, long n)
{
	int len;
	char *p, *buf;

	buf = smalloc(Eventlen);
	qlock(&events);
	lock(&events);
	p = events.rp;
	len = *p;
	if(len == 0){
		n = 0;
		unlock(&events);
	} else {
		if(n > len)
			n = len;
		/* can't move directly into pageable space with events lock held */
		memmove(buf, p+1, n);
		*p = 0;
		events.rp = p += Eventlen;
		if(p >= events.buf + sizeof events.buf)
			events.rp = events.buf;
		unlock(&events);

		/* the concern here is page faults in memmove below */
		if(waserror()){
			qunlock(&events);
			free(buf);
			nexterror();
		}
		memmove(a, buf, n);
		poperror();
	}
	qunlock(&events);
	free(buf);
	return n;
}

static int
eventlog(char *fmt, ...)
{
	int dragrp, n;
	char *p;
	va_list arg;

	lock(&events);
	p = events.wp;
	dragrp = *p++;
	va_start(arg, fmt);
	n = vsnprint(p, Eventlen-1, fmt, arg);
	*--p = n;
	p = events.wp += Eventlen;
	if(p >= events.buf + sizeof events.buf)
		p = events.wp = events.buf;
	if(dragrp)
		events.rp = p;
	unlock(&events);
	wakeup(&events);
	return n;
}

static int
eventcount(void)
{
	uint n;

	lock(&events);
	if(*events.rp == 0)
		n = 0;
	else
		n = events.wp - events.rp & Nevents - 1;
	unlock(&events);
	return n/Eventlen;
}

static int
tsince(int tag)
{
	int n;

	n = Ticks & 0xffff;
	n -= tag & 0xffff;
	if(n < 0)
		n += 1<<16;
	return n;
}

static int
newtag(Aoedev *d)
{
	int t;

	do {
		t = ++d->lasttag << 16;
		t |= Ticks & 0xffff;
	} while (t == Tfree || t == Tmgmt);
	return t;
}

static void
downdev(Aoedev *d, char *err)
{
	Frame *f, *e;

	d->flag &= ~Dup;
	f = d->frames;
	e = f + d->nframes;
	for(; f < e; f++)
		frameerror(d, f, Enotup);
	d->inprocess = nil;
	eventlog("%æ: removed; %s\n", d, err);
}

static Block*
allocfb(Frame *f)
{
	int len;
	Block *b;

	len = f->nhdr + f->dlen;
	if(len < ETHERMINTU)
		len = ETHERMINTU;
	b = allocb(len);
	memmove(b->wp, f->hdr, f->nhdr);
	if(f->dlen)
		memmove(b->wp + f->nhdr, f->dp, f->dlen);
	b->wp += len;
	return b;
}

static void
putlba(Aoeata *a, vlong lba)
{
	uchar *c;

	c = a->lba;
	c[0] = lba;
	c[1] = lba >> 8;
	c[2] = lba >> 16;
	c[3] = lba >> 24;
	c[4] = lba >> 32;
	c[5] = lba >> 40;
}

static Devlink*
pickdevlink(Aoedev *d)
{
	ulong i, n;
	Devlink *l;

	for(i = 0; i < d->ndl; i++){
		n = d->dlidx++ % d->ndl;
		l = d->dl + n;
		if(l->flag & Dup)
			return l;
	}
	return nil;
}

static int
pickea(Devlink *l)
{
	if(l == nil)
		return -1;
	if(l->nea == 0)
		return -1;
	return l->eaidx++ % l->nea;
}

/*
 * would like this to depend on the chan (srb).
 * not possible in the current structure.
 */
#define Nofail(d, s)	(((d)->flag&Dnofail) == Dnofail)

static int
hset(Aoedev *d, Frame *f, Aoehdr *h, int cmd, int new)
{
	int i;
	Devlink *l;

	if(f->srb != nil)
	if((long)(Ticks-f->srb->ticksent) > Srbtimeout){
		eventlog("%æ: srb timeout\n", d);
		if(cmd == ACata && Nofail(d, s))
			f->srb->ticksent = Ticks;
		else
			frameerror(d, f, Etimedout);
		return -1;
	}
	l = pickdevlink(d);
	i = pickea(l);
	if(i == -1){
		if(!(cmd == ACata && f->srb != nil && Nofail(d, s)))
			downdev(d, "resend fails; no netlink/ea");
		return -1;
	}
	memmove(h->dst, l->eatab[i], Eaddrlen);
	memmove(h->src, l->nl->ea, sizeof h->src);
	hnputs(h->type, Aoetype);
	h->verflag = Aoever << 4;
	h->error = 0;
	hnputs(h->major, d->major);
	h->minor = d->minor;
	h->cmd = cmd;

	if(new)
		f->tag = newtag(d);
	hnputl(h->tag, f->tag);
	f->dl = l;
	f->nl = l->nl;
	f->eaidx = i;
	f->ticksent = Ticks;

	return f->tag;
}

static int
resend(Aoedev *d, Frame *f)
{
	ulong n;
	Aoeata *a;
	Aoehdr *h;

	h = (Aoehdr*)f->hdr;
	if(hset(d, f, h, h->cmd, 0) == -1)
		return -1;
	a = (Aoeata*)(f->hdr + Aoehsz);
	n = f->bcnt;
	if(n > d->maxbcnt){
		n = d->maxbcnt;		/* mtu mismatch (jumbo fail?) */
		if(f->dlen > n)
			f->dlen = n;
	}
	a->scnt = n / Aoesectsz;
	f->dl->resent++;
	f->dl->npkt++;
	if(waserror())
		/* should remove the netlink */
		return -1;
	devtab[f->nl->dc->type]->bwrite(f->nl->dc, allocfb(f), 0);
	poperror();
	return 0;
}

static void
discover(uint major, uint minor)
{
	Aoehdr *h;
	Block *b;
	Netlink *nl, *e;

	nl = netlinks.nl;
	e = nl + nelem(netlinks.nl);
	for(; nl < e; nl++){
		if(nl->cc == nil)
			continue;
		b = allocb(ETHERMINTU);
		if(waserror()){
			freeb(b);
			nexterror();
		}
		b->wp = b->rp + ETHERMINTU;
		memset(b->rp, 0, ETHERMINTU);
		h = (Aoehdr*)b->rp;
		memset(h->dst, 0xff, sizeof h->dst);
		memmove(h->src, nl->ea, sizeof h->src);
		hnputs(h->type, Aoetype);
		h->verflag = Aoever << 4;
		hnputs(h->major, major);
		h->minor = minor;
		h->cmd = ACconfig;
		poperror();
		devtab[nl->dc->type]->bwrite(nl->dc, b, 0);
	}
}

/*
 * Check all frames on device and resend any frames that have been
 * outstanding for 200% of the device round trip time average.
 */
static void
aoesweepproc(void*)
{
	ulong i, tx, timeout, nbc;
	vlong starttick;
	enum { Nms = 100, Nbcms = 30*1000, };
	uchar *ea;
	Aoeata *a;
	Aoedev *d;
	Devlink *l;
	Frame *f, *e;

	nbc = Nbcms/Nms;
loop:
	if(nbc-- == 0){
		if(rediscover && !waserror()){
			discover(0xffff, 0xff);
			poperror();
		}
		nbc = Nbcms/Nms;
	}
	starttick = Ticks;
	rlock(&devs);
	for(d = devs.d; d != nil; d = d->next){
		if(!canqlock(d))
			continue;
		if(!UP(d)){
			qunlock(d);
			continue;
		}
		tx = 0;
		f = d->frames;
		e = f + d->nframes;
		for (; f < e; f++){
			if(f->tag == Tfree)
				continue;
			l = f->dl;
			timeout = l->rttavg << 1;
			i = tsince(f->tag);
			if(i < timeout)
				continue;
			if(d->nout == d->maxout){
				if(d->maxout > 1)
					d->maxout--;
				d->lastwadj = Ticks;
			}
			a = (Aoeata*)(f->hdr + Aoehsz);
			if(a->scnt > Dbcnt / Aoesectsz &&
			   ++f->nl->lostjumbo > (d->nframes << 1)){
				ea = f->dl->eatab[f->eaidx];
				eventlog("%æ: jumbo failure on %s:%E; %llud\n",
					d, f->nl->path, ea, f->lba);
				d->maxbcnt = Dbcnt;
				d->flag &= ~Djumbo;
			}
			resend(d, f);
			if(tx++ == 0){
				if((l->rttavg <<= 1) > Rtmax)
					l->rttavg = Rtmax;
				eventlog("%æ: rtt %ldms\n", d, Tk2ms(l->rttavg));
			}
		}
		if(d->nout == d->maxout && d->maxout < d->nframes &&
		   TK2MS(Ticks-d->lastwadj) > 10*1000){
			d->maxout++;
			d->lastwadj = Ticks;
		}
		qunlock(d);
	}
	runlock(&devs);
	i = Nms - TK2MS(Ticks - starttick);
	if(i > 0 && !waserror()){
		tsleep(&up->sleep, return0, 0, i);
		poperror();
	}
	goto loop;
}

static int
fmtæ(Fmt *f)
{
	char buf[16];
	Aoedev *d;

	d = va_arg(f->args, Aoedev*);
	snprint(buf, sizeof buf, "aoe%ud.%ud", d->major, d->minor);
	return fmtstrcpy(f, buf);
}

static void netbind(char *path);

static void
aoecfg(void)
{
	char *p, *f[32], buf[24], ifbuf[64];
	int n, i;

	if((p = getconf("aoeif")) == nil)
		return;
	strncpy(ifbuf, p, sizeof(ifbuf)-1);
	ifbuf[sizeof(ifbuf)-1] = 0;
	if((n = tokenize(ifbuf, f, nelem(f))) < 1)
		return;
	/* goo! */
	for(i = 0; i < n; i++){
		p = f[i];
		if(strncmp(p, "ether", 5) == 0)
			snprint(buf, sizeof buf, "#l%c/ether%c", p[5], p[5]);
		else if(strncmp(p, "#l", 2) == 0)
			snprint(buf, sizeof buf, "#l%c/ether%c", p[2], p[2]);
		else
			continue;
		if(!waserror()){
			netbind(buf);
			poperror();
		}
	}
}

static void
aoeinit(void)
{
	static int init;
	static QLock l;

	if(!canqlock(&l))
		return;
	if(init == 0){
		fmtinstall(L'æ', fmtæ);
		events.rp = events.wp = events.buf;
		kproc("aoesweep", aoesweepproc, nil);
		aoecfg();
		init = 1;
	}
	qunlock(&l);
}

static Chan*
aoeattach(char *spec)
{
	Chan *c;

	if(*spec)
		error(Enonexist);
	aoeinit();
	c = devattach(L'æ', spec);
	mkqid(&c->qid, Qzero, 0, QTDIR);
	return c;
}

static int
unitseq(Chan *c, uint unit, Dir *dp)
{
	int i, rv;
	Qid q;
	Aoedev *d;

	i = 0;
	rv = -1;
	rlock(&devs);
	for(d = devs.d; d != nil; d = d->next)
		if(i++ == unit){
			mkqid(&q, QID(d->unit, Qunitdir), 0, QTDIR);
			devdir(c, q, unitname(d), 0, eve, 0555, dp);
			rv = 1;
			break;
		}
	runlock(&devs);
	return rv;
}

static Aoedev*
unit2dev(ulong unit)
{
	Aoedev *d;

	rlock(&devs);
	for(d = devs.d; d != nil; d = d->next)
		if(d->unit == unit){
			runlock(&devs);
			return d;
		}
	runlock(&devs);
	error("unit lookup failure");
	return nil;
}

static int
unitgen(Chan *c, ulong type, Dir *dp)
{
	int perm, t;
	ulong vers;
	vlong size;
	char *p;
	Aoedev *d;
	Qid q;

	d = unit2dev(UNIT(c->qid));
	perm = 0664;
	size = 0;
	vers = d->vers;
	t = QTFILE;

	switch(type){
	default:
		return -1;
	case Qctl:
		p = "ctl";
		break;
	case Qdata:
		p = "data";
		perm = 0660;
		if(UP(d))
			size = d->bsize;
		break;
	case Qconfig:
		p = "config";
		if(UP(d))
			size = d->nconfig;
		break;
	case Qident:
		p = "ident";
		if(UP(d))
			size = sizeof d->ident;
		break;
	case Qdevlinkdir:
		p = "devlink";
		t = QTDIR;
		perm = 0555;
		break;
	}
	mkqid(&q, QID(UNIT(c->qid), type), vers, t);
	devdir(c, q, p, size, eve, perm, dp);
	return 1;
}

static int
topgen(Chan *c, ulong type, Dir *d)
{
	int perm;
	vlong size;
	char *p;
	Qid q;

	perm = 0444;
	size = 0;
	switch(type){
	default:
		return -1;
	case Qtopctl:
		p = "ctl";
		perm = 0664;
		break;
	case Qtoplog:
		p = "log";
		size = eventcount();
		break;
	}
	mkqid(&q, type, 0, QTFILE);
	devdir(c, q, p, size, eve, perm, d);
	return 1;
}

static int
aoegen(Chan *c, char *, Dirtab *, int, int s, Dir *dp)
{
	int i;
	Aoedev *d;
	Qid q;

	if(c->qid.path == 0){
		switch(s){
		case DEVDOTDOT:
			q.path = 0;
			q.type = QTDIR;
			devdir(c, q, "#æ", 0, eve, 0555, dp);
			break;
		case 0:
			q.path = Qtopdir;
			q.type = QTDIR;
			devdir(c, q, "aoe", 0, eve, 0555, dp);
			break;
		default:
			return -1;
		}
		return 1;
	}

	switch(TYPE(c->qid)){
	default:
		return -1;
	case Qtopdir:
		if(s == DEVDOTDOT){
			mkqid(&q, Qzero, 0, QTDIR);
			devdir(c, q, "aoe", 0, eve, 0555, dp);
			return 1;
		}
		if(s < Qtopfiles)
			return topgen(c, Qtopbase + s, dp);
		s -= Qtopfiles;
		return unitseq(c, s, dp);
	case Qtopctl:
	case Qtoplog:
		return topgen(c, TYPE(c->qid), dp);
	case Qunitdir:
		if(s == DEVDOTDOT){
			mkqid(&q, QID(0, Qtopdir), 0, QTDIR);
			uprint("%uld", UNIT(c->qid));
			devdir(c, q, up->genbuf, 0, eve, 0555, dp);
			return 1;
		}
		return unitgen(c, Qunitbase+s, dp);
	case Qctl:
	case Qdata:
	case Qconfig:
	case Qident:
		return unitgen(c, TYPE(c->qid), dp);
	case Qdevlinkdir:
		i = UNIT(c->qid);
		if(i >= Maxunits || i >= units.ref)
			return -1;
		d = unit2dev(i);
		if(s == DEVDOTDOT){
			mkqid(&q, QID(i, Qunitdir), 0, QTDIR);
			devdir(c, q, unitname(d), 0, eve, 0555, dp);
			return 1;
		}
		if(s >= d->ndl)
			return -1;
		uprint("%d", s);
		mkqid(&q, Q3(s, i, Qdevlink), 0, QTFILE);
		devdir(c, q, up->genbuf, 0, eve, 0444, dp);
		return 1;
	case Qdevlink:
		uprint("%d", s);
		mkqid(&q, Q3(s, UNIT(c->qid), Qdevlink), 0, QTFILE);
		devdir(c, q, up->genbuf, 0, eve, 0444, dp);
		return 1;
	}
}

static Walkqid*
aoewalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, nil, 0, aoegen);
}

static int
aoestat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, nil, 0, aoegen);
}

static Chan*
aoeopen(Chan *c, int omode)
{
	Aoedev *d;

	if(TYPE(c->qid) != Qdata)
		return devopen(c, omode, 0, 0, aoegen);

	d = unit2dev(UNIT(c->qid));
	qlock(d);
	if(waserror()){
		qunlock(d);
		nexterror();
	}
	if(!UP(d))
		error(Enotup);
	c = devopen(c, omode, 0, 0, aoegen);
	d->nopen++;
	poperror();
	qunlock(d);
	return c;
}

static void
aoeclose(Chan *c)
{
	Aoedev *d;

	if(TYPE(c->qid) != Qdata || (c->flag&COPEN) == 0)
		return;

	d = unit2dev(UNIT(c->qid));
	qlock(d);
	if(--d->nopen == 0 && !waserror()){
		discover(d->major, d->minor);
		poperror();
	}
	qunlock(d);
}

static void
atarw(Aoedev *d, Frame *f)
{
	ulong bcnt;
	char extbit, writebit;
	Aoeata *ah;
	Aoehdr *h;
	Srb *srb;

	extbit = 0x4;
	writebit = 0x10;

	srb = d->inprocess;
	bcnt = d->maxbcnt;
	if(bcnt > srb->len)
		bcnt = srb->len;
	f->nhdr = Aoehsz + Aoeatasz;
	memset(f->hdr, 0, f->nhdr);
	h = (Aoehdr*)f->hdr;
	if(hset(d, f, h, ACata, 1) == -1){
		d->inprocess = nil;
		return;
	}
	ah = (Aoeata*)(f->hdr + Aoehsz);
	f->dp = srb->dp;
	f->bcnt = bcnt;
	f->lba = srb->sector;
	f->srb = srb;

	ah->scnt = bcnt / Aoesectsz;
	putlba(ah, f->lba);
	if(d->feat & Dllba)
		ah->aflag |= AAFext;
	else {
		extbit = 0;
		ah->lba[3] &= 0x0f;
		ah->lba[3] |= 0xe0;	/* LBA bit+obsolete 0xa0 */
	}
	if(srb->write){
		ah->aflag |= AAFwrite;
		f->dlen = bcnt;
	}else{
		writebit = 0;
		f->dlen = 0;
	}
	ah->cmdstat = 0x20 | writebit | extbit;

	/* mark tracking fields and load out */
	srb->nout++;
	srb->dp = (uchar*)srb->dp + bcnt;
	srb->len -= bcnt;
	srb->sector += bcnt / Aoesectsz;
	if(srb->len == 0)
		d->inprocess = nil;
	d->nout++;
	f->dl->npkt++;
	if(waserror())
		frameerror(d, f, "write error");
	else{
		devtab[f->nl->dc->type]->bwrite(f->nl->dc, allocfb(f), 0);
		poperror();
	}
}

static char*
aoeerror(Aoehdr *h)
{
	int n;
	static char *errs[] = {
		"aoe protocol error: unknown",
		"aoe protocol error: bad command code",
		"aoe protocol error: bad argument param",
		"aoe protocol error: device unavailable",
		"aoe protocol error: config string present",
		"aoe protocol error: unsupported version",
		"aoe protocol error: target is reserved",
	};

	if((h->verflag & AFerr) == 0)
		return nil;
	n = h->error;
	if(n >= nelem(errs))
		n = 0;
	return errs[n];
}

static void
rtupdate(Devlink *l, int rtt)
{
	int n;

	n = rtt;
	if(rtt < 0){
		n = -rtt;
		if(n < Rtmin)
			n = Rtmin;
		else if(n > Rtmax)
			n = Rtmax;
		l->mintimer += (n - l->mintimer) >> 1;
	} else if(n < l->mintimer)
		n = l->mintimer;
	else if(n > Rtmax)
		n = Rtmax;

	/* g == .25; cf. Congestion Avoidance and Control, Jacobson&Karels; 1988 */
	n -= l->rttavg;
	l->rttavg += n >> 2;
}

static Frame*
getframe(Aoedev *d, int tag)
{
	Frame *f, *e;

	f = d->frames;
	e = f + d->nframes;
	for(; f < e; f++)
		if(f->tag == tag)
			return f;
	return nil;
}

static Frame*
freeframe(Aoedev *d)
{
	if(d->nout < d->maxout)
		return getframe(d, Tfree);
	return nil;
}

static void
work(Aoedev *d)
{
	Frame *f;

	while(f = freeframe(d)) {
		if(d->inprocess == nil){
			if(d->head == nil)
				return;
			d->inprocess = d->head;
			d->head = d->head->next;
			if(d->head == nil)
				d->tail = nil;
		}
		atarw(d, f);
	}
}

static void
strategy(Aoedev *d, Srb *srb)
{
	qlock(d);
	if(waserror()){
		qunlock(d);
		nexterror();
	}
	if(!UP(d))
		error(Eio);
	srb->next = nil;
	if(d->tail != nil)
		d->tail->next = srb;
	d->tail = srb;
	if(d->head == nil)
		d->head = srb;
	work(d);
	poperror();
	qunlock(d);

	while(waserror()){
		qlock(d);
		srberror(d, srb, "interrupted");
		qunlock(d);
	}
	sleep(srb, srbready, srb);
	poperror();
}

#define iskaddr(a)	((uintptr)(a) > KZERO)

static long
rw(Aoedev *d, int write, uchar *db, long len, uvlong off)
{
	long n, nlen, copy;
	enum { Srbsz = 1<<19, };
	Srb *srb;

	if((off|len) & (Aoesectsz-1))
		error("offset and length must be sector multiple.\n");
	if(!UP(d))
		error(Eio);
	if(off >= d->bsize)
		return 0;
	if(off + len > d->bsize)
		len = d->bsize - off;
	copy = 0;
	if(iskaddr(db)){
		srb = srbkalloc(db, len);
		copy = 1;
	}else
		srb = srballoc(Srbsz <= len? Srbsz: len);
	if(waserror()){
		srbfree(srb);
		nexterror();
	}
	srb->write = write;
	for(nlen = len; nlen; nlen -= n){
		srb->sector = off / Aoesectsz;
		srb->dp = srb->data;
		n = nlen;
		if(n > Srbsz)
			n = Srbsz;
		srb->len = n;
		if(write && !copy)
			memmove(srb->data, db, n);
		strategy(d, srb);
		if(srb->error != nil)
			error(srb->error);
		if(!write && !copy)
			memmove(db, srb->data, n);
		db += n;
		off += n;
	}
	poperror();
	srbfree(srb);
	return len;
}

static long
readmem(ulong off, void *dst, long n, void *src, long size)
{
	if(off >= size)
		return 0;
	if(off + n > size)
		n = size - off;
	memmove(dst, (uchar*)src + off, n);
	return n;
}

static char*
aoeflag(char *s, char *e, uchar f)
{
	uchar i;

	for(i = 0; i < nelem(flagname); i++)
		if(f & 1 << i)
			s = seprint(s, e, "%s ", flagname[i]);
	return seprint(s, e, "\n");
}

static int
pstat(Aoedev *d, char *db, int len, int off)
{
	int i;
	char *state, *s, *p, *e;

	s = p = smalloc(READSTR);
	e = p + READSTR;

	state = "down";
	if(UP(d))
		state = "up";

	p = seprint(p, e,
		"state: %s\n"	"nopen: %d\n"	"nout: %d\n"
		"nmaxout: %d\n"	"nframes: %d\n"	"maxbcnt: %d [maxmtu %d]\n"
		"fw: %.4ux\n"
		"model: %s\n"	"serial: %s\n"	"firmware: %s\n",
		state,		d->nopen,	d->nout,
		d->maxout, 	d->nframes,	d->maxbcnt, d->maxmtu,
		d->fwver,
		d->model, 	d->serial, 	d->firmware);
	p = seprint(p, e, "flag: ");
	p = pflag(p, e, d);
	p[-1] = ' ';	/* horrid */
	p = aoeflag(p, e, d->flag);

	if(p - s < len)
		len = p - s;
	i = readstr(off, db, len, s);
	free(s);
	return i;
}

static long
unitread(Chan *c, void *db, long len, vlong off)
{
	Aoedev *d;

	d = unit2dev(UNIT(c->qid));
	if(d->vers != c->qid.vers)
		error(Echange);
	switch(TYPE(c->qid)){
	default:
		error(Ebadarg);
	case Qctl:
		return pstat(d, db, len, off);
	case Qdata:
		return rw(d, Read, db, len, off);
	case Qconfig:
		if(!UP(d))
			error(Enotup);
		return readmem(off, db, len, d->config, d->nconfig);
	case Qident:
		if(!UP(d))
			error(Enotup);
		return readmem(off, db, len, d->ident, sizeof d->ident);
	}
}

static int
getmtu(Chan *m)
{
	int n, mtu;
	char buf[36];

	mtu = 1514;
	if(m == nil || waserror())
		return mtu;
	n = devtab[m->type]->read(m, buf, sizeof buf - 1, 0);
	poperror();
	if(n > 12){
		buf[n] = 0;
		mtu = strtoul(buf + 12, 0, 0);
	}
	return mtu;
}

static int
devlinkread(Chan *c, void *db, int len, int off)
{
	int i;
	char *s, *p, *e;
	Aoedev *d;
	Devlink *l;

	d = unit2dev(UNIT(c->qid));
	i = L(c->qid);
	if(i >= d->ndl)
		return 0;
	l = d->dl + i;

	s = p = smalloc(READSTR);
	e = s + READSTR;

	p = seprint(p, e, "addr: ");
	for(i = 0; i < l->nea; i++)
		p = seprint(p, e, "%E ", l->eatab[i]);
	p = seprint(p, e, "\n");
	p = seprint(p, e, "npkt: %uld\n", l->npkt);
	p = seprint(p, e, "resent: %uld\n", l->resent);
	p = seprint(p, e, "flag: ");
	p = aoeflag(p, e, l->flag);
	p = seprint(p, e, "rttavg: %uld\n", Tk2ms(l->rttavg));
	p = seprint(p, e, "mintimer: %uld\n", Tk2ms(l->mintimer));
	p = seprint(p, e, "datamtu: %d\n", l->datamtu);

	p = seprint(p, e, "nl path: %s\n", l->nl->path);
	p = seprint(p, e, "nl ea: %E\n", l->nl->ea);
	p = seprint(p, e, "nl flag: ");
	p = aoeflag(p, e, l->flag);
	p = seprint(p, e, "nl lostjumbo: %d\n", l->nl->lostjumbo);
	p = seprint(p, e, "nl datamtu: %d\n", getmtu(l->nl->mtu));

	if(p - s < len)
		len = p - s;
	i = readstr(off, db, len, s);
	free(s);
	return i;
}

static long
topctlread(Chan *, void *db, int len, int off)
{
	int i;
	char *s, *p, *e;
	Netlink *n;

	s = p = smalloc(READSTR);
	e = s + READSTR;

	p = seprint(p, e, "debug: %d\n", debug);
	p = seprint(p, e, "autodiscover: %d\n", autodiscover);
	p = seprint(p, e, "rediscover: %d\n", rediscover);

	for(i = 0; i < Nnetlink; i++){
		n = netlinks.nl+i;
		if(n->cc == nil)
			continue;
		p = seprint(p, e, "if%d path: %s\n", i, n->path);
		p = seprint(p, e, "if%d ea: %E\n", i, n->ea);
		p = seprint(p, e, "if%d flag: ", i);
		p = aoeflag(p, e, n->flag);
		p = seprint(p, e, "if%d lostjumbo: %d\n", i, n->lostjumbo);
		p = seprint(p, e, "if%d datamtu: %d\n", i, getmtu(n->mtu));
	}

	if(p - s < len)
		len = p - s;
	i = readstr(off, db, len, s);
	free(s);
	return i;
}

static long
aoeread(Chan *c, void *db, long n, vlong off)
{
	switch(TYPE(c->qid)){
	default:
		error(Eperm);
	case Qzero:
	case Qtopdir:
	case Qunitdir:
	case Qdevlinkdir:
		return devdirread(c, db, n, 0, 0, aoegen);
	case Qtopctl:
		return topctlread(c, db, n, off);
	case Qtoplog:
		return eventlogread(db, n);
	case Qctl:
	case Qdata:
	case Qconfig:
	case Qident:
		return unitread(c, db, n, off);
	case Qdevlink:
		return devlinkread(c, db, n, off);
	}
}

static long
configwrite(Aoedev *d, void *db, long len)
{
	char *s;
	Aoehdr *h;
	Aoecfg *ch;
	Frame *f;
	Srb *srb;

	if(!UP(d))
		error(Enotup);
	if(len > sizeof d->config)
		error(Etoobig);
	srb = srballoc(len);
	s = smalloc(len);
	memmove(s, db, len);
	if(waserror()){
		srbfree(srb);
		free(s);
		nexterror();
	}
	for (;;) {
		qlock(d);
		if(waserror()){
			qunlock(d);
			nexterror();
		}
		f = freeframe(d);
		if(f != nil)
			break;
		poperror();
		qunlock(d);
		if(waserror())
			nexterror();
		tsleep(&up->sleep, return0, 0, 100);
		poperror();
	}
	f->nhdr = Aoehsz + Aoecfgsz;
	memset(f->hdr, 0, f->nhdr);
	h = (Aoehdr*)f->hdr;
	if(hset(d, f, h, ACconfig, 1) == -1)
		return 0;
	ch = (Aoecfg*)(f->hdr + Aoehsz);
	f->srb = srb;
	f->dp = s;
	ch->verccmd = AQCfset;
	hnputs(ch->cslen, len);
	d->nout++;
	srb->nout++;
	f->dl->npkt++;
	f->dlen = len;
	/*
	 * these refer to qlock & waserror in the above for loop.
	 * there's still the first waserror outstanding.
	 */
	poperror();
	qunlock(d);

	devtab[f->nl->dc->type]->bwrite(f->nl->dc, allocfb(f), 0);
	sleep(srb, srbready, srb);
	if(srb->error)
		error(srb->error);

	qlock(d);
	if(waserror()){
		qunlock(d);
		nexterror();
	}
	memmove(d->config, s, len);
	d->nconfig = len;
	poperror();
	qunlock(d);

	poperror();			/* pop first waserror */

	srbfree(srb);
	memmove(db, s, len);
	free(s);
	return len;
}

static int
devmaxdata(Aoedev *d)
{
	int i, m, mtu, datamtu;
	Devlink *l;
	Netlink *n;

	mtu = 100000;
	datamtu = 100000;
	for(i = 0; i < d->ndl; i++){
		l = d->dl + i;
		n = l->nl;
		if((l->flag & Dup) == 0 || (n->flag & Dup) == 0)
			continue;
		m = getmtu(n->mtu);
		if(l->datamtu < datamtu)
			datamtu = l->datamtu;
		if(m < mtu)
			mtu = m;
	}
	if(mtu == 100000)
		mtu = 1514;
	mtu -= Aoehsz + Aoeatasz;
	mtu -= mtu % Aoesectsz;
	if(mtu > datamtu)
		mtu = datamtu;
	return mtu;
}

static int
toggle(char *s, uint f, uint bit)
{
	if(s == nil)
		f = f^bit;
	else if(strcmp(s, "on") == 0)
		f |= bit;
	else
		f &= ~bit;
	return f;
}

static void ataident(Aoedev*);

static long
unitctlwrite(Aoedev *d, void *db, long n)
{
	uint maxbcnt, m;
	uvlong bsize;
	enum {
		Failio,
		Ident,
		Jumbo,
		Maxbno,
		Mtu,
		Nofailf,
		Setsize,
	};
	Cmdbuf *cb;
	Cmdtab *ct;
	static Cmdtab cmds[] = {
		{Failio, 	"failio", 	1 },
		{Ident, 	"identify", 	1 },
		{Jumbo, 	"jumbo", 	0 },
		{Maxbno,	"maxbno",	0 },
		{Mtu,		"mtu",		0 },
		{Nofailf,		"nofail",		0 },
		{Setsize, 	"setsize", 	0 },
	};

	cb = parsecmd(db, n);
	qlock(d);
	if(waserror()){
		qunlock(d);
		free(cb);
		nexterror();
	}
	ct = lookupcmd(cb, cmds, nelem(cmds));
	switch(ct->index){
	case Failio:
		downdev(d, "i/o failure");
		break;
	case Ident:
		ataident(d);
		break;
	case Jumbo:
		d->flag = toggle(cb->f[1], d->flag, Djumbo);
		break;
	case Maxbno:
	case Mtu:
		maxbcnt = devmaxdata(d);
		if(cb->nf > 2)
			error(Ecmdargs);
		if(cb->nf == 2){
			m = strtoul(cb->f[1], 0, 0);
			if(ct->index == Maxbno)
				m *= Aoesectsz;
			else{
				m -= Aoehsz + Aoeatasz;
				m &= ~(Aoesectsz-1);
			}
			if(m == 0 || m > maxbcnt)
				cmderror(cb, "invalid mtu");
			maxbcnt = m;
			d->maxmtu = m;
		} else
			d->maxmtu = Maxmtu;
		d->maxbcnt = maxbcnt;
		break;
	case Nofailf:
		d->flag = toggle(cb->f[1], d->flag, Dnofail);
		break;
	case Setsize:
		bsize = d->realbsize;
		if(cb->nf > 2)
			error(Ecmdargs);
		if(cb->nf == 2){
			bsize = strtoull(cb->f[1], 0, 0);
			if(bsize % Aoesectsz)
				cmderror(cb, "disk size must be sector aligned");
		}
		d->bsize = bsize;
		break;
	}
	poperror();
	qunlock(d);
	free(cb);
	return n;
}

static long
unitwrite(Chan *c, void *db, long n, vlong off)
{
	long rv;
	char *buf;
	Aoedev *d;

	d = unit2dev(UNIT(c->qid));
	switch(TYPE(c->qid)){
	default:
		error(Ebadarg);
	case Qctl:
		return unitctlwrite(d, db, n);
	case Qident:
		error(Eperm);
	case Qdata:
		return rw(d, Write, db, n, off);
	case Qconfig:
		if(off + n > sizeof d->config)
			error(Etoobig);
		buf = smalloc(sizeof d->config);
		if(waserror()){
			free(buf);
			nexterror();
		}
		memmove(buf, d->config, d->nconfig);
		memmove(buf + off, db, n);
		rv = configwrite(d, buf, n + off);
		poperror();
		free(buf);
		return rv;
	}
}

static Netlink*
addnet(char *path, Chan *cc, Chan *dc, Chan *mtu, uchar *ea)
{
	Netlink *nl, *e;

	lock(&netlinks);
	if(waserror()){
		unlock(&netlinks);
		nexterror();
	}
	nl = netlinks.nl;
	e = nl + nelem(netlinks.nl);
	for(; nl < e && nl->cc != nil; nl++)
		continue;
	if(nl == e)
		error("out of netlink structures");
	nl->cc = cc;
	nl->dc = dc;
	nl->mtu = mtu;
	strncpy(nl->path, path, sizeof(nl->path)-1);
	nl->path[sizeof(nl->path)-1] = 0;
	memmove(nl->ea, ea, sizeof nl->ea);
	poperror();
	nl->flag |= Dup;
	unlock(&netlinks);
	return nl;
}

static int
newunit(void)
{
	int x;

	x = incref(&units)-1;
	if(x >= Maxunits){
		decref(&units);
		x = -1;
	}
	return x;
}

static int
dropunit(void)
{
	return decref(&units);
}

/*
 * always allocate max frames.  maxout may change.
 */
static Aoedev*
newdev(uint major, uint minor, int n)
{
	Aoedev *d;
	Frame *f, *e;

	d = malloc(sizeof *d);
	f = malloc(sizeof *f*Maxframes);
	if(d == nil || f == nil) {
		free(d);
		free(f);
		error("aoe device allocation failure");
	}
	d->nframes = n;
	d->frames = f;
	for (e = f + Maxframes; f < e; f++)
		f->tag = Tfree;
	d->maxout = n;
	d->major = major;
	d->minor = minor;
	d->maxbcnt = Dbcnt;
	d->flag = Djumbo;
	d->maxmtu = Maxmtu;
	d->unit = newunit();		/* bzzt.  inaccurate if units removed */
	if(d->unit == -1){
		free(d);
		free(d->frames);
		error("too many units");
	}
	d->dl = d->dltab;
	return d;
}

static Aoedev*
mm2dev(uint major, uint minor)
{
	Aoedev *d;

	rlock(&devs);
	for(d = devs.d; d; d = d->next)
		if(d->major == major && d->minor == minor){
			runlock(&devs);
			return d;
		}
	runlock(&devs);
	eventlog("mm2dev: %ud.%ud not found\n", major, minor);
	return nil;
}

/* Find the device in our list.  If not known, add it */
static Aoedev*
getdev(uint major, uint minor, int n)
{
	Aoedev *d;

	if(major == 0xffff || minor == 0xff)
		return 0;
	wlock(&devs);
	if(waserror()){
		wunlock(&devs);
		nexterror();
	}
	for(d = devs.d; d; d = d->next)
		if(d->major == major && d->minor == minor)
			break;
	if(d == nil) {
		d = newdev(major, minor, n);
		d->next = devs.d;
		devs.d = d;
	}
	poperror();
	wunlock(&devs);
	return d;
}

static void
ataident(Aoedev *d)
{
	Aoeata *a;
	Aoehdr *h;
	Frame *f;

	f = freeframe(d);
	if(f == nil)
		return;
	f->nhdr = Aoehsz + Aoeatasz;
	memset(f->hdr, 0, f->nhdr);
	h = (Aoehdr*)f->hdr;
	if(hset(d, f, h, ACata, 1) == -1)
		return;
	a = (Aoeata*)(f->hdr + Aoehsz);
	f->srb = srbkalloc(0, 0);
	a->cmdstat = Cid;	/* ata 6, page 110 */
	a->scnt = 1;
	a->lba[3] = 0xa0;
	d->nout++;
	f->dl->npkt++;
	f->bcnt = 512;
	f->dlen = 0;
	if(waserror()){
		srbfree(f->srb);
		d->nout--;
		f->tag = Tfree;
	}else{
		devtab[f->nl->dc->type]->bwrite(f->nl->dc, allocfb(f), 0);
		poperror();
	}
}

static int
newdlea(Devlink *l, uchar *ea)
{
	int i;
	uchar *t;

	for(i = 0; i < Nea; i++){
		t = l->eatab[i];
		if(i == l->nea){
			memmove(t, ea, Eaddrlen);
			return l->nea++;
		}
		if(memcmp(t, ea, Eaddrlen) == 0)
			return i;
	}
	return -1;
}

static Devlink*
newdevlink(Aoedev *d, Netlink *n, Aoehdr *h)
{
	int i;
	Aoecfg *c;
	Devlink *l;

	c = (Aoecfg*)((uchar*)h + Aoehsz);
	for(i = 0; i < Ndevlink; i++){
		l = d->dl + i;
		if(i == d->ndl){
			d->ndl++;
			newdlea(l, h->src);
			l->datamtu = c->scnt*Aoesectsz;
			l->nl = n;
			l->flag |= Dup;
			l->mintimer = Rtmin;
			l->rttavg = Rtmax;
			return l;
		}
		if(l->nl == n){
			newdlea(l, h->src);
			l->datamtu = c->scnt*Aoesectsz;
			l->flag |= Dup;
			return l;
		}
	}
	eventlog("%æ: out of links: %s:%E to %E\n", d, n->path, n->ea, h->src);
	return 0;
}

static void
errrsp(Block *b, char *s)
{
	int n;
	Aoedev *d;
	Aoehdr *h;
	Frame *f;

	h = (Aoehdr*)b->rp;
	n = nhgetl(h->tag);
	if(n == Tmgmt || n == Tfree)
		return;
	d = mm2dev(nhgets(h->major), h->minor);
	if(d == nil)
		return;
	if(f = getframe(d, n))
		frameerror(d, f, s);
}

static void
qcfgrsp(Block *b, Netlink *nl)
{
	int cmd, cslen, blen;
	uint n, major;
	Aoedev *d;
	Aoehdr *h, *h0;
	Aoecfg *ch;
	Devlink *l;
	Frame *f;
	Srb *srb;

	h = (Aoehdr*)b->rp;
	ch = (Aoecfg*)(b->rp + Aoehsz);
	major = nhgets(h->major);
	n = nhgetl(h->tag);
	if(n != Tmgmt && n != Tfree){
		d = mm2dev(major, h->minor);
		if(d == nil)
			return;
		qlock(d);
		f = getframe(d, n);
		if(f == nil){
			qunlock(d);
			eventlog("%æ: unknown response tag %ux\n", d, n);
			return;
		}
		h0 = (Aoehdr*)f->hdr;
		cmd = h0->cmd;
		if(cmd != ACconfig){
			qunlock(d);
			eventlog("%æ: malicious server got ACconfig want %d; tag %ux\n", d, cmd, n);
			return;
		}
		cslen = nhgets(ch->cslen);
		blen = BLEN(b) - (Aoehsz + Aoecfgsz);
		if(cslen < blen && BLEN(b) > 60)
			eventlog("%æ: cfgrsp: tag %.8ux oversized %d %d\n",
				d, n, cslen, blen);
		if(cslen > blen){
			eventlog("%æ: cfgrsp: tag %.8ux runt %d %d\n",
				d, n, cslen, blen);
			cslen = blen;
		}
		memmove(f->dp, b->rp + Aoehsz + Aoecfgsz, cslen);
		srb = f->srb;
		f->dp = nil;
		f->srb = nil;
		if(srb != nil){
			srb->nout--;
			srbwakeup(srb);
			d->nout--;
			f->tag = Tfree;
		}
		qunlock(d);
		return;
	}

	cmd = ch->verccmd & 0xf;
	if(cmd != 0){
		eventlog("aoe%ud.%ud: cfgrsp: bad command %d\n", major, h->minor, cmd);
		return;
	}
	n = nhgets(ch->bufcnt);
	if(n > Maxframes)
		n = Maxframes;

	if(waserror()){
		eventlog("getdev: %ud.%ud ignored: %s\n", major, h->minor, up->errstr);
		return;
	}
	d = getdev(major, h->minor, n);
	poperror();
	if(d == 0)
		return;

	qlock(d);
	*up->errstr = 0;
	if(waserror()){
		qunlock(d);
		eventlog("%æ: %s\n", d, up->errstr);
		nexterror();
	}

	l = newdevlink(d, nl, h);		/* add this interface. */

	d->fwver = nhgets(ch->fwver);
	cslen = nhgets(ch->cslen);
	if(cslen > sizeof d->config)
		cslen = sizeof d->config;
	if(Aoehsz + Aoecfgsz + cslen > BLEN(b))
		cslen = BLEN(b) - (Aoehsz + Aoecfgsz);
	d->nconfig = cslen;
	memmove(d->config, b->rp + Aoehsz + Aoecfgsz, cslen);

	/* manually set mtu may be reset lower if conditions warrant */
	if(l){
		n = devmaxdata(d);
		if((d->flag & Djumbo) == 0)
			n = Dbcnt;
		if(n > d->maxmtu)
			n = d->maxmtu;
		if(n != d->maxbcnt){
			eventlog("%æ: setting %d byte mtu on %s:%E\n",
				d, n, nl->path, nl->ea);
			d->maxbcnt = n;
		}
	}
	if(d->nopen == 0)
		ataident(d);
	poperror();
	qunlock(d);
}

static vlong
aoeidentify(Aoedev *d, ushort *id)
{
	vlong s;

	s = idfeat(d, id);
	if(s == -1){
		eventlog("%æ: idfeat returns -1\n", d);
		return -1;
	}
	if((d->feat&Dlba) == 0){
		eventlog("%æ: no lba support\n", d);
		return -1;
	}
	d->flag |= Dup;
	memmove(d->ident, id, sizeof d->ident);
	return s;
}

static void
newvers(Aoedev *d)
{
	d->vers = incref(&drivevers);
}

static int
identify(Aoedev *d, ushort *id)
{
	vlong osectors, s;
	uchar oserial[21];

	s = aoeidentify(d, id);
	if(s == -1)
		return -1;
	osectors = d->realbsize;
	memmove(oserial, d->serial, sizeof d->serial);

	idmove(d->serial, id+10, 20);
	idmove(d->firmware, id+23, 8);
	idmove(d->model, id+27, 40);
	/* idss() */
	/* d->wwn = idwwn(d, id); */

	s *= Aoesectsz;
	if(osectors != s || memcmp(oserial, d->serial, sizeof oserial)){
		d->bsize = s;
		d->realbsize = s;
//		d->mediachange = 1;
		newvers(d);
	}
	return 0;
}

static void
atarsp(Block *b)
{
	uint n, cmd;
	ushort major;
	Aoeata *ahin, *ahout;
	Aoehdr *h, *h0;
	Aoedev *d;
	Frame *f;
	Srb *srb;

	h = (Aoehdr*)b->rp;
	major = nhgets(h->major);
	d = mm2dev(major, h->minor);
	if(d == nil)
		return;
	ahin = (Aoeata*)(b->rp + Aoehsz);
	qlock(d);
	if(waserror()){
		qunlock(d);
		nexterror();
	}
	n = nhgetl(h->tag);
	if(n == Tfree || n == Tmgmt)
		goto bail;
	f = getframe(d, n);
	if(f == nil){
		eventlog("%æ: unexpected response; tag %ux\n", d, n);
		goto bail;
	}
	h0 = (Aoehdr*)f->hdr;
	cmd = h0->cmd;
	if(cmd != ACata){
		eventlog("%æ: malicious server got ACata want %d; tag %ux\n", d, cmd, n);
		goto bail;
	}

	rtupdate(f->dl, tsince(f->tag));
	ahout = (Aoeata*)(f->hdr + Aoehsz);
	srb = f->srb;

	if(ahin->cmdstat & 0xa9){
		eventlog("%æ: ata error cmd %.2ux stat %.2ux\n",
			d, ahout->cmdstat, ahin->cmdstat);
		if(srb != nil)
			srb->error = Eio;
	} else {
		n = ahout->scnt * Aoesectsz;
		switch(ahout->cmdstat){
		case Crd:
		case Crdext:
			if(BLEN(b) - (Aoehsz + Aoeatasz) != n){
				eventlog("%æ: misread blen %zd expect %d\n",
					d, BLEN(b), n);
				goto bail;
			}
			memmove(f->dp, b->rp + Aoehsz + Aoeatasz, n);
		case Cwr:
		case Cwrext:
			if(n > Dbcnt)
				f->nl->lostjumbo = 0;
			if(f->bcnt -= n){
				f->lba += n / Aoesectsz;
				f->dp = (uchar*)f->dp + n;
				resend(d, f);
				goto bail;
			}
			break;
		case Cid:
			if(BLEN(b) - (Aoehsz + Aoeatasz) < 512){
				eventlog("%æ: runt identify blen %zd expect %d\n",
					d, BLEN(b), 512 + Aoehsz + Aoeatasz);
				goto bail;
			}
			identify(d, (ushort*)(b->rp + Aoehsz + Aoeatasz));
			free(srb);		/* BOTCH */
			srb = nil;
			break;
		default:
			eventlog("%æ: unknown ata command %.2ux \n",
				d, ahout->cmdstat);
		}
	}

	f->srb = nil;
	if(srb != nil){
		srb->nout--;
		srbwakeup(srb);
	}
	f->tag = Tfree;
	d->nout--;

	work(d);
bail:
	poperror();
	qunlock(d);
}

static void
netrdaoeproc(void *v)
{
	int idx;
	char name[Maxpath+1], *s;
	Aoehdr *h;
	Block *b;
	Netlink *nl;

	nl = (Netlink*)v;
	idx = nl - netlinks.nl;
	netlinks.reader[idx] = 1;
	kstrcpy(name, nl->path, Maxpath);

	if(waserror()){
		eventlog("netrdaoe@%s: exiting: %s\n", name, up->errstr);
		netlinks.reader[idx] = 0;
		wakeup(netlinks.rendez + idx);
		pexit(up->errstr, 1);
	}
	if(autodiscover)
		discover(0xffff, 0xff);
	for (;;) {
		if((nl->flag & Dup) == 0)
			error("netlink is down");
		if(nl->dc == nil)
			panic("netrdaoe: nl->dc == nil");
		b = devtab[nl->dc->type]->bread(nl->dc, 1<<16, 0);
		if(b == nil)
			error("network read");
		h = (Aoehdr*)b->rp;
		if(h->verflag & AFrsp)
			if(s = aoeerror(h)){
				eventlog("%s: %d.%d %s\n", nl->path,
					h->major[0]<<8 | h->major[1], h->minor, s);
				errrsp(b, s);
			}else if(h->cmd == ACata)
				atarsp(b);
			else if(h->cmd == ACconfig)
				qcfgrsp(b, nl);
			else if((h->cmd & 0xf0) != 0xf0){
				eventlog("%s: unknown cmd %d\n",
					nl->path, h->cmd);
				errrsp(b, "unknown command");
			}
		freeb(b);
	}
}

static void
getaddr(char *path, uchar *ea)
{
	int n;
	char buf[2*Eaddrlen+1];
	Chan *c;

	uprint("%s/addr", path);
	c = namec(up->genbuf, Aopen, OREAD, 0);
	if(waserror()) {
		cclose(c);
		nexterror();
	}
	n = devtab[c->type]->read(c, buf, sizeof buf-1, 0);
	cclose(c);
	poperror();
	buf[n] = 0;
	if(parseether(ea, buf) < 0)
		error("parseether failure");
}

static void
netbind(char *path)
{
	char addr[Maxpath];
	uchar ea[2*Eaddrlen+1];
	Chan *dc, *cc, *mtu;
	Netlink *nl;

	snprint(addr, sizeof addr, "%s!0x%x", path, Aoetype);
	dc = chandial(addr, nil, nil, &cc);
	snprint(addr, sizeof addr, "%s/mtu", path);
	mtu = nil;
	if(!waserror()){
		mtu = namec(addr, Aopen, OREAD, 0);
		poperror();
	}
	if(waserror()){
		cclose(dc);
		cclose(cc);
		if(mtu != nil)
			cclose(mtu);
		nexterror();
	}
	if(dc == nil || cc == nil)
		error(Enonexist);
	getaddr(path, ea);
	nl = addnet(path, cc, dc, mtu, ea);
	snprint(addr, sizeof addr, "netrdaoe@%s", path);
	kproc(addr, netrdaoeproc, nl);
	poperror();
}

static int
unbound(void *v)
{
	return *(int*)v != 0;
}

static void
netunbind(char *path)
{
	int i, idx;
	Aoedev *d, *p, *next;
	Chan *dc, *cc;
	Devlink *l;
	Frame *f;
	Netlink *n, *e;

	n = netlinks.nl;
	e = n + nelem(netlinks.nl);

	lock(&netlinks);
	for(; n < e; n++)
		if(n->dc && strcmp(n->path, path) == 0)
			break;
	unlock(&netlinks);
	if(n == e)
		error("device not bound");

	/*
	 * hunt down devices using this interface; disable
	 * this also terminates the reader.
	 */
	idx = n - netlinks.nl;
	wlock(&devs);
	for(d = devs.d; d; d = d->next){
		qlock(d);
		for(i = 0; i < d->ndl; i++){
			l = d->dl + i;
			if(l->nl == n)
				l->flag &= ~Dup;
		}
		qunlock(d);
	}
	n->flag &= ~Dup;
	wunlock(&devs);

	/* confirm reader is down. */
	while(waserror())
		;
	sleep(netlinks.rendez + idx, unbound, netlinks.reader + idx);
	poperror();

	/* reschedule packets. */
	wlock(&devs);
	for(d = devs.d; d != nil; d = d->next){
		qlock(d);
		for(i = 0; i < d->nframes; i++){
			f = d->frames + i;
			if(f->tag != Tfree && f->nl == n)
				resend(d, f);
		}
		qunlock(d);
	}
	wunlock(&devs);

	/* squeeze devlink pool.  (we assert nobody is using them now) */
	wlock(&devs);
	for(d = devs.d; d != nil; d = d->next){
		qlock(d);
		for(i = 0; i < d->ndl; i++){
			l = d->dl + i;
			if(l->nl == n)
				memmove(l, l + 1, sizeof *l * (--d->ndl - i));
		}
		qunlock(d);
	}
	wunlock(&devs);

	/* close device link. */
	lock(&netlinks);
	dc = n->dc;
	cc = n->cc;
	if(n->mtu != nil)
		cclose(n->mtu);
	memset(n, 0, sizeof *n);
	unlock(&netlinks);

	cclose(dc);
	cclose(cc);

	/* squeeze orphan devices */
	wlock(&devs);
	for(p = d = devs.d; d != nil; d = next){
		next = d->next;
		if(d->ndl > 0){
			p = d;
			continue;
		}
		qlock(d);
		downdev(d, "orphan");
		qunlock(d);
		if(p != devs.d)
			p->next = next;
		else{
			devs.d = next;
			p = devs.d;
		}
		free(d->frames);
		free(d);
		dropunit();
	}
	wunlock(&devs);
}

static void
strtoss(char *f, uint *shelf, uint *slot)
{
	char *s;

	*shelf = 0xffff;
	*slot = 0xff;
	if(f == nil)
		return;
	*shelf = strtol(f, &s, 0);
	if(s == f || *shelf > 0xffff)
		error("bad shelf");
	f = s;
	if(*f++ == '.'){
		*slot = strtol(f, &s, 0);
		if(s == f || *slot > 0xff)
			error("bad slot");
	}
}

static void
discoverstr(char *f)
{
	uint shelf, slot;

	strtoss(f, &shelf, &slot);
	discover(shelf, slot);
}

static void
removedev(Aoedev *d)
{
	int i;
	Aoedev *p;

	wlock(&devs);
	p = 0;
	if(d != devs.d)
	for(p = devs.d; p != nil; p = p->next)
		if(p->next == d)
			break;
	qlock(d);
	d->flag &= ~Dup;
	newvers(d);
	d->ndl = 0;
	qunlock(d);
	for(i = 0; i < d->nframes; i++)
		frameerror(d, d->frames+i, Enotup);

	if(p != nil)
		p->next = d->next;
	else
		devs.d = d->next;
	free(d->frames);
	free(d);
	dropunit();
	wunlock(&devs);
}


static void
aoeremove(Chan *c)
{
	switch(TYPE(c->qid)){
	default:
	case Qzero:
	case Qtopdir:
	case Qtoplog:
	case Qtopctl:
	case Qctl:
	case Qdata:
	case Qconfig:
	case Qident:
		error(Eperm);
	case Qunitdir:
		removedev(unit2dev(UNIT(c->qid)));
		break;
	}
}

static void
removestr(char *f)
{
	uint shelf, slot;
	Aoedev *d;

	strtoss(f, &shelf, &slot);
	wlock(&devs);
	for(d = devs.d; d != nil; d = d->next)
		if(shelf == d->major && slot == d->minor){
			wunlock(&devs);	/* BOTCH */
			removedev(d);
			return;
		}
	wunlock(&devs);
	error("device not bound");
}

static long
topctlwrite(void *db, long n)
{
	enum {
		Autodiscover,
		Bind,
		Debug,
		Discover,
		Rediscover,
		Remove,
		Unbind,
	};
	char *f;
	Cmdbuf *cb;
	Cmdtab *ct;
	static Cmdtab cmds[] = {
		{ Autodiscover,	"autodiscover",	0	},
		{ Bind, 	"bind", 	2	},
		{ Debug, 	"debug", 	0	},
		{ Discover, 	"discover", 	0	},
		{ Rediscover,	"rediscover",	0	},
		{ Remove,	"remove",	2	},
		{ Unbind,	"unbind",	2	},
	};

	cb = parsecmd(db, n);
	if(waserror()){
		free(cb);
		nexterror();
	}
	ct = lookupcmd(cb, cmds, nelem(cmds));
	f = cb->f[1];
	switch(ct->index){
	case Autodiscover:
		autodiscover = toggle(f, autodiscover, 1);
		break;
	case Bind:
		netbind(f);
		break;
	case Debug:
		debug = toggle(f, debug, 1);
		break;
	case Discover:
		discoverstr(f);
		break;
	case Rediscover:
		rediscover = toggle(f, rediscover, 1);
		break;
	case Remove:
		removestr(f);	/* depricated */
		break;
	case Unbind:
		netunbind(f);
		break;
	}
	poperror();
	free(cb);
	return n;
}

static long
aoewrite(Chan *c, void *db, long n, vlong off)
{
	switch(TYPE(c->qid)){
	default:
	case Qzero:
	case Qtopdir:
	case Qunitdir:
	case Qtoplog:
		error(Eperm);
	case Qtopctl:
		return topctlwrite(db, n);
	case Qctl:
	case Qdata:
	case Qconfig:
	case Qident:
		return unitwrite(c, db, n, off);
	}
}

Dev aoedevtab = {
	L'æ',
	"aoe",

	devreset,
	devinit,
	devshutdown,
	aoeattach,
	aoewalk,
	aoestat,
	aoeopen,
	devcreate,
	aoeclose,
	aoeread,
	devbread,
	aoewrite,
	devbwrite,
	aoeremove,
	devwstat,
	devpower,
	devconfig,
};
