/*
 * IP Ethernet bridge
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../ip/ip.h"
#include "../ip/ipv6.h"
#include "../port/netif.h"
#include "../port/error.h"

typedef struct Bridge 	Bridge;
typedef struct Port 	Port;
typedef struct Centry	Centry;
typedef struct Tcphdr	Tcphdr;

enum
{
	Qtopdir=	1,		/* top level directory */

	Qbridgedir,			/* bridge* directory */
	Qbctl,
	Qstats,
	Qcache,
	Qlog,

	Qportdir,			/* directory for a protocol */
	Qpctl,
	Qlocal,
	Qstatus,

	MaxQ,

	Maxbridge=	4,
	Maxport=	128,		// power of 2
	CacheHash=	257,		// prime
	CacheLook=	5,		// how many cache entries to examine
	CacheSize=	(CacheHash+CacheLook-1),
	CacheTimeout=	5*60,		// timeout for cache entry in seconds
	MaxMTU=	IP_MAX,	// allow for jumbo frames and large UDP

	TcpMssMax = 1300,		// max desirable Tcp MSS value
	TunnelMtu = 1400,
};

static Dirtab bridgedirtab[]={
	"ctl",		{Qbctl},	0,	0666,
	"stats",	{Qstats},	0,	0444,
	"cache",	{Qcache},	0,	0444,
	"log",		{Qlog},		0,	0666,
};

static Dirtab portdirtab[]={
	"ctl",		{Qpctl},	0,	0666,
	"local",	{Qlocal},	0,	0444,
	"status",	{Qstatus},	0,	0444,
};

enum {
	Logcache=	(1<<0),
	Logmcast=	(1<<1),
};

// types of interfaces
enum
{
	Tether,
	Ttun,
};

static Logflag logflags[] =
{
	{ "cache",	Logcache, },
	{ "multicast",	Logmcast, },
	{ nil,		0, },
};

static Dirtab	*dirtab[MaxQ];

#define TYPE(x) 	(((ulong)(x).path) & 0xff)
#define PORT(x) 	((((ulong)(x).path) >> 8)&(Maxport-1))
#define QID(x, y) 	(((x)<<8) | (y))

#define VID(tag)	((tag) & 0xFFF)

struct Centry
{
	ushort	vid;
	uchar	d[Eaddrlen];
	int	port;
	long	expire;		// entry expires this many seconds after bootime
	long	src;
	long	dst;
};

struct Bridge
{
	QLock;
	int	nport;
	Port	*port[Maxport];
	Centry	cache[CacheSize];
	ulong	hit;
	ulong	miss;
	ulong	copy;
	long	delay0;		// constant microsecond delay per packet
	long	delayn;		// microsecond delay per byte
	int	tcpmss;		// modify tcpmss value

	Log;
};

struct Port
{
	Ref;
	int	id;
	Bridge	*bridge;
	int	closed;

	Chan	*data[2];	// channel to data

	Proc	*readp;		// read proc
	
	// the following uniquely identifies the port
	int	type;
	char	name[KNAMELEN];
	
	// owner hash - avoids bind/unbind races
	ulong	ownhash;

	// various stats
	int	in;		// number of packets read
	int	inmulti;	// multicast or broadcast
	int	inunknown;	// unknown address
	int	out;		// number of packets read
	int	outmulti;	// multicast or broadcast
	int	outunknown;	// unknown address
	int	outfrag;	// fragmented the packet
	int	nentry;		// number of cache entries for this port

	// 802.1q
	ushort	pvid;
	ushort	prio;
	uchar	member[0x1000/8];
};

enum {
	EOLOPT		= 0,
	NOOPOPT		= 1,
	MSSOPT		= 2,
	MSS_LENGTH	= 4,		/* Mean segment size */
	SYN		= 0x02,		/* Pkt. is synchronise */
	TCPHDR		= 20,
};

struct Tcphdr
{
	uchar	sport[2];
	uchar	dport[2];
	uchar	seq[4];
	uchar	ack[4];
	uchar	flag[2];
	uchar	win[2];
	uchar	cksum[2];
	uchar	urg[2];
};

static Bridge bridgetab[Maxbridge];

static int m2p[] = {
	[OREAD]		4,
	[OWRITE]	2,
	[ORDWR]		6
};

static int	bridgegen(Chan *c, char*, Dirtab*, int, int s, Dir *dp);

static void	portbind(Bridge *b, int argc, char *argv[]);
static void	portunbind(Bridge *b, int argc, char *argv[]);
static void	portvlan(Bridge *b, int argc, char *argv[]);

static void	etherread(void *a);
static char	*cachedump(Bridge *b);
static void	portfree(Port *port);
static void	cacheflushport(Bridge *b, int port);
static void	etherwrite(Port *port, Block *bp, ushort tag);

