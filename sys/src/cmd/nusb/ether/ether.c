#include <u.h>
#include <libc.h>
#include <thread.h>

#include "usb.h"
#include "dat.h"

#include <fcall.h>
#include <9p.h>
#include <ip.h>

typedef struct Tab Tab;
typedef struct Dq Dq;
typedef struct Conn Conn;
typedef struct Stats Stats;

enum
{
	Qroot,
	Qiface,
	Qclone,
	Qstats,
	Qaddr,
	Qndir,
	Qctl,
	Qdata,
	Qtype,
	Qmax,
};

struct Tab
{
	char *name;
	ulong mode;
};

Tab tab[] =
{
	"/",		DMDIR|0555,
	"etherU",	DMDIR|0555,	/* calling it *ether* makes snoopy(8) happy */
	"clone",	0666,
	"stats",	0666,
	"addr",	0444,
	"%ud",	DMDIR|0555,
	"ctl",		0666,
	"data",	0666,
	"type",	0444,
};

struct Dq
{
	QLock;

	Dq		*next;
	Req		*r;
	Req		**rt;
	Block		*q;
	Block		**qt;

	int		size;
};

struct Conn
{
	QLock;

	int		used;
	int		type;

	char		prom;
	char		bridge;
	char		bypass;
	char		headersonly;

	Dq		*dq;
};

struct Stats
{
	int		in;
	int		out;
};

uchar bcast[Eaddrlen] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

Stats stats;
Conn conn[32];
Conn *bypass;
int nconn = 0;

int nprom = 0;
int nmulti = 0;

Dev *epctl;
Dev *epin;
Dev *epout;

ulong time0;
static char *uname;

#define PATH(type, n)		((type)|((n)<<8))
#define TYPE(path)			(((uint)(path) & 0x000000FF)>>0)
#define NUM(path)			(((uint)(path) & 0xFFFFFF00)>>8)
#define NUMCONN(c)		(((uintptr)(c)-(uintptr)&conn[0])/sizeof(conn[0]))

static void
fillstat(Dir *d, uvlong path)
{
	Tab *t;

	memset(d, 0, sizeof(*d));
	d->uid = estrdup9p(uname);
	d->gid = estrdup9p(uname);
	d->qid.path = path;
	d->atime = d->mtime = time0;
	t = &tab[TYPE(path)];
	d->name = smprint(t->name, NUM(path));
	d->qid.type = t->mode>>24;
	d->mode = t->mode;
}

static void
fsattach(Req *r)
{
	if(r->ifcall.aname && r->ifcall.aname[0]){
		respond(r, "invalid attach specifier");
		return;
	}
	if(uname == nil)
		uname = estrdup9p(r->ifcall.uname);
	r->fid->qid.path = PATH(Qroot, 0);
	r->fid->qid.type = QTDIR;
	r->fid->qid.vers = 0;
	r->ofcall.qid = r->fid->qid;
	respond(r, nil);
}

static void
fsstat(Req *r)
{
	fillstat(&r->d, r->fid->qid.path);
	respond(r, nil);
}

static int
rootgen(int i, Dir *d, void*)
{
	i += Qroot+1;
	if(i == Qiface){
		fillstat(d, i);
		return 0;
	}
	return -1;
}

static int
ifacegen(int i, Dir *d, void*)
{
	i += Qiface+1;
	if(i < Qndir){
		fillstat(d, i);
		return 0;
	}
	i -= Qndir;
	if(i < nconn){
		fillstat(d, PATH(Qndir, i));
		return 0;
	}
	return -1;
}

