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

enum {
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

	Maxbridge=	16,
	Maxport=	128,		/* power of 2 */
	CacheHash=	257,		/* prime */
	CacheLook=	5,		/* how many cache entries to examine */
	CacheSize=	(CacheHash+CacheLook-1),
	CacheTimeout=	5*60,		/* timeout for cache entry in seconds */
	MaxMTU=		IP_MAX,		/* allow for jumbo frames and large UDP */

	TcpMssMax = 1300,		/* max desirable Tcp MSS value */
	TunnelMtu = 1400,
};

static Dirtab bridgedirtab[] = {
	"ctl",		{Qbctl},	0,	0666,
	"stats",	{Qstats},	0,	0444,
	"cache",	{Qcache},	0,	0444,
	"log",		{Qlog},		0,	0666,
};

static Dirtab portdirtab[] = {
	"ctl",		{Qpctl},	0,	0666,
	"local",	{Qlocal},	0,	0444,
	"status",	{Qstatus},	0,	0444,
};

enum {
	Logcache=	(1<<0),
	Logmcast=	(1<<1),
};

static Logflag logflags[] = {
	{ "cache",	Logcache, },
	{ "multicast",	Logmcast, },
	{ nil,		0, },
};

enum {
	Tether,
	Ttun,
};

static char *typstr[] = {
	"ether",
	"tunnel",
};

static Dirtab	*dirtab[MaxQ];

Dev bridgedevtab;

#define TYPE(x) 	(((ulong)(x).path) & 0xff)
#define PORT(x) 	((((ulong)(x).path) >> 8)&(Maxport-1))
#define QID(x, y) 	(((x)<<8) | (y))

#define VID(tag)	((tag) & 0xFFF)

struct Centry
{
	ushort	vid;
	uchar	d[Eaddrlen];
	int	portid;
	long	expire;		/* entry expires this many seconds after bootime */
	long	src;
	long	dst;
};

struct Bridge
{
	RWlock;

	ulong	dev;		/* bridgetab[dev] */

	ulong	hit;
	ulong	miss;
	ulong	drop;
	ulong	copy;

	long	delay0;		/* constant microsecond delay per packet */
	long	delayn;		/* microsecond delay per byte */
	int	tcpmss;		/* modify tcpmss value */

	int	nport;
	Port	*port[Maxport];

	Centry	cache[CacheSize];

	Log;
};

struct Port
{
	int	id;		/* bridge->port[id] */

	Bridge	*bridge;

	Chan	*data[2];	/* channel to data */

	Proc	*readp;		/* read proc */
	
	/* the following uniquely identifies the port */
	int	type;
	char	name[KNAMELEN];
	
	/* owner hash - avoids bind/unbind races */
	ulong	ownhash;

	/* various stats */
	ulong	in;		/* number of packets read */
	ulong	inmulti;	/* multicast or broadcast */
	ulong	inunknown;	/* unknown address */
	ulong	out;		/* number of packets read */
	ulong	outmulti;	/* multicast or broadcast */
	ulong	outunknown;	/* unknown address */
	ulong	outfrag;	/* fragmented the packet */

	/* 802.1q vlan configuration */
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

static Bridge *bridgetab[Maxbridge];

static int bridgegen(Chan *c, char*, Dirtab*, int, int s, Dir *dp);

static void portbind(Bridge *b, int argc, char *argv[]);
static void portunbind(Bridge *b, int argc, char *argv[]);
static void portvlan(Bridge *b, int argc, char *argv[]);

static void etherread(void *a);
static char *cachedump(Bridge *b);
static void cacheflushport(Bridge *b, int portid);
static void etherwrite(Port *port, Block *bp, ushort tag);

static void
clearmember(Port *port)
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

	/* setup dirtab with non directory entries */
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

	if(bridgetab[dev] == nil){
		static Lock lk;
		Bridge *b;

		/* only hostowner should create new bridges */
		if(!iseve())
			error(Enoattach);

		b = malloc(sizeof(Bridge));
		if(b == nil)
			error(Enomem);
		b->dev = dev;
		lock(&lk);
		if(bridgetab[dev] == nil){
			bridgetab[dev] = b;
			unlock(&lk);
		} else {
			unlock(&lk);
			free(b);
		}
	}

	c = devattach(bridgedevtab.dc, spec);
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
	Bridge *b = bridgetab[c->dev];