static void
initmember(Port *port)
{
	memset(port->member, 0, sizeof(port->member));
}
static void
addmember(Port *port, ushort vid)
{
	/* vlan ids 0 and 4095 are reserved */
	if(vid == 0 || vid >= 0xFFF)
		return;

	port->member[vid/8] |= 1 << (vid % 8);
}
static int
ismember(Port *port, ushort vid)
{
	return port->member[vid/8] & (1 << (vid%8));
}

static Block*
tagpkt(Block *bp, ushort tag)
{
	uchar *h;
	bp = padblock(bp, 4);
	memmove(bp->rp, bp->rp+4, 2*Eaddrlen);
	h = bp->rp + 2*Eaddrlen;
	h[0] = 0x81;
	h[1] = 0x00;
	h[2] = tag>>8;
	h[3] = tag;
	return bp;
}

static ushort
untagpkt(Block *bp)
{
	uchar *h = bp->rp + 2*Eaddrlen;
	ushort tag = h[2]<<8 | h[3];
	memmove(bp->rp+4, bp->rp, 2*Eaddrlen);
	bp->rp += 4;
	return tag;
}

static void
bridgeinit(void)
{
	int i;
	Dirtab *dt;

	// setup dirtab with non directory entries
	for(i=0; i<nelem(bridgedirtab); i++) {
		dt = bridgedirtab + i;
		dirtab[TYPE(dt->qid)] = dt;
	}
	for(i=0; i<nelem(portdirtab); i++) {
		dt = portdirtab + i;
		dirtab[TYPE(dt->qid)] = dt;
	}
}

static Chan*
bridgeattach(char *spec)
{
	Chan *c;
	ulong dev;

	dev = strtoul(spec, nil, 10);
	if(dev >= Maxbridge)
		error(Enodev);

	c = devattach('B', spec);
	mkqid(&c->qid, QID(0, Qtopdir), 0, QTDIR);
	c->dev = dev;
	return c;
}

static Walkqid*
bridgewalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, (Dirtab*)0, 0, bridgegen);
}

static int
bridgestat(Chan* c, uchar* db, int n)
{
	return devstat(c, db, n, (Dirtab *)0, 0L, bridgegen);
}