static int
ndirgen(int i, Dir *d, void *aux)
{
	i += Qndir+1;
	if(i < Qmax){
		fillstat(d, PATH(i, NUMCONN(aux)));
		return 0;
	}
	return -1;
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	int i, n;
	char buf[32];
	ulong path;

	path = fid->qid.path;
	if(!(fid->qid.type&QTDIR))
		return "walk in non-directory";

	if(strcmp(name, "..") == 0){
		switch(TYPE(path)){
		case Qroot:
			return nil;
		case Qiface:
			qid->path = PATH(Qroot, 0);
			qid->type = tab[Qroot].mode>>24;
			return nil;
		case Qndir:
			qid->path = PATH(Qiface, 0);
			qid->type = tab[Qiface].mode>>24;
			return nil;
		default:
			return "bug in fswalk1";
		}
	}

	for(i = TYPE(path)+1; i<nelem(tab); i++){
		if(i==Qndir){
			n = atoi(name);
			snprint(buf, sizeof buf, "%d", n);
			if(n < nconn && strcmp(buf, name) == 0){
				qid->path = PATH(i, n);
				qid->type = tab[i].mode>>24;
				return nil;
			}
			break;
		}
		if(strcmp(name, tab[i].name) == 0){
			qid->path = PATH(i, NUM(path));
			qid->type = tab[i].mode>>24;
			return nil;
		}
		if(tab[i].mode&DMDIR)
			break;
	}
	return "directory entry not found";
}

static void
matchrq(Dq *d)
{
	Req *r;
	Block *b;

	while(r = d->r){
		int n;

		if((b = d->q) == nil)
			break;

		d->size -= BLEN(b);
		if((d->q = b->next) == nil)
			d->qt = &d->q;
		if((d->r = (Req*)r->aux) == nil)
			d->rt = &d->r;

		n = r->ifcall.count;
		if(n > BLEN(b))
			n = BLEN(b);
		memmove(r->ofcall.data, b->rp, n);
		r->ofcall.count = n;
		freeb(b);

		respond(r, nil);
	}
}

static void
readconndata(Req *r)
{
	Dq *d;

	d = r->fid->aux;
	qlock(d);
	r->aux = nil;
	*d->rt = r;
	d->rt = (Req**)&r->aux;
	matchrq(d);
	qunlock(d);
}

static void
etheroq(Block*, Conn*);