	switch(TYPE(c->qid)) {
	default:
		break;
	case Qlog:
		logopen(b);
		break;
	case Qcache:
		rlock(b);
		c->aux = cachedump(b);
		runlock(b);
		if(c->aux == nil)
			error(Enomem);
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
	Bridge *b = bridgetab[c->dev];

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

	s = seprint(s, e, "%ud", port->pvid);
	if(port->prio)
		s = seprint(s, e, "#%ud", port->prio>>12);
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
	Bridge *b = bridgetab[c->dev];
	char buf[512];
	Port *port;
	int i;
	ulong ingood, outgood;

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
		rlock(b);
		if(waserror()){
			runlock(b);
			nexterror();
		}
		port = b->port[PORT(c->qid)];
		if(port == nil)
			strcpy(buf, "unbound\n");
		else {
			i = 0;
			i += snprint(buf+i, sizeof(buf)-i, "%s %s: ", typstr[port->type], port->name);
			i += snprint(buf+i, sizeof(buf)-i, "vlan=");
			i += getvlancfg(port, buf+i, sizeof(buf)-i);
			i += snprint(buf+i, sizeof(buf)-i, " ");

			ingood = port->in - port->inmulti - port->inunknown;
			outgood = port->out - port->outmulti - port->outunknown;
			i += snprint(buf+i, sizeof(buf)-i,
				"in=%lud(%lud:%lud:%lud) out=%lud(%lud:%lud:%lud:%lud)\n",
				port->in, ingood, port->inmulti, port->inunknown,
				port->out, outgood, port->outmulti,
				port->outunknown, port->outfrag);
			USED(i);
		}
		poperror();
		runlock(b);
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
		snprint(buf, sizeof(buf), "hit=%uld miss=%uld drop=%uld copy=%uld\n",
			b->hit, b->miss, b->drop, b->copy);
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
	Bridge *b = bridgetab[c->dev];
	Cmdbuf *cb;
	char *arg0, *p;
	
	USED(off);
	switch(TYPE(c->qid)) {
	default:
		error(Eperm);
	case Qbctl:
		cb = parsecmd(a, n);
		if(waserror()){
			free(cb);
			nexterror();
		}
		if(cb->nf == 0)
			error("short write");
		arg0 = cb->f[0];
		if(strcmp(arg0, "bind") == 0)
			portbind(b, cb->nf-1, cb->f+1);
		else {
			wlock(b);
			if(waserror()) {
				wunlock(b);
				nexterror();
			}
			if(strcmp(arg0, "unbind") == 0) {
				portunbind(b, cb->nf-1, cb->f+1);
			} else if(strcmp(arg0, "vlan") == 0) {
				portvlan(b, cb->nf-1, cb->f+1);
			} else if(strcmp(arg0, "cacheflush") == 0) {
				cacheflushport(b, -1);
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
			wunlock(b);
			poperror();
		}
		free(cb);
		poperror();
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
	Bridge *b = bridgetab[c->dev];
	int type = TYPE(c->qid);
	Dirtab *dt;
	Qid qid;

	if(s  == DEVDOTDOT){
		switch(TYPE(c->qid)){
		case Qtopdir:
		case Qbridgedir:
			snprint(up->genbuf, sizeof(up->genbuf), "#%C%lud", bridgedevtab.dc, c->dev);
			mkqid(&qid, Qtopdir, 0, QTDIR);
			devdir(c, qid, up->genbuf, 0, eve, 0555, dp);
			break;
		case Qportdir:
			snprint(up->genbuf, sizeof(up->genbuf), "bridge%lud", c->dev);
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
		snprint(up->genbuf, sizeof(up->genbuf), "bridge%lud", c->dev);
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
	} else if(*x == '-') {
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

/*
 *  set the vlan configuration of a port.
 *  first number is the pvid (port vlan id)
 *  followed by zero or more other vlan members.
 *  members can be specified as comma separated ranges:
 *    -10,13,50-60,1000- => [1..10],13,[50-60],[1000-4094]
 */
static void
setvlancfg(Port *port, char *cfg)
{
	int i, j;

	clearmember(port);
	port->pvid = strtol(cfg, &cfg, 10);
	if(port->pvid >= 0xFFF)
		port->pvid = 0;
	if(*cfg == '#'){
		cfg++;
		port->prio = strtoul(cfg, &cfg, 10)<<12;
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

static void
portfree(Port *port)
{
	if(port->data[0])
		cclose(port->data[0]);
	if(port->data[1])
		cclose(port->data[1]);
	free(port);
}

static void
portbind(Bridge *b, int argc, char *argv[])
{
	Port *port;
	Chan *ctl;
	int type, i, n;
	ulong ownhash;
	char *dev, *dev2, *vlan;
	char buf[100], name[KNAMELEN], path[8*KNAMELEN];
	static char usage[] = "usage: bind type name ownhash dev [dev2] [pvid[,vlans...]]";

	dev2 = nil;
	vlan = "1";	/* default vlan configuration */

	if(argc < 4)
		error(usage);
	for(type = 0; type < nelem(typstr); type++)
		if(strcmp(argv[0], typstr[type]) == 0)
			break;

	memset(name, 0, KNAMELEN);
	strncpy(name, argv[1], KNAMELEN);
	name[KNAMELEN-1] = 0;

	ownhash = strtoul(argv[2], nil, 10);

	dev = argv[3];

	switch(type){
	default:
		error(usage);
	case Tether:
		if(argc > 4)
			vlan = argv[4];
		break;
	case Ttun:
		if(argc < 5)
			error(usage);
		if(argc > 5)
			vlan = argv[5];
		dev2 = argv[4];
		break;
	}

	port = malloc(sizeof(Port));
	if(port == nil)
		error(Enomem);
	port->id = -1;
	port->type = type;
	memmove(port->name, name, KNAMELEN);
	port->ownhash = ownhash;
	port->readp = (void*)-1;
	port->data[0] = nil;
	port->data[1] = nil;
	if(waserror()) {
		portfree(port);
		nexterror();
	}

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

		/* get directory name */
		n = devtab[ctl->type]->read(ctl, buf, sizeof(buf)-1, 0);
		buf[n] = 0;
		snprint(path, sizeof(path), "%s/%lud/data", dev, strtoul(buf, 0, 0));

		/* setup connection to be promiscuous */
		snprint(buf, sizeof(buf), "connect -1");
		devtab[ctl->type]->write(ctl, buf, strlen(buf), 0);
		snprint(buf, sizeof(buf), "nonblocking");
		devtab[ctl->type]->write(ctl, buf, strlen(buf), 0);
		snprint(buf, sizeof(buf), "promiscuous");
		devtab[ctl->type]->write(ctl, buf, strlen(buf), 0);
		snprint(buf, sizeof(buf), "bridge");
		devtab[ctl->type]->write(ctl, buf, strlen(buf), 0);

		/* open data port */
		port->data[0] = namec(path, Aopen, ORDWR, 0);
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

	/* try to insert port into the bridge */
	wlock(b);
	if(waserror()){
		wunlock(b);
		nexterror();
	}
	for(i=0; i<b->nport; i++) {
		if(b->port[i] == nil)
			continue;
		if(b->port[i]->type == type && memcmp(b->port[i]->name, name, KNAMELEN) == 0)
			error("port in use");
	}
	for(i=0; i<Maxport; i++)
		if(b->port[i] == nil)
			break;
	if(i >= Maxport)
		error("no more ports");

	/* committed to binding port */
	poperror();
	poperror();

	port->id = i;
	port->bridge = b;
	b->port[i] = port;
	if(i >= b->nport)
		b->nport = i+1;
	wunlock(b);

	/* start the reader */
	snprint(buf, sizeof(buf), "#%C%lud/bridge%lud/%d:%s",
		bridgedevtab.dc, b->dev, b->dev, i, dev);
	kproc(buf, etherread, port);
}

static Port*
getport(Bridge *b, int argc, char **argv)
{
	static char usage[] = "usage: ... type name [ownhash]";
	int type, i;
	char name[KNAMELEN];
	ulong ownhash;
	Port *port;

	if(argc < 2)
		error(usage);

	for(type = 0; type < nelem(typstr); type++)
		if(strcmp(argv[0], typstr[type]) == 0)
			break;
	if(type >= nelem(typstr))
		error(usage);

	memset(name, 0, KNAMELEN);
	strncpy(name, argv[1], KNAMELEN);
	name[KNAMELEN-1] = 0;

	if(argc == 3)
		ownhash = strtoul(argv[2], nil, 10);
	else
		ownhash = 0;

	port = nil;
	for(i=0; i<b->nport; i++) {
		port = b->port[i];
		if(port != nil && port->type == type
		&& memcmp(port->name, name, KNAMELEN) == 0)
			break;
	}
	if(i >= b->nport)
		error("port not found");
	if(ownhash != 0 && port->ownhash != 0 && ownhash != port->ownhash)
		error("bad owner hash");
	return port;
}

static void
unbindport(Bridge *b, Port *port)
{
	if(port->bridge != b)
		return;
	port->bridge = nil;
	b->port[port->id] = nil;
	cacheflushport(b, port->id);
}

static void
portunbind(Bridge *b, int argc, char *argv[])
{
	Port *port = getport(b, argc, argv);

	/* wait for reader to startup */
	while(port->readp == (void*)-1)
		tsleep(&up->sleep, return0, 0, 300);

	/* stop reader */
	postnote(port->readp, 1, "unbind", 0);

	unbindport(b, port);
}

static void
portvlan(Bridge *b, int argc, char *argv[])
{
	Port *port = getport(b, argc-1, argv+1);

	cacheflushport(b, port->id);
	setvlancfg(port, argv[0]);
}

static Centry *
cacheent(Bridge *b, uchar d[Eaddrlen], ushort vid)
{
	uint h = (uint)vid*587;
	int i;

	for(i=0; i<Eaddrlen; i++) {
		h *= 7;
		h += d[i];
	}
	return &b->cache[h % CacheHash];
}

static Centry *
cachelookup(Bridge *b, uchar d[Eaddrlen], ushort vid)
{
	Centry *p, *e;
	long sec;

	for(p = cacheent(b, d, vid), e = p+CacheLook; p < e; p++) {
		if(p->vid == vid && memcmp(d, p->d, Eaddrlen) == 0) {
			sec = TK2SEC(MACHP(0)->ticks);
			if(sec >= p->expire)
				break;
			p->expire = sec + CacheTimeout;
			return p;
		}
	}
	return nil;
}

static void
cacheenter(Bridge *b, uchar d[Eaddrlen], int portid, ushort vid)
{
	Centry *p, *e, *pp;
	long sec;

	pp = cacheent(b, d, vid);
	sec = pp->expire;
	for(p = pp, e = pp+CacheLook; p < e; p++) {
		if(p->vid == vid && memcmp(d, p->d, Eaddrlen) == 0){
			if(p->portid != portid)
				log(b, Logcache, "NIC changed port: %E %ud %d->%d\n",
					d, vid, p->portid, portid);
			pp = p;
			goto Update;
		}
		if(p->expire < sec){
			pp = p;
			sec = p->expire;
		}
	}
	if(pp->expire != 0)
		log(b, Logcache, "bumping from cache: %E %ud %d\n", pp->d, pp->vid, pp->portid);
Update:
	log(b, Logcache, "adding to cache: %E %ud %d\n", d, vid, portid);
	sec = TK2SEC(MACHP(0)->ticks);
	pp->expire = sec + CacheTimeout;
	pp->vid = vid;
	pp->portid = portid;
	pp->src = 1;
	pp->dst = 0;
	memmove(pp->d, d, Eaddrlen);
}

static void
cacheflushport(Bridge *b, int portid)
{
	Centry *p, *e;

	log(b, Logcache, "cache flush: %d\n", portid);

	e = &b->cache[CacheSize];
	for(p = b->cache; p < e; p++){
		if(p->expire == 0)
			continue;
		if(portid < 0 || p->portid == portid){
			log(b, Logcache, "deleting from cache: %E %ud %d\n", p->d, p->vid, p->portid);
			memset(p, 0, sizeof(Centry));
		}
	}
}

static char *
cachedump(Bridge *b)
{
	long off, sec, n;
	char *buf, *p, *ep;
	Centry *c, *e;

	e = &b->cache[CacheSize];
	n = 0;
	for(c = b->cache; c < e; c++)
		if(c->expire != 0)
			n++;
	n *= 13+5+3+11+11+11+2;
	buf = malloc(++n);
	if(buf == nil)
		return nil;
	p = buf;
	ep = buf + n;

	sec = TK2SEC(MACHP(0)->ticks);
	off = seconds() - sec;

	for(c = b->cache; c < e; c++){
		if(c->expire == 0)
			continue;	
		p = seprint(p, ep, "%E %4ud %2d %10ld %10ld %10ld %c\n",
			c->d, c->vid, c->portid,
			c->src, c->dst, c->expire+off,
			(sec < c->expire)?'v':'e');
	}
	*p = '\0';

	return buf;
}

static void
ethermultiwrite(Bridge *b, Block *bp, int portid, ushort tag)
{
	Port *oport;
	Etherpkt *ep;
	int i, mcast;
	ushort vid;

	vid = VID(tag);
	ep = (Etherpkt*)bp->rp;
	mcast = ep->d[0] & 1;		/* multicast bit of ethernet address */
	oport = nil;
	for(i=0; i<b->nport; i++) {
		if(i == portid || b->port[i] == nil || !ismember(b->port[i], vid))
			continue;
		/*
		 * we need to forward multicast packets for ipv6,
		 * so always do it.
		 */
		if(mcast)
			b->port[i]->outmulti++;
		else
			b->port[i]->outunknown++;

		/* delay one so that the last write does not copy */
		if(oport != nil) {
			b->copy++;
			etherwrite(oport, copyblock(bp, BLEN(bp)), tag);
		}
		oport = b->port[i];
	}

	/* last write free block */
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

	/* ignore non-ip packets */
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

	/* MSS can only appear in SYN packet */
	if(!(tcphdr->flag[1] & SYN))
		return;
	hl = (tcphdr->flag[0] & 0xf0)>>2;
	if(n < hl)
		return;

	/* check for MSS option */
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

	/* fix checksum */
	cksum = nhgets(tcphdr->cksum);
	if(optr-(uchar*)tcphdr & 1) {
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
	long n;
	ushort type, tag;

	port->readp = up;
	if(waserror()) {
		print("%s: %s\n", up->text, up->errstr);
		goto Exit;
	}
	while(port->bridge == b) {
		bp = devtab[port->data[0]->type]->bread(port->data[0], MaxMTU, 0);
		if(bp == nil)
			break;

		n = BLEN(bp);

		/* delay packets to simulate a slow link */
		if(b->delay0 != 0 || b->delayn != 0){
			long md = b->delay0 + b->delayn * n;
			if(md > 0)
				microdelay(md);
		}

		rlock(b);
		if(port->bridge != b)
			goto Drop;

		port->in++;

		/* short packet? */
		if(n < ETHERHDRSIZE)
			goto Drop;

		/* untag vlan packets */
		ep = (Etherpkt*)bp->rp;
		type = ep->type[0]<<8|ep->type[1];
		if(type != 0x8100) {
			/* untagged packet is native vlan */
			tag = port->pvid;
			if(tag == 0)
				goto Drop;
			tag |= port->prio;
		} else {
			/* short packet? */
			n -= 4;
			if(n < ETHERHDRSIZE)
				goto Drop;

			tag = untagpkt(bp);
			if(!ismember(port, VID(tag))) {
				/* wrong vlan id? */
				if(VID(tag) != 0 || port->pvid == 0)
					goto Drop;
				/* priority tagged packet on native vlan */
				tag |= port->pvid;
			}
			ep = (Etherpkt*)bp->rp;
			type = ep->type[0]<<8|ep->type[1];
		}
	
		if(b->tcpmss)
			tcpmsshack(ep, n);

		/* learn source MAC addresses */
		if((ep->s[0] & 1) == 0) {
			ce = cachelookup(b, ep->s, VID(tag));
			if(ce != nil && ce->portid == port->id)
				ce->src++;
			else {
				runlock(b), wlock(b);
				if(port->bridge == b)
					cacheenter(b, ep->s, port->id, VID(tag));
				wunlock(b), rlock(b);
				if(port->bridge != b)
					goto Drop;
			}
		}

		/* forward to destination port(s) */
		if(ep->d[0] & 1) {
			port->inmulti++;
			log(b, Logmcast, "multicast: port=%d tag=%#.4ux src=%E dst=%E type=%#.4ux\n",
				port->id, tag, ep->s, ep->d, type);
			ethermultiwrite(b, bp, port->id, tag);
		} else {
			ce = cachelookup(b, ep->d, VID(tag));
			if(ce == nil) {
				port->inunknown++;
				b->miss++;
				ethermultiwrite(b, bp, port->id, tag);
			}else if(ce->portid != port->id){
				ce->dst++;
				b->hit++;
				etherwrite(b->port[ce->portid], bp, tag);
			} else{
Drop:
				b->drop++;
				runlock(b);
				freeb(bp);
				continue;
			}
		}
		runlock(b);
	}
Exit:
	port->readp = nil;

	wlock(b);
	unbindport(b, port);
	wunlock(b);

	print("%s: unbound\n", up->text);

	portfree(port);
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
etherwrite1(Port *port, Block *bp, ushort tag)
{
	if(VID(tag) != port->pvid)
		bp = tagpkt(bp, tag);

	/* don't generate small packets */
	if(BLEN(bp) < ETHERMINTU)
		bp = adjustblock(bp, ETHERMINTU);

	devtab[port->data[1]->type]->bwrite(port->data[1], bp, 0);
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
			etherwrite1(port, bp, tag);
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

		etherwrite1(port, nb, tag);
	}
	poperror();
	freeb(bp);	
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