static Chan*
bridgeopen(Chan* c, int omode)
{
	int perm;
	Bridge *b;

	omode &= 3;
	perm = m2p[omode];
	USED(perm);

	b = bridgetab + c->dev;
	USED(b);

	switch(TYPE(c->qid)) {
	default:
		break;
	case Qlog:
		logopen(b);
		break;
	case Qcache:
		c->aux = cachedump(b);
		break;
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
bridgeclose(Chan* c)
{
	Bridge *b  = bridgetab + c->dev;

	switch(TYPE(c->qid)) {
	case Qcache:
		if(c->flag & COPEN)
			free(c->aux);
		break;
	case Qlog:
		if(c->flag & COPEN)
			logclose(b);
		break;
	}
}

static int
getvlancfg(Port *port, char *buf, int nbuf)
{
	char *s = buf, *e = buf + nbuf;
	int i, j;

	s = seprint(s, e, "%d", (int)port->pvid);
	if(port->prio)
		s = seprint(s, e, "#%d", (int)port->prio>>12);
	i = 0;
	for(j = 1; j <= 0xFFF; j++){
		if(ismember(port, j)){
			if(i == 0)
				i = j;
			continue;
		} else if(i == 0)
			continue;
		if(i == j-1)
			s = seprint(s, e, ",%d", i);
		else
			s = seprint(s, e, ",%d-%d", i, j-1);
		i = 0;
	}
	return s - buf;
}

static long
bridgeread(Chan *c, void *a, long n, vlong off)
{
	char buf[512];
	Bridge *b = bridgetab + c->dev;
	Port *port;
	int i, ingood, outgood;

	USED(off);
	switch(TYPE(c->qid)) {
	default:
		error(Egreg);
	case Qtopdir:
	case Qbridgedir:
	case Qportdir:
		return devdirread(c, a, n, 0, 0, bridgegen);
	case Qlog:
		return logread(b, a, off, n);
	case Qlocal:
		return 0;	/* TO DO */
	case Qstatus:
		qlock(b);
		if(waserror()){
			qunlock(b);
			nexterror();
		}
		port = b->port[PORT(c->qid)];
		if(port == 0)
			strcpy(buf, "unbound\n");
		else {
			i = 0;
			switch(port->type) {
			default:
				panic("bridgeread: unknown port type: %d",
					port->type);
			case Tether:
				i += snprint(buf+i, sizeof(buf)-i, "ether %s: ", port->name);
				break;
			case Ttun:
				i += snprint(buf+i, sizeof(buf)-i, "tunnel %s: ", port->name);
				break;
			}

			i += snprint(buf+i, sizeof(buf)-i, "vlan=");
			i += getvlancfg(port, buf+i, sizeof(buf)-i);
			i += snprint(buf+i, sizeof(buf)-i, " ");

			ingood = port->in - port->inmulti - port->inunknown;
			outgood = port->out - port->outmulti - port->outunknown;
			i += snprint(buf+i, sizeof(buf)-i,
				"in=%d(%d:%d:%d) out=%d(%d:%d:%d:%d)\n",
				port->in, ingood, port->inmulti, port->inunknown,
				port->out, outgood, port->outmulti,
				port->outunknown, port->outfrag);
			USED(i);
		}
		poperror();
		qunlock(b);
		return readstr(off, a, n, buf);
	case Qbctl:
		snprint(buf, sizeof(buf), "%s tcpmss\ndelay %ld %ld\n",
			b->tcpmss ? "set" : "clear", b->delay0, b->delayn);
		n = readstr(off, a, n, buf);
		return n;
	case Qcache:
		n = readstr(off, a, n, c->aux);
		return n;
	case Qstats:
		snprint(buf, sizeof(buf), "hit=%uld miss=%uld copy=%uld\n",
			b->hit, b->miss, b->copy);
		n = readstr(off, a, n, buf);
		return n;
	}
}

static void
bridgeoption(Bridge *b, char *option, int value)
{
	if(strcmp(option, "tcpmss") == 0)
		b->tcpmss = value;
	else
		error("unknown bridge option");
}

static long
bridgewrite(Chan *c, void *a, long n, vlong off)
{
	Bridge *b = bridgetab + c->dev;
	Cmdbuf *cb;
	char *arg0, *p;
	
	USED(off);
	switch(TYPE(c->qid)) {
	default:
		error(Eperm);
	case Qbctl:
		cb = parsecmd(a, n);
		qlock(b);
		if(waserror()) {
			qunlock(b);
			free(cb);
			nexterror();
		}
		if(cb->nf == 0)
			error("short write");
		arg0 = cb->f[0];
		if(strcmp(arg0, "bind") == 0) {
			portbind(b, cb->nf-1, cb->f+1);
		} else if(strcmp(arg0, "unbind") == 0) {
			portunbind(b, cb->nf-1, cb->f+1);
		} else if(strcmp(arg0, "vlan") == 0) {
			portvlan(b, cb->nf-1, cb->f+1);
		} else if(strcmp(arg0, "cacheflush") == 0) {
			log(b, Logcache, "cache flush\n");
			memset(b->cache, 0, CacheSize*sizeof(Centry));
		} else if(strcmp(arg0, "set") == 0) {
			if(cb->nf != 2)
				error("usage: set option");
			bridgeoption(b, cb->f[1], 1);
		} else if(strcmp(arg0, "clear") == 0) {
			if(cb->nf != 2)
				error("usage: clear option");
			bridgeoption(b, cb->f[1], 0);
		} else if(strcmp(arg0, "delay") == 0) {
			if(cb->nf != 3)
				error("usage: delay delay0 delayn");
			b->delay0 = strtol(cb->f[1], nil, 10);
			b->delayn = strtol(cb->f[2], nil, 10);
		} else
			error("unknown control request");
		poperror();
		qunlock(b);
		free(cb);
		return n;
	case Qlog:
		cb = parsecmd(a, n);
		p = logctl(b, cb->nf, cb->f, logflags);
		free(cb);
		if(p != nil)
			error(p);
		return n;
	}
}

static int
bridgegen(Chan *c, char *, Dirtab*, int, int s, Dir *dp)
{
	Bridge *b = bridgetab + c->dev;
	int type = TYPE(c->qid);
	Dirtab *dt;
	Qid qid;

	if(s  == DEVDOTDOT){
		switch(TYPE(c->qid)){
		case Qtopdir:
		case Qbridgedir:
			snprint(up->genbuf, sizeof(up->genbuf), "#B%ld", c->dev);
			mkqid(&qid, Qtopdir, 0, QTDIR);
			devdir(c, qid, up->genbuf, 0, eve, 0555, dp);
			break;
		case Qportdir:
			snprint(up->genbuf, sizeof(up->genbuf), "bridge%ld", c->dev);
			mkqid(&qid, Qbridgedir, 0, QTDIR);
			devdir(c, qid, up->genbuf, 0, eve, 0555, dp);
			break;
		default:
			panic("bridgewalk %llux", c->qid.path);
		}
		return 1;
	}

	switch(type) {
	default:
		/* non-directory entries end up here */
		if(c->qid.type & QTDIR)
			panic("bridgegen: unexpected directory");	
		if(s != 0)
			return -1;
		dt = dirtab[TYPE(c->qid)];
		if(dt == nil)
			panic("bridgegen: unknown type: %lud", TYPE(c->qid));
		devdir(c, c->qid, dt->name, dt->length, eve, dt->perm, dp);
		return 1;
	case Qtopdir:
		if(s != 0)
			return -1;
		snprint(up->genbuf, sizeof(up->genbuf), "bridge%ld", c->dev);
		mkqid(&qid, QID(0, Qbridgedir), 0, QTDIR);
		devdir(c, qid, up->genbuf, 0, eve, 0555, dp);
		return 1;
	case Qbridgedir:
		if(s<nelem(bridgedirtab)) {
			dt = bridgedirtab+s;
			devdir(c, dt->qid, dt->name, dt->length, eve, dt->perm, dp);
			return 1;
		}
		s -= nelem(bridgedirtab);
		if(s >= b->nport)
			return -1;
		mkqid(&qid, QID(s, Qportdir), 0, QTDIR);
		snprint(up->genbuf, sizeof(up->genbuf), "%d", s);
		devdir(c, qid, up->genbuf, 0, eve, 0555, dp);
		return 1;
	case Qportdir:
		if(s>=nelem(portdirtab))
			return -1;
		dt = portdirtab+s;
		mkqid(&qid, QID(PORT(c->qid),TYPE(dt->qid)), 0, QTFILE);
		devdir(c, qid, dt->name, dt->length, eve, dt->perm, dp);
		return 1;
	}
}

static char*
vlanrange(char *s, int *i, int *j)
{
	char *x;

	*j = -1;
	*i = strtol(s, &x, 10);
	if(x <= s)
		return x;
	if(*i < 0) {
		/* -nnn */
		*j = -(*i);
		*i = 1;
	} else if(*s == '-') {
		/* nnn- */
		s = x;
		*j = -strtol(s, &x, 10);
		if(x <= s || *j <= 0)
			*j = 0xFFE;
	} else {
		/* nnn */
		*j = *i;
	}
	return x;
}

// set the vlan configuration of a port.
// first number is the pvid (port vlan id)
// followed by zero or more other vlan members.
// members can be specified as comma separated ranges:
//   -10,13,50-60,1000- => [1..10],13,[50-60],[1000-4094]
static void
setvlancfg(Port *port, char *cfg)
{
	int i, j;

	initmember(port);
	port->pvid = strtol(cfg, &cfg, 10);
	if(port->pvid >= 0xFFF)
		port->pvid = 0;
	if(*cfg == '#'){
		cfg++;
		port->prio = strtol(cfg, &cfg, 10)<<12;
	} else {
		port->prio = 0<<12;
	}
	while(*cfg == ','){
		cfg = vlanrange(++cfg, &i, &j);
		for(; i <= j; i++)
			addmember(port, i);
	}
	addmember(port, port->pvid);
}

// assumes b is locked
static void
portbind(Bridge *b, int argc, char *argv[])
{
	Port *port;
	Chan *ctl;
	int type = 0, i, n;
	ulong ownhash;
	char *dev, *dev2, *vlan;
	char buf[100], name[KNAMELEN], path[8*KNAMELEN];
	static char usage[] = "usage: bind ether|tunnel name ownhash dev [dev2] [pvid[,vlans...]]";

	dev2 = nil;
	vlan = "1";	// default vlan configuration
	memset(name, 0, KNAMELEN);
	if(argc < 4)
		error(usage);
	if(strcmp(argv[0], "ether") == 0) {
		if(argc > 4)
			vlan = argv[4];
		type = Tether;
		strncpy(name, argv[1], KNAMELEN);
		name[KNAMELEN-1] = 0;
	} else if(strcmp(argv[0], "tunnel") == 0) {
		if(argc < 5)
			error(usage);
		if(argc > 5)
			vlan = argv[5];
		type = Ttun;
		strncpy(name, argv[1], KNAMELEN);
		name[KNAMELEN-1] = 0;
		dev2 = argv[4];
	} else
		error(usage);
	ownhash = atoi(argv[2]);
	dev = argv[3];
	for(i=0; i<b->nport; i++) {
		port = b->port[i];
		if(port != nil && port->type == type &&
		    memcmp(port->name, name, KNAMELEN) == 0)
			error("port in use");
	}
	for(i=0; i<Maxport; i++)
		if(b->port[i] == nil)
			break;
	if(i == Maxport)
		error("no more ports");
	port = smalloc(sizeof(Port));
	port->ref = 1;
	port->id = i;
	port->ownhash = ownhash;

	if(waserror()) {
		portfree(port);
		nexterror();
	}
	port->type = type;
	memmove(port->name, name, KNAMELEN);
	setvlancfg(port, vlan);

	switch(port->type) {
	default:
		panic("portbind: unknown port type: %d", type);
	case Tether:
		snprint(path, sizeof(path), "%s/clone", dev);
		ctl = namec(path, Aopen, ORDWR, 0);
		if(waserror()) {
			cclose(ctl);
			nexterror();
		}
		// check addr?

		// get directory name
		n = devtab[ctl->type]->read(ctl, buf, sizeof(buf)-1, 0);
		buf[n] = 0;
		snprint(path, sizeof(path), "%s/%lud/data", dev, strtoul(buf, 0, 0));

		// setup connection to be promiscuous
		snprint(buf, sizeof(buf), "connect -1");
		devtab[ctl->type]->write(ctl, buf, strlen(buf), 0);
		snprint(buf, sizeof(buf), "nonblocking");
		devtab[ctl->type]->write(ctl, buf, strlen(buf), 0);
		snprint(buf, sizeof(buf), "promiscuous");
		devtab[ctl->type]->write(ctl, buf, strlen(buf), 0);
		snprint(buf, sizeof(buf), "bridge");
		devtab[ctl->type]->write(ctl, buf, strlen(buf), 0);

		// open data port
		port->data[0] = namec(path, Aopen, ORDWR, 0);
		// dup it
		incref(port->data[0]);
		port->data[1] = port->data[0];

		poperror();
		cclose(ctl);		

		break;
	case Ttun:
		port->data[0] = namec(dev, Aopen, OREAD, 0);
		port->data[1] = namec(dev2, Aopen, OWRITE, 0);
		break;
	}

	poperror();

	/* committed to binding port */
	b->port[port->id] = port;
	port->bridge = b;
	if(b->nport <= port->id)
		b->nport = port->id+1;

	// assumes kproc always succeeds
	incref(port);
	snprint(buf, sizeof(buf), "bridge:%s", dev);
	kproc(buf, etherread, port);
}

static int
getport(Bridge *b, int argc, char **argv)
{
	static char usage[] = "usage: ... ether|tunnel name [ownhash]";
	int type = 0, i;
	char name[KNAMELEN];
	ulong ownhash;
	Port *port = nil;

	memset(name, 0, KNAMELEN);
	if(argc < 2)
		error(usage);
	if(strcmp(argv[0], "ether") == 0) {
		type = Tether;
		strncpy(name, argv[1], KNAMELEN);
		name[KNAMELEN-1] = 0;
	} else if(strcmp(argv[0], "tunnel") == 0) {
		type = Ttun;
		strncpy(name, argv[1], KNAMELEN);
		name[KNAMELEN-1] = 0;
	} else
		error(usage);
	if(argc == 3)
		ownhash = atoi(argv[2]);
	else
		ownhash = 0;
	for(i=0; i<b->nport; i++) {
		port = b->port[i];
		if(port != nil && port->type == type &&
		    memcmp(port->name, name, KNAMELEN) == 0)
			break;
	}
	if(i == b->nport)
		error("port not found");
	if(ownhash != 0 && port->ownhash != 0 && ownhash != port->ownhash)
		error("bad owner hash");
	return i;
}

// assumes b is locked
static void
portunbind(Bridge *b, int argc, char *argv[])
{
	int i = getport(b, argc, argv);
	Port *port = b->port[i];
	port->closed = 1;
	b->port[i] = nil;	// port is now unbound
	cacheflushport(b, i);

	// try and stop reader
	if(port->readp)
		postnote(port->readp, 1, "unbind", 0);
	portfree(port);
}

static void
portvlan(Bridge *b, int argc, char *argv[])
{
	int i = getport(b, argc-1, argv+1);
	cacheflushport(b, i);
	setvlancfg(b->port[i], argv[0]);
}

static Centry *
cachehash(Bridge *b, uchar d[Eaddrlen], ushort vid)
{
	uint h = (uint)vid*587;
	int i;

	for(i=0; i<Eaddrlen; i++) {
		h *= 7;
		h += d[i];
	}
	return &b->cache[h % CacheHash];
}

// assumes b is locked
static Centry *
cachelookup(Bridge *b, uchar d[Eaddrlen], ushort vid)
{
	int i;
	Centry *p;
	long sec;

	// dont cache multicast or broadcast
	if(d[0] & 1)
		return 0;
	p = cachehash(b, d, vid);
	sec = TK2SEC(m->ticks);
	for(i=0; i<CacheLook; i++,p++) {
		if(p->vid == vid && memcmp(d, p->d, Eaddrlen) == 0) {
			p->dst++;
			if(sec >= p->expire) {
				log(b, Logcache, "expired cache entry: %E %d %d\n",
					d, (int)vid, p->port);
				return nil;
			}
			p->expire = sec + CacheTimeout;
			return p;
		}
	}
	log(b, Logcache, "cache miss: %E %d\n", d, (int)vid);
	return nil;
}

// assumes b is locked
static void
cacheupdate(Bridge *b, uchar d[Eaddrlen], int port, ushort vid)
{
	int i;
	Centry *p, *pp;
	long sec;

	// dont cache multicast or broadcast
	if(d[0] & 1) {
		log(b, Logcache, "bad source address: %E %d %d\n", d, (int)vid, port);
		return;
	}
	p = pp = cachehash(b, d, vid);
	sec = p->expire;

	// look for oldest entry
	for(i=0; i<CacheLook; i++,p++) {
		if(p->vid == vid && memcmp(p->d, d, Eaddrlen) == 0) {
			p->expire = TK2SEC(m->ticks) + CacheTimeout;
			if(p->port != port) {
				log(b, Logcache, "NIC changed port: %E %d %d->%d\n",
					d, (int)vid, p->port, port);
				p->port = port;
			}
			p->src++;
			return;
		}
		if(p->expire < sec) {
			sec = p->expire;
			pp = p;
		}
	}
	if(pp->expire != 0)
		log(b, Logcache, "bumping from cache: %E %d %d\n", pp->d, (int)pp->vid, pp->port);
	log(b, Logcache, "adding to cache: %E %d %d\n", d, (int)vid, port);
	pp->expire = TK2SEC(m->ticks) + CacheTimeout;
	pp->vid = vid;
	memmove(pp->d, d, Eaddrlen);
	pp->port = port;
	pp->src = 1;
	pp->dst = 0;
}

// assumes b is locked
static void
cacheflushport(Bridge *b, int port)
{
	Centry *ce;
	int i;

	ce = b->cache;
	for(i=0; i<CacheSize; i++,ce++) {
		if(ce->port != port)
			continue;
		memset(ce, 0, sizeof(Centry));
	}
}

static char *
cachedump(Bridge *b)
{
	int i, n;
	long sec, off;
	char *buf, *p, *ep;
	Centry *ce;
	char c;

	qlock(b);
	if(waserror()) {
		qunlock(b);
		nexterror();
	}
	sec = TK2SEC(m->ticks);
	n = 0;
	for(i=0; i<CacheSize; i++)
		if(b->cache[i].expire != 0)
			n++;
	// change if print format is changed
	n *= (13+5+3+11+11+11+2);
	n += 10;	// some slop at the end
	buf = malloc(n);
	if(buf == nil)
		error(Enomem);
	p = buf;
	ep = buf + n;
	ce = b->cache;
	off = seconds() - sec;
	for(i=0; i<CacheSize; i++,ce++) {
		if(ce->expire == 0)
			continue;	
		c = (sec < ce->expire)?'v':'e';
		p += snprint(p, ep-p, "%E %4d %2d %10ld %10ld %10ld %c\n",
			ce->d, (int)ce->vid, ce->port,
			ce->src, ce->dst, ce->expire+off, c);
	}
	*p = 0;
	poperror();
	qunlock(b);

	return buf;
}

// assumes b is locked, no error return
static void
ethermultiwrite(Bridge *b, Block *bp, Port *port, ushort tag)
{
	Port *oport;
	Etherpkt *ep;
	int i, mcast;

	ep = (Etherpkt*)bp->rp;
	mcast = ep->d[0] & 1;		/* multicast bit of ethernet address */

	oport = nil;
	for(i=0; i<b->nport; i++) {
		if(i == port->id || b->port[i] == nil || !ismember(b->port[i], VID(tag)))
			continue;
		/*
		 * we need to forward multicast packets for ipv6,
		 * so always do it.
		 */
		if(mcast)
			b->port[i]->outmulti++;
		else
			b->port[i]->outunknown++;

		// delay one so that the last write does not copy
		if(oport != nil) {
			b->copy++;
			etherwrite(oport, copyblock(bp, BLEN(bp)), tag);
		}
		oport = b->port[i];
	}

	// last write free block
	if(oport)
		etherwrite(oport, bp, tag);
	else
		freeb(bp);
}

static void
tcpmsshack(Etherpkt *epkt, int n)
{
	int hl, optlen;
	Tcphdr *tcphdr;
	ulong mss, cksum;
	uchar *optr;

	/* ignore non-ipv4 packets */
	switch(nhgets(epkt->type)){
	case ETIP4:
	case ETIP6:
		break;
	default:
		return;
	}
	n -= ETHERHDRSIZE;
	if(n < 1)
		return;
	switch(epkt->data[0]&0xF0){
	case IP_VER4:
		hl = (epkt->data[0]&15)<<2;
		if(n < hl+TCPHDR || hl < IP4HDR || epkt->data[9] != TCP)
			return;
		n -= hl;
		tcphdr = (Tcphdr*)(epkt->data + hl);
		break;
	case IP_VER6:
		if(n < IP6HDR+TCPHDR || epkt->data[6] != TCP)
			return;
		n -= IP6HDR;
		tcphdr = (Tcphdr*)(epkt->data + IP6HDR);
		break;
	default:
		return;
	}
	// MSS can only appear in SYN packet
	if(!(tcphdr->flag[1] & SYN))
		return;
	hl = (tcphdr->flag[0] & 0xf0)>>2;
	if(n < hl)
		return;

	// check for MSS option
	optr = (uchar*)tcphdr + TCPHDR;
	n = hl - TCPHDR;
	for(;;) {
		if(n <= 0 || *optr == EOLOPT)
			return;
		if(*optr == NOOPOPT) {
			n--;
			optr++;
			continue;
		}
		optlen = optr[1];
		if(optlen < 2 || optlen > n)
			return;
		if(*optr == MSSOPT && optlen == MSS_LENGTH)
			break;
		n -= optlen;
		optr += optlen;
	}

	mss = nhgets(optr+2);
	if(mss <= TcpMssMax)
		return;

	// fit checksum
	cksum = nhgets(tcphdr->cksum);
	if(optr-(uchar*)tcphdr & 1) {
// print("tcpmsshack: odd alignment!\n");
		// odd alignments are a pain
		cksum += nhgets(optr+1);
		cksum -= (optr[1]<<8)|(TcpMssMax>>8);
		cksum += (cksum>>16);
		cksum &= 0xffff;
		cksum += nhgets(optr+3);
		cksum -= ((TcpMssMax&0xff)<<8)|optr[4];
		cksum += (cksum>>16);
	} else {
		cksum += mss;
		cksum -= TcpMssMax;
		cksum += (cksum>>16);
	}
	hnputs(tcphdr->cksum, cksum);
	hnputs(optr+2, TcpMssMax);
}

/*
 *  process to read from the ethernet
 */
static void
etherread(void *a)
{
	Port *port = a;
	Bridge *b = port->bridge;
	Block *bp;
	Etherpkt *ep;
	Centry *ce;
	long md, n;
	ushort type, tag;
	
	qlock(b);
	port->readp = up;	/* hide identity under a rock for unbind */

	while(!port->closed){
		// release lock to read - error means it is time to quit
		qunlock(b);
		if(waserror()) {
			print("etherread read error: %s\n", up->errstr);
			qlock(b);
			break;
		}
		bp = devtab[port->data[0]->type]->bread(port->data[0], MaxMTU, 0);
		poperror();
		qlock(b);
		if(bp == nil)
			break;
		n = BLEN(bp);
		if(port->closed || n < ETHERHDRSIZE){
			freeb(bp);
			continue;
		}
		port->in++;

		ep = (Etherpkt*)bp->rp;
		type = ep->type[0]<<8|ep->type[1];
		if(type != 0x8100) {
			tag = port->pvid;
			if(tag == 0){
				freeb(bp);
				continue;
			}
			tag |= port->prio;
		} else {
			tag = untagpkt(bp);
			if(!ismember(port, VID(tag))) {
				if(VID(tag) != 0 || port->pvid == 0){
					freeb(bp);
					continue;
				}
				tag |= port->pvid;
			}
			ep = (Etherpkt*)bp->rp;
			type = ep->type[0]<<8|ep->type[1];
		}

		cacheupdate(b, ep->s, port->id, VID(tag));
		if(b->tcpmss)
			tcpmsshack(ep, n);

		/*
		 * delay packets to simulate a slow link
		 */
		if(b->delay0 != 0 || b->delayn != 0){
			md = b->delay0 + b->delayn * n;
			if(md > 0)
				microdelay(md);
		}

		if(ep->d[0] & 1) {
			log(b, Logmcast, "multicast: port=%d tag=%#.4ux src=%E dst=%E type=%#.4ux\n",
				port->id, tag, ep->s, ep->d, type);
			port->inmulti++;
			ethermultiwrite(b, bp, port, tag);
		} else {
			ce = cachelookup(b, ep->d, VID(tag));
			if(ce == nil) {
				b->miss++;
				port->inunknown++;
				ethermultiwrite(b, bp, port, tag);
			}else if(ce->port != port->id){
				b->hit++;
				etherwrite(b->port[ce->port], bp, tag);
			}else
				freeb(bp);
		}
	}
//	print("etherread: trying to exit\n");
	port->readp = nil;
	portfree(port);
	qunlock(b);
	pexit("hangup", 1);
}

static int
fragment(Etherpkt *epkt, int n)
{
	Ip4hdr *iphdr;

	if(n <= TunnelMtu)
		return 0;

	/* ignore non-ipv4 packets */
	if(nhgets(epkt->type) != ETIP4)
		return 0;
	iphdr = (Ip4hdr*)(epkt->data);
	n -= ETHERHDRSIZE;
	/*
	 * ignore: IP runt packets, bad packets (I don't handle IP
	 * options for the moment), packets with don't-fragment set,
	 * and short blocks.
	 */
	if(n < IP4HDR || iphdr->vihl != (IP_VER4|IP_HLEN4) ||
	    iphdr->frag[0] & (IP_DF>>8) || nhgets(iphdr->length) > n)
		return 0;

	return 1;
}

static void
etherwrite(Port *port, Block *bp, ushort tag)
{
	Ip4hdr *eh, *feh;
	Etherpkt *epkt;
	int lid, len, seglen, dlen, blklen, mf;
	Block *nb;
	ushort fragoff, frag;

	port->out++;
	epkt = (Etherpkt*)bp->rp;
	if(port->type != Ttun || !fragment(epkt, BLEN(bp))) {
		if(!waserror()){
			if(VID(tag) != port->pvid)
				bp = tagpkt(bp, tag);

			/* don't generate small packets */
			if(BLEN(bp) < ETHERMINTU)
				bp = adjustblock(bp, ETHERMINTU);
			devtab[port->data[1]->type]->bwrite(port->data[1], bp, 0);
			poperror();
		}
		return;
	}
	port->outfrag++;
	if(waserror()){
		freeb(bp);	
		return;
	}

	seglen = (TunnelMtu - ETHERHDRSIZE - IP4HDR) & ~7;
	eh = (Ip4hdr*)(epkt->data);
	len = nhgets(eh->length);
	frag = nhgets(eh->frag);
	mf = frag & IP_MF;
	frag <<= 3;
	dlen = len - IP4HDR;
	lid = nhgets(eh->id);
	bp->rp += ETHERHDRSIZE+IP4HDR;
	
	if(0)
		print("seglen=%d, dlen=%d, mf=%x, frag=%d\n",
			seglen, dlen, mf, frag);
	for(fragoff = 0; fragoff < dlen; fragoff += seglen) {
		nb = allocb(ETHERHDRSIZE+IP4HDR+seglen);
		
		feh = (Ip4hdr*)(nb->wp+ETHERHDRSIZE);

		memmove(nb->wp, epkt, ETHERHDRSIZE+IP4HDR);
		nb->wp += ETHERHDRSIZE+IP4HDR;

		if((fragoff + seglen) >= dlen) {
			seglen = dlen - fragoff;
			hnputs(feh->frag, (frag+fragoff)>>3 | mf);
		}
		else	
			hnputs(feh->frag, (frag+fragoff>>3) | IP_MF);

		hnputs(feh->length, seglen + IP4HDR);
		hnputs(feh->id, lid);

		if(seglen){
			blklen = BLEN(bp);
			if(seglen < blklen)
				blklen = seglen;
			memmove(nb->wp, bp->rp, blklen);
			nb->wp += blklen;
			bp->rp += blklen;
		}

		feh->cksum[0] = 0;
		feh->cksum[1] = 0;
		hnputs(feh->cksum, ipcsum(&feh->vihl));
	
		if(VID(tag) != port->pvid)
			nb = tagpkt(nb, tag);

		/* don't generate small packets */
		if(BLEN(nb) < ETHERMINTU)
			nb = adjustblock(nb, ETHERMINTU);
		devtab[port->data[1]->type]->bwrite(port->data[1], nb, 0);
	}
	poperror();
	freeb(bp);	
}

// hold b lock
static void
portfree(Port *port)
{
	if(decref(port) != 0)
		return;

	if(port->data[0])
		cclose(port->data[0]);
	if(port->data[1])
		cclose(port->data[1]);
	memset(port, 0, sizeof(Port));
	free(port);
}

Dev bridgedevtab = {
	'B',
	"bridge",

	devreset,
	bridgeinit,
	devshutdown,
	bridgeattach,
	bridgewalk,
	bridgestat,
	bridgeopen,
	devcreate,
	bridgeclose,
	bridgeread,
	devbread,
	bridgewrite,
	devbwrite,
	devremove,
	devwstat,
};