static void
writeconndata(Req *r)
{
	void *p;
	int n;
	Block *b;

	p = r->ifcall.data;
	n = r->ifcall.count;

	/* minimum frame length for rtl8150 */
	if(n < 60)
		n = 60;

	/* slack space for header and trailers */
	n += 44+16;

	b = allocb(n);

	/* header space */
	b->wp += 44;
	b->rp = b->wp;

	/* copy in the ethernet packet */
	memmove(b->wp, p, r->ifcall.count);
	b->wp += r->ifcall.count;

	etheroq(b, &conn[NUM(r->fid->qid.path)]);

	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

static void
fsread(Req *r)
{
	char buf[200];
	char e[ERRMAX];
	ulong path;
	int mbps;

	path = r->fid->qid.path;

	switch(TYPE(path)){
	default:
		snprint(e, sizeof e, "bug in fsread path=%lux", path);
		respond(r, e);
		break;

	case Qroot:
		dirread9p(r, rootgen, nil);
		respond(r, nil);
		break;

	case Qiface:
		dirread9p(r, ifacegen, nil);
		respond(r, nil);
		break;

	case Qstats:
		if(eplinkspeed == nil)
			mbps = 10;	/* default */
		else
			mbps = (*eplinkspeed)(epctl);
		snprint(buf, sizeof(buf),
			"in: %d\n"
			"link: %d\n"	/* for stats(8) */
			"out: %d\n"
			"mbps: %d\n"
			"addr: %E\n",
			stats.in, mbps != 0, stats.out, mbps, macaddr);
		readstr(r, buf);
		respond(r, nil);
		break;

	case Qaddr:
		snprint(buf, sizeof(buf), "%E", macaddr);
		readstr(r, buf);
		respond(r, nil);
		break;

	case Qndir:
		dirread9p(r, ndirgen, &conn[NUM(path)]);
		respond(r, nil);
		break;

	case Qctl:
		snprint(buf, sizeof(buf), "%11d ", NUM(path));
		readstr(r, buf);
		respond(r, nil);
		break;

	case Qtype:
		snprint(buf, sizeof(buf), "%11d ", conn[NUM(path)].type);
		readstr(r, buf);
		respond(r, nil);
		break;

	case Qdata:
		readconndata(r);
		break;
	}
}

static int
activemulti(uchar *ea)
{
	int i;

	for(i=0; i<nmulti; i++)
		if(memcmp(ea, multiaddr[i], Eaddrlen) == 0)
			return i;
	return -1;
}

static void
fswrite(Req *r)
{
	char e[ERRMAX];
	ulong path;
	char *p;
	int n;

	path = r->fid->qid.path;
	switch(TYPE(path)){
	case Qctl:
		n = r->ifcall.count;
		p = (char*)r->ifcall.data;
		if(n >= 6 && memcmp(p, "bridge", 6)==0){
			conn[NUM(path)].bridge = 1;
		} else if(n >= 6 && memcmp(p, "bypass", 6)==0){
			if(bypass != nil){
				respond(r, "bypass in use");
				return;
			}
			conn[NUM(path)].bypass = 1;
			bypass = &conn[NUM(path)];
		} else if(n >= 11 && memcmp(p, "headersonly", 11)==0){
			conn[NUM(path)].headersonly = 1;
		} else if(n >= 11 && memcmp(p, "promiscuous", 11)==0){
			if(conn[NUM(path)].prom == 0){
				conn[NUM(path)].prom = 1;
				if(nprom++ == 0 && eppromiscuous != nil)
					(*eppromiscuous)(epctl, 1);
			}
		} else if(n >= 9+12 && (memcmp(p, "addmulti ", 9)==0 || memcmp(p, "delmulti ", 9)==0 || memcmp(p, "remmulti ", 9)==0)){
			uchar ea[Eaddrlen];
			int i;

			if(parseether(ea, p+9) < 0){
				respond(r, "bad ether address");
				return;
			}
			i = activemulti(ea);
			if(i >= 0){
				if(*p != 'a'){
					memmove(multiaddr[i], multiaddr[--nmulti], Eaddrlen);
					if(epmulticast != nil)
						(*epmulticast)(epctl, ea, 0);
				}
			} else if(nmulti < nelem(multiaddr)){
				if(*p == 'a'){
					memmove(multiaddr[nmulti++], ea, Eaddrlen);
					if(epmulticast != nil)
						(*epmulticast)(epctl, ea, 1);
				}
			}
		} else if(n > 8 && memcmp(p, "connect ", 8)==0){
			char x[12];

			if(n - 8 >= sizeof(x)){
				respond(r, "invalid control msg");
				return;
			}

			p += 8;
			memcpy(x, p, n-8);
			x[n-8] = 0;

			conn[NUM(path)].type = strtoul(x, nil, 0);
		}
		r->ofcall.count = n;
		respond(r, nil);
		break;
	case Qdata:
		writeconndata(r);
		break;
	default:
		snprint(e, sizeof e, "bug in fswrite path=%lux", path);
		respond(r, e);
	}
}

static void
fsopen(Req *r)
{
	static int need[4] = { 4, 2, 6, 1 };
	ulong path;
	int i, n;
	Tab *t;
	Dq *d;
	Conn *c;

	/*
	 * lib9p already handles the blatantly obvious.
	 * we just have to enforce the permissions we have set.
	 */
	path = r->fid->qid.path;
	t = &tab[TYPE(path)];
	n = need[r->ifcall.mode&3];
	if((n&t->mode) != n){
		respond(r, "permission denied");
		return;
	}

	d = nil;
	r->fid->aux = nil;

	switch(TYPE(path)){
	case Qclone:
		for(i=0; i<nelem(conn); i++){
			if(conn[i].used)
				continue;
			if(i >= nconn)
				nconn = i+1;
			path = PATH(Qctl, i);
			goto CaseConn;
		}
		respond(r, "out of connections");
		return;
	case Qdata:
		d = emalloc9p(sizeof(*d));
		memset(d, 0, sizeof(*d));
		d->qt = &d->q;
		d->rt = &d->r;
		r->fid->aux = d;
	case Qndir:
	case Qctl:
	case Qtype:
	CaseConn:
		c = &conn[NUM(path)];
		qlock(c);
		if(c->used++ == 0){
			c->type = 0;
			c->prom = 0;
			c->bridge = 0;
			c->bypass = 0;
			c->headersonly = 0;
		}
		if(d != nil){
			d->next = c->dq;
			c->dq = d;
		}
		qunlock(c);
		break;
	}

	r->fid->qid.path = path;
	r->ofcall.qid.path = path;
	respond(r, nil);
}

static void
fsflush(Req *r)
{
	Req *o, **p;
	Fid *f;
	Dq *d;

	o = r->oldreq;
	f = o->fid;
	if(TYPE(f->qid.path) == Qdata){
		d = f->aux;
		qlock(d);
		for(p=&d->r; *p; p=(Req**)&((*p)->aux)){
			if(*p == o){
				if((*p = (Req*)o->aux) == nil)
					d->rt = p;
				r->oldreq = nil;
				respond(o, "interrupted");
				break;
			}
		}
		qunlock(d);
	}
	respond(r, nil);
}


static void
fsdestroyfid(Fid *fid)
{
	Conn *c;
	Dq **x, *d;
	Block *b;

	if(TYPE(fid->qid.path) >= Qndir){
		c = &conn[NUM(fid->qid.path)];
		qlock(c);
		if(d = fid->aux){
			fid->aux = nil;
			for(x=&c->dq; *x; x=&((*x)->next)){
				if(*x == d){
					*x = d->next;
					break;
				}
			}
			while(b = d->q){
				d->q = b->next;
				freeb(b);
			}
			free(d);
		}
		if(TYPE(fid->qid.path) == Qctl){
			if(c->prom){
				c->prom = 0;
				if(--nprom == 0 && eppromiscuous != nil)
					(*eppromiscuous)(epctl, 0);
			}
		}
		if(TYPE(fid->qid.path) == Qdata && c->bridge)
			memset(mactab, 0, sizeof(mactab));
		if(--c->used == 0){
			if(c->bypass)
				bypass = nil;
		}
		qunlock(c);
	}
}

static int
ifaceinit(Dev *d, Iface *iface, Ep **ein, Ep **eout)
{
	Ep *ep;
	int i;

	if(iface == nil)
		return -1;
	*ein = *eout = nil;
	for(i = 0; i < nelem(iface->ep); i++){
		ep = iface->ep[i];
		if(ep == nil)
			break;
		if(ep->type != Ebulk)
			continue;
		if(ep->dir == Eboth || ep->dir == Ein){
			if(*ein == nil)
				*ein = ep;
		}
		if(ep->dir == Eboth || ep->dir == Eout){
			if(*eout == nil)
				*eout = ep;
		}
		if(*ein != nil && *eout != nil)
			return setalt(d, iface);
	}
	return -1;
}

int
findendpoints(Dev *d, Ep **ein, Ep **eout)
{
	int i, j, ctlid, datid;
	Iface *iface, *ctlif, *datif;
	Usbdev *ud;
	Desc *desc;
	Conf *c;

	ud = d->usb;

	/* look for union descriptor with ethernet ctrl interface */
	for(i = 0; i < nelem(ud->ddesc); i++){
		if((desc = ud->ddesc[i]) == nil)
			continue;
		if(desc->data.bLength < 5 || desc->data.bbytes[0] != Cdcunion)
			continue;

		ctlid = desc->data.bbytes[1];
		datid = desc->data.bbytes[2];

		if((c = desc->conf) == nil)
			continue;

		ctlif = datif = nil;
		for(j = 0; j < nelem(c->iface); j++){
			for(iface = c->iface[j]; iface != nil; iface = iface->next){
				if(ctlif == nil && iface->id == ctlid)
					ctlif = iface;
				if(datif == nil && iface->id == datid)
					datif = iface;
			}
			if(datif != nil && ctlif != nil){
				if(Subclass(ctlif->csp) == Scether) {
					if(setconf(d, c) < 0)
						break;
					if(ifaceinit(d, datif, ein, eout) != -1)
						return 0;
				}
				break;
			}
		}		
	}

	/* try any other one that seems to be ok */
	for(i = 0; i < nelem(ud->conf); i++){
		if((c = ud->conf[i]) != nil){
			if(setconf(d, c) < 0)
				break;
			for(j = 0; j < nelem(c->iface); j++){
				for(datif = c->iface[j]; datif != nil; datif = datif->next){
					if(ifaceinit(d, datif, ein, eout) != -1)
						return 0;
				}
			}
		}
	}

	*ein = *eout = nil;

	return -1;
}

static int
inote(void *, char *msg)
{
	if(strstr(msg, "interrupt"))
		return 1;
	return 0;
}

static void
cpass(Conn *c, Block *bp)
{
	Dq *d;

	qlock(c);
	for(d = c->dq; d != nil; d = d->next){
		qlock(d);
		if(d->size < 100000){
			Block *q;

			if(d->next == nil) {
				q = bp;
				bp = nil;
			} else
				q = copyblock(bp, BLEN(bp));
			q->next = nil;
			*d->qt = q;
			d->qt = &q->next;
			d->size += BLEN(q);
			matchrq(d);
		}
		qunlock(d);
	}
	qunlock(c);

	if(bp != nil)
		freeb(bp);
}

static void
etherrtrace(Conn *c, Etherpkt *pkt, int len)
{
	Block *bp;

	bp = allocb(64);
	memmove(bp->wp, pkt, len < 64 ? len : 64);
	if(c->type != -2){
		u32int ms = nsec()/1000000LL;
		bp->wp[58] = len>>8;
		bp->wp[59] = len;
		bp->wp[60] = ms>>24;
		bp->wp[61] = ms>>16;
		bp->wp[62] = ms>>8;
		bp->wp[63] = ms;
	}
	bp->wp += 64;
	cpass(c, bp);
}

static Macent*
macent(uchar *ea)
{
	u32int h = (ea[0] | ea[1]<<8 | ea[2]<<16 | ea[3]<<24) ^ (ea[4] | ea[5]<<8);
	return &mactab[h % nelem(mactab)];
}

static Block*
ethermux(Block *bp, Conn *from)
{
	Etherpkt *pkt;
	Conn *c, *x;
	int len, multi, tome, port, type, dispose;

	len = BLEN(bp);
	if(len < ETHERHDRSIZE)
		goto Drop;
	pkt = (Etherpkt*)bp->rp;
	if(!(multi = pkt->d[0] & 1)){
		tome = memcmp(pkt->d, macaddr, Eaddrlen) == 0;
		if(!tome && from != nil && nprom == 0)
			return bp;
	} else {
		tome = 0;
		if(from == nil && nprom == 0
		&& memcmp(pkt->d, bcast, Eaddrlen) != 0
		&& activemulti(pkt->d) < 0)
			goto Drop;
	}

	port = -1;
	if(nprom){
		if((from == nil || from->bridge) && (pkt->s[0] & 1) == 0){
			Macent *t = macent(pkt->s);
			t->port = from == nil ? 0 : 1+(from - conn);
			memmove(t->ea, pkt->s, Eaddrlen);
		}
		if(!tome && !multi){
			Macent *t = macent(pkt->d);
			if(memcmp(t->ea, pkt->d, Eaddrlen) == 0)
				port = t->port;
		}
	}

	x = nil;
	type = (pkt->type[0]<<8)|pkt->type[1];
	dispose = tome || from == nil || port > 0;

	for(c = conn; c < &conn[nconn]; c++){
		if(!c->used)
			continue;
		if(c->type != type && c->type >= 0)
			continue;
		if(!tome && !multi && !c->prom)
			continue;
		if(c->bypass)
			continue;
		if(c->bridge){
			if(tome || c == from)
				continue;
			if(port >= 0 && port != 1+(c - conn))
				continue;
		}
		if(c->headersonly || c->type == -2){
			etherrtrace(c, pkt, len);
			continue;
		}
		if(dispose && x == nil)
			x = c;
		else
			cpass(c, copyblock(bp, len));
	}
	if(x != nil){
		cpass(x, bp);
		return nil;
	}

	if(dispose){
Drop:		freeb(bp);
		return nil;
	}
	return bp;
}

void
etheriq(Block *bp)
{
	if(bypass != nil){
		freeb(bp);
		return;
	}
	stats.in++;
	ethermux(bp, nil);
}

static void
etheroq(Block *bp, Conn *from)
{
	Conn *x;

	if(!from->bridge)
		memmove(((Etherpkt*)bp->rp)->s, macaddr, Eaddrlen);
	if(from->bypass)
		from = nil;
	bp = ethermux(bp, from);
	if(bp == nil)
		return;
	if((x = bypass) != nil){
		cpass(x, bp);
		return;
	}
	stats.out++;
	/* transmit frees buffer */
	(*eptransmit)(epout, bp);
}

static void
usbreadproc(void *)
{
	char err[ERRMAX];
	int nerr;

	atnotify(inote, 1);

	threadsetname("usbreadproc");

	nerr = 0;
	for(;;){
		/* receive allocates buffer and calls etheriq */
		if((*epreceive)(epin) < 0){
			rerrstr(err, sizeof(err));
			if(strstr(err, "interrupted") || strstr(err, "timed out"))
				continue;
			fprint(2, "usbreadproc: %s\n", err);
			if(++nerr < 3)
				continue;
			threadexitsall(err);
		}
		nerr = 0;
	}
}

Srv fs = 
{
.attach=	fsattach,
.destroyfid=	fsdestroyfid,
.walk1=		fswalk1,
.open=		fsopen,
.read=		fsread,
.write=		fswrite,
.stat=		fsstat,
.flush=		fsflush,
};

static void
usage(void)
{
	fprint(2, "usage: %s [-dD] [-t type] [-a addr] devid\n", argv0);
	exits("usage");
}

extern int aueinit(Dev *);
extern int a88178init(Dev *);
extern int a88772init(Dev *);
extern int a88179init(Dev *);
extern int smscinit(Dev *);
extern int lan78xxinit(Dev *);
extern int cdcinit(Dev *);
extern int urlinit(Dev *);
extern int rndisinit(Dev *);

static struct {
	char *name;
	int (*init)(Dev*);
} ethertype[] = {
	"cdc",		cdcinit,
	"smsc",		smscinit,
	"lan78xx",	lan78xxinit,
	"a88178",	a88178init,
	"a88772",	a88772init,
	"a88179",	a88179init,
	"aue",		aueinit,
	"url",		urlinit,
	"rndis",	rndisinit,
};

void
threadmain(int argc, char **argv)
{
	char s[64], *t;
	int et;
	Ep *ein, *eout;

	fmtinstall('E', eipfmt);

	et = 0;	/* CDC */
	ARGBEGIN {
	case 'd':
		debug = 1;
		break;
	case 'D':
		chatty9p++;
		break;
	case 'a':
		setmac = 1;
		if(parseether(macaddr, EARGF(usage())) != 0)
			usage();
		break;
	case 't':
		t = EARGF(usage());
		for(et=0; et<nelem(ethertype); et++)
			if(strcmp(t, ethertype[et].name) == 0)
				break;
		if(et == nelem(ethertype)){
			fprint(2, "unsupported ethertype -t %s, supported types are: ", t);
			for(et=0; et<nelem(ethertype); et++)
				fprint(2, "%s ", ethertype[et].name);
			fprint(2, "\n");
			exits("usage");
		}
		break;
	default:
		usage();
	} ARGEND;

	if(argc != 1)
		usage();

	if((epctl = getdev(*argv)) == nil)
		sysfatal("getdev: %r");

	if(findendpoints(epctl, &ein, &eout) < 0)
		sysfatal("no endpoints found");

	werrstr("");
	if((*ethertype[et].init)(epctl) < 0)
		sysfatal("%s init failed: %r", ethertype[et].name);
	if(epreceive == nil || eptransmit == nil)
		sysfatal("bug in init");

	if((epin = openep(epctl, ein)) == nil)
		sysfatal("openep: %r");
	if(ein == eout){
		incref(epin);
		epout = epin;
		opendevdata(epin, ORDWR);
	} else {
		if((epout = openep(epctl, eout)) == nil)
			sysfatal("openep: %r");
		opendevdata(epin, OREAD);
		opendevdata(epout, OWRITE);
	}

	proccreate(usbreadproc, nil, 8*1024);

	atnotify(inote, 1);
	time0 = time(0);
	tab[Qiface].name = smprint("etherU%s", epctl->hname);
	snprint(s, sizeof(s), "%d.ether", epctl->id);

	threadpostsharesrv(&fs, nil, "usbnet", s);

	threadexits(0);
}

Block*
allocb(int size)
{
	Block *b;

	b = emalloc9p(sizeof(*b) + size);
	b->lim = b->base + size;
	b->rp = b->base;
	b->wp = b->base;
	b->next = nil;
	return b;
}

Block*
copyblock(Block *b, int count)
{
	Block *nb;

	if(count > BLEN(b))
		count = BLEN(b);
	nb = allocb(count);
	memmove(nb->wp, b->rp, count);
	nb->wp += count;
	return nb;
}
