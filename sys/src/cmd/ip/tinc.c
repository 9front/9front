#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>
#include <auth.h>
#include <ip.h>

enum {
	OptIndirect	= 1,
	OptTcpOnly	= 3,
	OptPmtuDiscov	= 4,
	OptClampMss	= 8,

	MaxWeight	= 0x7fffffff,
	AESkeylen	= 256/8,
	MAClen		= 4,

	Eaddrlen	= 6,
	EtherType	= 2*Eaddrlen,
	EtherHdr	= EtherType+2,

	Ip4Hdr		= 20,
	Ip6Hdr		= 40,
	UdpHdr		= 8,
	TcpHdr		= 20,

	/* worst case: UDPv6 over 6in4 over PPPoE */
	DefPMTU		= 1500-8-Ip4Hdr-Ip6Hdr-UdpHdr-4-AESbsize-MAClen,

	MaxPacket	= 4096,

	/* messages */
	ID		= 0,
	META_KEY	= 1,
	CHALLENGE	= 2,
	CHAL_REPLY	= 3,
	ACK		= 4,
	PING		= 8,
	PONG		= 9,
	ADD_SUBNET	= 10,
	DEL_SUBNET	= 11,
	ADD_EDGE	= 12,
	DEL_EDGE	= 13,
	KEY_CHANGED	= 14,
	REQ_KEY		= 15,
	ANS_KEY		= 16,
	TCP_PACKET	= 17,

	/* openssl crap */
	EVP_AES256CBC	= 427,
	EVP_AES256CFB	= 429,
	EVP_SHA256	= 672,
};

typedef struct Snet Snet;
typedef struct Edge Edge;
typedef struct Ciph Ciph;
typedef struct Host Host;
typedef struct Conn Conn;

struct Snet
{
	Host	*owner;

	Snet	*next;	/* next subnet on owner */
	uchar	ip[IPaddrlen];
	uchar	mask[IPaddrlen];
	int	prefixlen;
	int	weight;
	char	reported;
	char	deleted;
};

struct Edge
{
	Host	*src;
	Host	*dst;
	Edge	*next;	/* next edge on src */
	Edge	*rev;	/* reverse direction edge */

	uchar	ip[IPaddrlen];
	int	port;

	int	options;
	int	weight;
	char	reported;
	char	deleted;
};

struct Ciph
{
	void	(*crypt)(uchar*, int, AESstate*);
	uint	seq;
	uchar	key[AESkeylen+AESbsize];
	AESstate cs[1];
	Lock;
};

struct Host
{
	Host	*next;
	char	*name;
	char	*addr;

	Conn	*conn;
	Host	*from;
	Edge	*link;

	Snet	*snet;

	uchar	ip[IPaddrlen];
	int	port;

	int	connected;
	int	options;
	int	pmtu;
	int	udpfd;

	uvlong	ooo;	/* out of order replay window */
	Ciph	cin[1];
	Ciph	cout[1];

	RSApub	*rsapub;
};

struct Conn
{
	Host	*host;
	Edge	*edge;

	int	fd;
	int	port;
	uchar	ip[IPaddrlen];

	vlong	pingtime;

	QLock	sendlk;

	Ciph	cin[1];
	Ciph	cout[1];

	char	*rp;
	char	*wp;
	char	buf[MaxPacket+16];
};

QLock	hostslk;
Host	*hosts;

Edge	**edges;
int	nedges;
Snet	**snet;
int	nsnet;

int	debug;
int	maxprocs = 100;

char	*myname = nil;
Host	*myhost = nil;

Conn	*bcast = (void*)-1;
Conn	*lconn = nil;
RWLock	netlk;

char	*outside = "/net";
char	*inside = "/net";
char	device[128];
int	ipcfd = -1;
int	ipdfd = -1;
uchar	localip[IPaddrlen];
uchar	localmask[IPaddrlen];
int	rcfd = -1;

void	deledge(Edge*);
void	delsubnet(Snet*);
void	netrecalc(void);

int	consend(Conn *c, char *fmt, ...);
#pragma varargck argpos consend 2
int	sendudp(Host *h, int fd, uchar *p, int n);
void	routepkt(Host *s, uchar *p, int n);
void	needkey(Host *from);
void	clearkey(Host *from);

void*
emalloc(ulong len)
{
	void *v = malloc(len);
	if(v == nil)
		sysfatal("malloc: %r");
	setmalloctag(v, getcallerpc(&len));
	memset(v, 0, len);
	return v;
}
void*
erealloc(void *v, ulong len)
{
	if((v = realloc(v, len)) == nil && len != 0)
		sysfatal("realloc: %r");
	setrealloctag(v, getcallerpc(&v));
	return v;
}
char*
estrdup(char *s)
{
	if((s = strdup(s)) == nil)
		sysfatal("strdup: %r");
	setmalloctag(s, getcallerpc(&s));
	return s;
}

char*
fd2dir(int fd, char *dir, int len)
{
	char *p;

	*dir = 0;
	if(fd2path(fd, dir, len) < 0)
		return nil;
	p = strrchr(dir, '/');
	if(p == nil || p == dir)
		return nil;
	*p = 0;
	return dir;
}

int
dir2ipport(char *dir, uchar ip[IPaddrlen])
{
	NetConnInfo *nci;
	int port = -1;

	if((nci = getnetconninfo(dir, -1)) == nil)
		return -1;
	if(parseip(ip, nci->rsys) != -1)
		port = atoi(nci->rserv);
	freenetconninfo(nci);
	return port;
}

void
hangupfd(int fd)
{
	char buf[128];

	if(fd < 0 || fd2dir(fd, buf, sizeof(buf)-5) == nil)
		return;
	strcat(buf, "/ctl");
	if((fd = open(buf, OWRITE)) >= 0){
		hangup(fd);
		close(fd);
	}
}

void
netlock(Conn *c)
{
	if(c != nil) {
		wlock(&netlk);
		assert(lconn == nil);
		lconn = c;
	} else {
		rlock(&netlk);
		assert(lconn == nil);
	}
}
void
netunlock(Conn *c)
{
	if(c != nil){
		assert(c == lconn);
		netrecalc();
		lconn = nil;
		wunlock(&netlk);
	}else {
		assert(lconn == nil);
		runlock(&netlk);
	}
}

int
edgecmp(Edge *a, Edge *b)
{
	int c;

	if((c = a->deleted - b->deleted) != 0)
		return c;
	return a->weight - b->weight;
}
int
edgepcmp(void *a, void *b)
{
	return edgecmp(*(Edge**)a, *(Edge**)b);
}

int
subnetcmp(Snet *a, Snet *b)
{
	int c;

	if((c = a->deleted - b->deleted) != 0)
		return c;
	if((c = ipcmp(b->mask, a->mask)) != 0)
		return c;
	if((c = ipcmp(a->ip, b->ip)) != 0)
		return c;
	return a->weight - b->weight;
}
int
subnetpcmp(void *a, void *b)
{
	return subnetcmp(*(Snet**)a, *(Snet**)b);
}

Snet*
lookupnet(uchar ip[IPaddrlen])
{
	int i;
	Snet *t;
	uchar x[IPaddrlen];

	for(i=0; i<nsnet; i++){
		t = snet[i];
		maskip(ip, t->mask, x);
		if(ipcmp(x, t->ip) == 0)
			return t;
	}
	return nil;
}

void
reportsubnet(Conn *c, Snet *t)
{
	if(c == nil || !(t->deleted || t->owner->connected))
		return;
	if(c == bcast){
		Edge *x;

		if(t->deleted != t->reported)
			return;
		t->reported = !t->deleted;
		for(x = myhost->link; x != nil; x = x->next)
			if(x->dst->conn != lconn && x->dst->from == myhost)
				reportsubnet(x->dst->conn, t);
		return;
	}
	if(t->owner == c->host)
		return;
	if(t->deleted)
		consend(c, "%d %x %s %I/%d#%d", DEL_SUBNET, rand(),
			t->owner->name, t->ip, t->prefixlen, t->weight);
	else
		consend(c, "%d %x %s %I/%d#%d", ADD_SUBNET, rand(), t->owner->name,
			t->ip, t->prefixlen, t->weight);
}
void
reportedge(Conn *c, Edge *e)
{
	if(c == nil || !(e->deleted || e->src->connected && e->dst->connected))
		return;
	if(c == bcast){
		Edge *x;

		if(e->deleted != e->reported)
			return;
		e->reported = !e->deleted;
		for(x = myhost->link; x != nil; x = x->next)
			if(x->dst->conn != lconn && x->dst->from == myhost)
				reportedge(x->dst->conn, e);
		return;
	}
	if(e->src == c->host)
		return;
	if(e->deleted){
		if(e->dst == c->host)
			return;
		consend(c, "%d %x %s %s", DEL_EDGE, rand(),
			e->src->name, e->dst->name);
	} else
		consend(c, "%d %x %s %s %I %d %x %d", ADD_EDGE, rand(),
			e->src->name, e->dst->name,
			e->ip, e->port, e->options, e->weight);
}

void
netrecalc(void)
{
	static int hostup = 0;
	Host *h;
	Edge *e;
	Snet *t;
	int i;

	if(myhost == nil)
		return;

	qsort(edges, nedges, sizeof(edges[0]), edgepcmp);
	while(nedges > 0 && edges[nedges-1]->deleted){
		reportedge(bcast, edges[--nedges]);
		free(edges[nedges]);
		edges[nedges] = nil;
	}
	for(h = hosts; h != nil; h = h->next) h->from = nil;

	myhost->from = myhost;
	myhost->connected = 1;

Loop:
	for(i=0; i<nedges; i++){
		e = edges[i];
		if(e->src->from == nil || (h = e->dst)->from != nil)
			continue;
		memmove(h->ip, e->ip, IPaddrlen);
		h->port = e->port;
		h->options = e->options;
		h->from = e->src;
		if(h->connected == 0){
			h->connected = 1;
			for(t = h->snet; t != nil; t = t->next)
				t->reported = 0;
			e->reported = 0;

			fprint(rcfd, "NAME=%s NODE=%s DEVICE=%s INTERFACE=%I"
				" REMOTENET=%s REMOTEADDRESS=%I REMOTEPORT=%d"
				" ./hosts/%s-up\n",
				myname, h->name, device, localip,
				outside, h->ip, h->port,
				h->name);
		}
		goto Loop;
	}

	for(h = hosts; h != nil; h = h->next){
		if(h->from == nil && h->connected){
			h->connected = 0;
			clearkey(h);

			fprint(rcfd, "NAME=%s NODE=%s DEVICE=%s INTERFACE=%I"
				" REMOTENET=%s REMOTEADDRESS=%I REMOTEPORT=%d"
				" ./hosts/%s-down\n",
				myname, h->name, device, localip,
				outside, h->ip, h->port,
				h->name);

			while(h->link != nil) {
				deledge(h->link->rev);
				deledge(h->link);
			}
			while(h->snet != nil) delsubnet(h->snet);
		}
	}

	i = myhost->link != nil;
	if(i != hostup){
		hostup = i;

		fprint(rcfd, "NAME=%s NODE=%s DEVICE=%s INTERFACE=%I"
			" REMOTENET=%s REMOTEADDRESS=%I REMOTEPORT=%d"
			" ./host-%s\n",
			myname, myhost->name, device, localip,
			outside, myhost->ip, myhost->port,
			hostup ? "up" : "down");
	}


	qsort(snet, nsnet, sizeof(snet[0]), subnetpcmp);
	for(i = nsnet-1; i >= 0; i--){
		t = snet[i];

		if(t->owner != myhost && (t->deleted || t->reported == 0))
			fprint(rcfd, "NAME=%s NODE=%s DEVICE=%s INTERFACE=%I"
				" REMOTENET=%s REMOTEADDRESS=%I REMOTEPORT=%d"
				" SUBNET=(%I %M) WEIGHT=%d"
				" ./subnet-%s\n",
				myname, t->owner->name, device, localip,
				outside, t->owner->ip, t->owner->port,
				t->ip, t->mask, t->weight,
				t->deleted ? "down" : "up");

		reportsubnet(bcast, t);
		if(t->deleted){
			assert(i == nsnet-1);
			snet[i] = nil;
			nsnet = i;
			free(t);
		}
	}

	qsort(edges, nedges, sizeof(edges[0]), edgepcmp);
	for(i = nedges-1; i >= 0; i--){
		reportedge(bcast, edges[i]);
		if(edges[i]->deleted){
			assert(i == nedges-1);
			nedges = i;
			free(edges[i]);
			edges[i] = nil;
		}
	}
}

Snet*
getsubnet(Host *h, char *s, int new)
{
	uchar ip[IPaddrlen], mask[IPaddrlen];
	int weight, prefixlen;
	Snet *t;

	if(parseipandmask(ip, mask, s, strchr(s, '/')) == -1)
		return nil;

	for(prefixlen = 0; prefixlen < 128; prefixlen++)
		if((mask[prefixlen/8] & (0x80 >> (prefixlen%8))) == 0)
			break;
	if(isv4(ip))
		prefixlen -= 96;

	maskip(ip, mask, ip);

	weight = 10;
	if((s = strchr(s, '#')) != nil)
		weight = atoi(s+1);

	for(t = h->snet; t != nil; t = t->next)
		if(ipcmp(t->ip, ip) == 0 && ipcmp(t->mask, mask) == 0){
			if(new)
				t->weight = weight;
			return t;
		}

	if(!new)
		return nil;

	t = emalloc(sizeof(Snet));
	ipmove(t->ip, ip);
	ipmove(t->mask, mask);
	t->prefixlen = prefixlen;
	t->weight = weight;
	t->owner = h;
	t->next = h->snet;
	h->snet = t;
	if((nsnet % 16) == 0)
		snet = erealloc(snet, sizeof(snet[0])*(nsnet+16));
	snet[nsnet++] = t;
	return t;
}

void
delsubnet(Snet *t)
{
	Snet **tp;

	if(t == nil || t->deleted)
		return;
	for(tp = &t->owner->snet; *tp != nil; tp = &(*tp)->next){
		if(*tp == t){
			*tp = t->next;
			break;
		}
	}
	t->next = nil;
	t->deleted = 1;
}

Edge*
getedge(Host *src, Host *dst, int new)
{
	Edge *e;

	for(e = src->link; e != nil; e = e->next)
		if(e->dst == dst)
			return e;
	if(!new)
		return nil;
	e = emalloc(sizeof(Edge));
	e->weight = MaxWeight;
	e->src = src;
	e->dst = dst;
	e->next = src->link;
	src->link = e;
	if((e->rev = getedge(dst, src, 0)) != nil)
		e->rev->rev = e;
	if((nedges % 16) == 0)
		edges = erealloc(edges, sizeof(edges[0])*(nedges+16));
	edges[nedges++] = e;
	return e;
}

void
deledge(Edge *e)
{
	Edge **ep;

	if(e == nil || e->deleted)
		return;
	if(e->rev != nil){
		if(e->rev->rev != nil){
			assert(e->rev->rev == e);
			e->rev->rev = nil;
		}
		e->rev = nil;
	}
	for(ep = &e->src->link; *ep != nil; ep = &((*ep)->next))
		if(*ep == e){
			*ep = e->next;
			break;
		}
	e->next = nil;
	e->options = 0;
	e->weight = MaxWeight;
	e->port = 0;
	memset(e->ip, 0, IPaddrlen);
	e->deleted = 1;
}

Host*
gethost(char *name, int new)
{
	char buf[8*1024], *s, *e, *x;
	Host *h;
	int fd, n;

	if(*name == 0 || *name == '.' || strchr(name, '/') != nil)
		return nil;
	qlock(&hostslk);
	for(h = hosts; h != nil; h = h->next)
		if(strcmp(h->name, name) == 0)
			goto out;

	n = -1;
	snprint(buf, sizeof(buf), "hosts/%s", name);
	if((fd = open(buf, OREAD)) >= 0){
		n = read(fd, buf, sizeof(buf)-1);
		close(fd);
	}
	if(n < 0){
		if(!new)
			goto out;
		n = 0;
	}
	buf[n] = 0;

	h = emalloc(sizeof(Host));
	h->name = estrdup(name);
	h->addr = estrdup(name);
	h->port = 655;
	h->pmtu = DefPMTU;
	h->options = OptClampMss|OptPmtuDiscov;
	h->udpfd = -1;
	h->connected = 0;
	h->rsapub = nil;
	h->next = hosts;
	hosts = h;
	if((s = (char*)decodePEM(buf, "RSA PUBLIC KEY", &n, nil)) != nil){
		h->rsapub = asn1toRSApub((uchar*)s, n);
		free(s);
	}
	for(s = buf; s != nil; s = e){
		char *f[2];

		if((e = strchr(s, '\n')) != nil)
			*e++ = 0;
		if(*s == '#' || (x = strchr(s, '=')) == nil)
			continue;
		*x = ' ';
		if((n = tokenize(s, f, 2)) != 2)
			continue;
		if(cistrcmp(f[0], "Address") == 0){
			if(tokenize(f[1], f, 2) > 1)
				h->port = atoi(f[1]);
			free(h->addr);
			h->addr = estrdup(f[0]);
			continue;
		}
		if(cistrcmp(f[0], "IndirectData") == 0){
			h->options |= OptIndirect*(cistrcmp(f[1], "yes") == 0);
			continue;
		}
		if(cistrcmp(f[0], "TCPonly") == 0){
			h->options |= OptTcpOnly*(cistrcmp(f[1], "yes") == 0);
			continue;
		}
		if(cistrcmp(f[0], "ClampMSS") == 0){
			h->options &= ~(OptClampMss*(cistrcmp(f[1], "no") == 0));
			continue;
		}
		if(cistrcmp(f[0], "PMTUDiscovery") == 0){
			h->options &= ~(OptPmtuDiscov*(cistrcmp(f[1], "no") == 0));
			continue;
		}
		if(cistrcmp(f[0], "PMTU") == 0){
			h->pmtu = atoi(f[1]);
			if(h->pmtu > MaxPacket)
				h->pmtu = MaxPacket;
			else if(h->pmtu < 512)
				h->pmtu = 512;
			continue;
		}
		if(cistrcmp(f[0], "Port") == 0){
			h->port = atoi(f[1]);
			continue;
		}
		if(cistrcmp(f[0], "Subnet") == 0){
			if(myhost == nil)
				getsubnet(h, f[1], 1);
			continue;
		}
	}
	parseip(h->ip, h->addr);
out:
	qunlock(&hostslk);
	return h;
}

Host*
findhost(uchar ip[IPaddrlen], int port)
{
	Host *h;

	qlock(&hostslk);
	for(h = hosts; h != nil; h = h->next){
		if(ipcmp(ip, h->ip) == 0 && (port == -1 || port == h->port))
			break;
	}
	qunlock(&hostslk);
	return h;
}

AuthRpc*
getrsarpc(void)
{
	AuthRpc *rpc;
	int afd, r;
	char *s;
	mpint *m;

	if(myhost->rsapub == nil){
		werrstr("no RSA public key");
		return nil;
	}
	if((afd = open("/mnt/factotum/rpc", ORDWR)) < 0)
		return nil;
	if((rpc = auth_allocrpc(afd)) == nil){
		close(afd);
		return nil;
	}
	m = mpnew(0);
	s = smprint("proto=rsa service=tinc role=client host=%q", myhost->name);
	r = auth_rpc(rpc, "start", s, strlen(s));
	free(s);
	if(r != ARok){
		goto Err;
	}
	werrstr("no key found");
	while(auth_rpc(rpc, "read", nil, 0) == ARok){
		s = rpc->arg;
		if(strtomp(s, &s, 16, m) == nil)
			continue;
		if(mpcmp(m, myhost->rsapub->n) != 0)
			continue;
		mpfree(m);
		return rpc;
	}
Err:
	mpfree(m);
	auth_freerpc(rpc);
	close(afd);
	return nil;
}
void
putrsarpc(AuthRpc *rpc)
{
	if(rpc == nil)
		return;
	close(rpc->afd);
	auth_freerpc(rpc);
}


int
conread(Conn *c)
{
	int n;

	if(c->rp > c->buf){
		memmove(c->buf, c->rp, c->wp - c->rp);
		c->wp -= (c->rp - c->buf);
		c->rp = c->buf;
	}
	if((n = read(c->fd, c->wp, &c->buf[sizeof(c->buf)] - c->wp)) <= 0)
		return n;
	if(c->cin->crypt != nil)
		(*c->cin->crypt)((uchar*)c->wp, n, c->cin->cs);
	c->wp += n;
	return n;
}
int
conwrite(Conn *c, char *s, int n)
{
	if(c->cout->crypt != nil)
		(*c->cout->crypt)((uchar*)s, n, c->cout->cs);
	if(write(c->fd, s, n) != n)
		return -1;
	return 0;
}

int
conrecv(Conn *c, char **f, int nf)
{
	char *s, *e;

	do {
		if(c->wp > c->rp && (e = memchr(s = c->rp, '\n', c->wp - c->rp)) != nil){
			*e++ = 0;
			c->rp = e;
if(debug) fprint(2, "<-%s %s\n", c->host != nil ? c->host->name : "???", s);
			return tokenize(s, f, nf);
		}
	} while(conread(c) > 0);
	return 0;
}
int
consend(Conn *c, char *fmt, ...)
{
	char buf[1024];
	va_list a;
	int n;

	if(c == nil)
		return -1;

	va_start(a, fmt);
	n = vsnprint(buf, sizeof(buf)-2, fmt, a);
	va_end(a);

	buf[n++] = '\n';
	buf[n] = 0;

	qlock(&c->sendlk);
if(debug) fprint(2, "->%s %s", c->host != nil ? c->host->name : "???", buf);
	n = conwrite(c, buf, n);
	qunlock(&c->sendlk);

	return n;
}

int
recvudp(Host *h, int fd)
{
	uchar buf[4+MaxPacket+AESbsize+MAClen], mac[SHA2_256dlen];
	AESstate cs[1];
	uint seq;
	int n, o;

	if((n = read(fd, buf, sizeof(buf))) <= 0)
		return -1;
	lock(h->cin);
	if(h->cin->crypt == nil || (n -= MAClen) < AESbsize){
		unlock(h->cin);
		return -1;
	}
	hmac_sha2_256(buf, n, h->cin->key, sizeof(h->cin->key), mac, nil);
	if(tsmemcmp(mac, buf+n, MAClen) != 0){
		unlock(h->cin);
		return -1;
	}
	memmove(cs, h->cin->cs, sizeof(cs));
	(*h->cin->crypt)(buf, n, cs);
	if((n -= buf[n-1]) < 4){
		unlock(h->cin);
		return -1;
	}

	seq  = buf[0]<<24;
	seq |= buf[1]<<16;
	seq |= buf[2]<<8;
	seq |= buf[3]<<0;

	if((o = (int)(seq - h->cin->seq)) > 0){
		h->cin->seq = seq;
		h->ooo = o < 64 ? h->ooo<<o | 1ULL : 0ULL;
	} else {
		o = -o;
		if(o >= 64 || (h->ooo & 1ULL<<o) != 0){
			unlock(h->cin);
			return 0;
		}
		h->ooo |= 1ULL<<o;
	}
	h->udpfd = fd;
	unlock(h->cin);

	/* pmtu probe */
	if(n >= 4+14 && buf[4+12] == 0 && buf[4+13] == 0){
		if(buf[4+0] == 0){
			buf[4+0] = 1;
			sendudp(h, fd, buf+4, n-4);
		}
		return 0;
	}

	routepkt(h, buf+4, n-4);
	return 0;
}
int
sendudp(Host *h, int fd, uchar *p, int n)
{
	uchar buf[4+MaxPacket+AESbsize+SHA2_256dlen];
	AESstate cs[1];
	uint seq;
	int pad;

	if(fd < 0 || n > MaxPacket)
		return -1;
	lock(h->cout);
	if(h->cout->crypt == nil){
		unlock(h->cout);
		needkey(h);
		return -1;
	}

	seq = ++h->cout->seq;
	buf[0] = seq>>24;
	buf[1] = seq>>16;
	buf[2] = seq>>8;
	buf[3] = seq>>0;

	memmove(buf+4, p, n), n += 4;
	pad = AESbsize - ((uint)n % AESbsize);
	memset(buf+n, pad, pad), n += pad;
	memmove(cs, h->cout->cs, sizeof(cs));
	(*h->cout->crypt)(buf, n, cs);
	hmac_sha2_256(buf, n, h->cout->key, sizeof(h->cout->key), buf+n, nil);
	unlock(h->cout);
	n += MAClen;
	if(write(fd, buf, n) != n)
		return -1;
	if((seq & 0xFFFFF) == 0) needkey(h);
	return 0;
}

int
sendtcp(Host *h, uchar *p, int n)
{
	char buf[24];
	Conn *c;
	int m;

	if((c = h->conn) == nil)
		return -1;
	m = snprint(buf, sizeof(buf), "17 %d\n", n);
	qlock(&c->sendlk);
	if(conwrite(c, buf, m) < 0
	|| conwrite(c, (char*)p, n) < 0)
		n = -1;
	else
		n = 0;
	qunlock(&c->sendlk);
	return n;
}

void
forward(Host *s, Host *d, uchar *p, int n)
{
	if(d->from == nil)
		return;
	while(d != s && d != myhost){
		if(n <= d->pmtu && sendudp(d, d->udpfd, p, n) == 0)
			return;
		if(sendtcp(d, p, n) == 0)
			return;
		d = d->from;
	}
}

int
updatebyte(int csum, uchar *b, uchar *p, int v)
{
	int o;

	o = *p;
	v &= 255;
	*p = v;
	if(((p - b) & 1) == 0){
		o <<= 8;
		v <<= 8;
	}
	csum += o^0xFFFF;
	csum += v;
	while(v = csum >> 16)
		csum = (csum & 0xFFFF) + v;
	return csum;
}

void
clampmss(Host *d, uchar *p, int n, int o)
{
	int oldmss, newmss, csum;
	uchar *h, *e;

	if(n <= TcpHdr || (p[13]&2) == 0 || (d->options & OptClampMss) == 0)
		return;
	if((e = p+(p[12]>>4)*4) > p+n)
		return;
	for(h = p+TcpHdr; h < e;){
		switch(h[0]){
		case 0:
			return;
		case 1:
			h++;
			continue;
		}
		if(h[1] < 2 || h[1] > e - h)
			return;
		if(h[0] == 2 && h[1] == 4)
			goto Found;
		h += h[1];
	}
	return;
Found:
	oldmss = h[2]<<8 | h[3];
	newmss = myhost->pmtu;
	if(d->pmtu < newmss)
		newmss = d->pmtu;
	newmss -= o + TcpHdr;
	if(oldmss <= newmss)
		return;
if(debug) fprint(2, "clamping tcp mss %d -> %d for %s\n", oldmss, newmss, d->name);
	csum = (p[16]<<8 | p[17]) ^ 0xFFFF;
	csum = updatebyte(csum, p, h+2, newmss>>8);
	csum = updatebyte(csum, p, h+3, newmss);
	csum ^= 0xFFFF;
	p[16] = csum>>8;
	p[17] = csum;
}

void
routepkt(Host *s, uchar *p, int n)
{
	uchar src[IPaddrlen], dst[IPaddrlen];
	int o, type;
	Snet *t;

Ether:
	if(n <= EtherHdr)
		return;
	switch(p[EtherType+0]<<8 | p[EtherType+1]){
	default:
		return;
	case 0x8100:	/* VLAN */
		memmove(p+4, p, 2*Eaddrlen);
		p += 4, n -= 4;
		goto Ether;
	case 0x0800:	/* IPv4 */
	case 0x86DD:	/* IPv6 */
		break;
	}
	switch(p[EtherHdr] & 0xF0){
	default:
		return;
	case 0x40:
		o = EtherHdr+(p[EtherHdr] & 15)*4;
		if(n < EtherHdr+Ip4Hdr || n < o)
			return;
		type = p[EtherHdr+9];
		v4tov6(src, p+EtherHdr+12);
		v4tov6(dst, p+EtherHdr+16);
		break;
	case 0x60:
		o = EtherHdr+Ip6Hdr;
		if(n < o)
			return;
		type = p[EtherHdr+6];
		memmove(src, p+EtherHdr+8, 16);
		memmove(dst, p+EtherHdr+24, 16);
		break;
	}
	netlock(nil);
	if((t = lookupnet(dst)) != nil){
		if(type == 6)	/* TCP */
			clampmss(t->owner, p+o, n-o, o);
		if(t->owner == myhost)
			write(ipdfd, p+EtherHdr, n-EtherHdr);
		else
			forward(s, t->owner, p, n);
	}
	netunlock(nil);
}

void
updateweight(Edge *e, int ms)
{
	e->weight = (e->weight + ms) / 2;
	if(e->weight < 0)
		e->weight = 0;
}

int
metaauth(Conn *c)
{
	mpint *m, *h;
	uchar b[512];
	AuthRpc *rpc;
	char *f[8];
	int n, n1, n2, ms;
	Edge *e;

	c->pingtime = nsec();
	if(consend(c, "%d %s 17", ID, myhost->name) < 0)
		return -1;
	n = conrecv(c, f, nelem(f));
	if(n != 3 || atoi(f[0]) != ID || atoi(f[2]) != 17)
		return -1;
	if((c->host = gethost(f[1], 0)) == nil
	|| c->host == myhost || c->host->rsapub == nil)
		return -1;

	n1 = (mpsignif(c->host->rsapub->n)+7)/8;
	if(n1 < AESkeylen+AESbsize || n1 > sizeof(b))
		return -1;
	n2 = (mpsignif(myhost->rsapub->n)+7)/8;
	if(n2 < AESkeylen+AESbsize || n2 > sizeof(b))
		return -1;

	m = mpnrand(c->host->rsapub->n, genrandom, nil);
	mptober(m, b, n1);
	setupAESstate(c->cout->cs, b+n1-AESkeylen, AESkeylen, b+n1-AESkeylen-AESbsize);
	rsaencrypt(c->host->rsapub, m, m);
	mptober(m, b, n1);
	mpfree(m);
	if(consend(c, "%d %d %d 0 0 %.*H", META_KEY, EVP_AES256CFB, EVP_SHA256, n1, b) < 0)
		return -1;
	c->cout->crypt = aesCFBencrypt;

	n = conrecv(c, f, nelem(f));
	if(n != 6 || atoi(f[0]) != META_KEY || strlen(f[5]) != 2*n2)
		return -1;
	if(atoi(f[1]) != EVP_AES256CFB || atoi(f[2]) != EVP_SHA256){
		fprint(2, "%s uses unknown cipher/digest algorithms: %s %s\n",
			c->host->name, f[1], f[2]);
		return -1;
	}

	if((rpc = getrsarpc()) == nil
	|| auth_rpc(rpc, "write", f[5], strlen(f[5])) != ARok
	|| auth_rpc(rpc, "read", nil, 0) != ARok){
		putrsarpc(rpc);
		return -1;
	}

	m = strtomp(rpc->arg, nil, 16, nil);
	putrsarpc(rpc);
	mptober(m, b, n2);
	mpfree(m);
	setupAESstate(c->cin->cs, b+n2-AESkeylen, AESkeylen, b+n2-AESkeylen-AESbsize);
	c->cin->crypt = aesCFBdecrypt;

	h = mpnrand(c->host->rsapub->n, genrandom, nil);
	mptober(h, b, n1);
	if(consend(c, "%d %.*H", CHALLENGE, n1, b) < 0){
		mpfree(h);
		return -1;
	}
	sha2_256(b, n1, b, nil);
	betomp(b, SHA2_256dlen, h);

	n = conrecv(c, f, nelem(f));
	if(n != 2 || atoi(f[0]) != CHALLENGE){
		mpfree(h);
		return -1;
	}
	m = strtomp(f[1], nil, 16, nil);
	mptober(m, b, n2);
	mpfree(m);
	sha2_256(b, n2, b, nil);
	if(consend(c, "%d %.*H", CHAL_REPLY, SHA2_256dlen, b) < 0){
		mpfree(h);
		return -1;
	}
	n = conrecv(c, f, nelem(f));
	if(n != 2 || atoi(f[0]) != CHAL_REPLY){
		mpfree(h);
		return -1;
	}
	m = strtomp(f[1], nil, 16, nil);
	n = mpcmp(m, h);
	mpfree(m);
	mpfree(h);
	if(n != 0)
		return -1;
	ms = (nsec() - c->pingtime)/1000000LL;
	if(consend(c, "%d %d %d %x", ACK, myhost->port, ms, myhost->options) < 0)
		return -1;
	n = conrecv(c, f, nelem(f));
	if(n != 4 || atoi(f[0]) != ACK)
		return -1;

	netlock(c);
	e = getedge(myhost, c->host, 1);
	memmove(e->ip, c->ip, IPaddrlen);
	e->port = atoi(f[1]);
	e->weight = atoi(f[2]);
	e->options = strtol(f[3], nil, 16);
	updateweight(e, ms);
	c->pingtime = 0;
	c->edge = e;
	c->host->conn = c;
	netunlock(c);

	return 0;
}

Conn*
nearcon(Host *to)
{
	while(to != nil && to != myhost){
		if(to->conn != nil)
			return to->conn;
		to = to->from;
	}
	return nil;
}

void
sendkey(Host *to)
{
	lock(to->cin);
	to->ooo = 0;
	to->cin->seq = 0;
	genrandom(to->cin->key, sizeof(to->cin->key));
	setupAESstate(to->cin->cs, to->cin->key, AESkeylen, to->cin->key+AESkeylen);
	to->cin->crypt = aesCBCdecrypt;
	unlock(to->cin);

	consend(nearcon(to), "%d %s %s %.*H %d %d %d %d", ANS_KEY,
		myhost->name, to->name,
		sizeof(to->cin->key), to->cin->key,
		EVP_AES256CBC, EVP_SHA256, MAClen, 0);
}
void
needkey(Host *from)
{
	consend(nearcon(from), "%d %s %s", REQ_KEY, myhost->name, from->name);
}
void
recvkey(Host *from, char **f)
{
	uchar key[sizeof(from->cout->key)];
	mpint *m;

	if(atoi(f[1]) != EVP_AES256CBC || atoi(f[2]) != EVP_SHA256
	|| atoi(f[3]) != MAClen || atoi(f[4]) != 0){
		fprint(2, "%s key uses unknown parameters: %s %s %s %s\n",
			from->name, f[1], f[2], f[3], f[4]);
		return;
	}
	if(strlen(f[0]) != sizeof(key)*2)
		return;
	if((m = strtomp(f[0], nil, 16, nil)) == nil)
		return;
	mptober(m, key, sizeof(key));
	mpfree(m);
	lock(from->cout);
	if(tsmemcmp(key, from->cout->key, sizeof(key)) == 0)
		goto Out;
	from->cout->seq = 0;
	memmove(from->cout->key, key, sizeof(key));
	setupAESstate(from->cout->cs, from->cout->key, AESkeylen, from->cout->key+AESkeylen);
	from->cout->crypt = aesCBCencrypt;
Out:
	unlock(from->cout);
	memset(key, 0, sizeof(key));
}
void
clearkey(Host *from)
{
	lock(from->cout);
	from->cout->crypt = nil;
	from->cout->seq = 0;
	memset(from->cout->cs, 0, sizeof(from->cout->cs));
	genrandom(from->cout->key, sizeof(from->cout->key));
	unlock(from->cout);
}

void
metapeer(Conn *c)
{
	char *f[8];
	Host *h, *r;
	Edge *e;
	Snet *t;
	int i, n;

	netlock(nil);
	for(i=0; i<nsnet; i++)
		reportsubnet(c, snet[i]);
	for(i=0; i<nedges; i++)
		reportedge(c, edges[i]);
	netunlock(nil);

	sendkey(c->host);
	while((n = conrecv(c, f, nelem(f))) > 0){
		switch(atoi(f[0])){
		case PING:
			netlock(c);
			n = consend(c, "%d %x", PONG, rand());
			netunlock(c);
			if(n < 0)
				return;
			continue;
		case PONG:
			netlock(c);
			if(c->pingtime != 0){
				if((e = getedge(myhost, c->host, 0)) != nil)
					updateweight(e, (nsec() - c->pingtime) / 1000000LL);
				c->pingtime = 0;
			}
			netunlock(c);
			continue;
		case ADD_SUBNET:
			if(n != 4 || (h = gethost(f[2], 1)) == nil || h == myhost)
				break;
			netlock(c);
			getsubnet(h, f[3], 1);
			netunlock(c);
			continue;
		case DEL_SUBNET:
			if(n != 4 || (h = gethost(f[2], 0)) == nil || h == myhost)
				break;
			netlock(c);
			if((t = getsubnet(h, f[3], 0)) != nil)
				delsubnet(t);
			netunlock(c);
			continue;
		case ADD_EDGE:
			if(n != 8 || (h = gethost(f[2], 1)) == nil || h == myhost
			|| (r = gethost(f[3], 1)) == nil)
				break;
			netlock(c);
			if((e = getedge(h, r, 1)) != nil){
				if(parseip(e->ip, f[4]) == -1)
					memmove(e->ip, r->ip, IPaddrlen);
				e->port = atoi(f[5]);
				e->weight = atoi(f[7]);
				e->options = strtol(f[6], nil, 16);
			}
			netunlock(c);
			continue;
		case DEL_EDGE:
			if(n != 4 || (h = gethost(f[2], 0)) == nil || h == myhost
			|| (r = gethost(f[3], 1)) == nil)
				break;
			netlock(c);
			if((e = getedge(h, r, 0)) != nil)
				deledge(e);
			netunlock(c);
			continue;
		case KEY_CHANGED:
			if(n != 3 || (h = gethost(f[2], 0)) == nil || h == myhost)
				break;
			netlock(c);
			clearkey(h);
			for(e = myhost->link; e != nil; e = e->next)
				if(e->dst->conn != c && e->dst->from == myhost)
					consend(e->dst->conn, "%s %s %s", f[0], f[1], f[2]);
			netunlock(c);
			continue;
		case REQ_KEY:
			if(n != 3 || (h = gethost(f[1], 0)) == nil || h == myhost
			|| (r = gethost(f[2], 0)) == nil)
				break;
			netlock(nil);
			if(r != myhost)
				consend(nearcon(r), "%s %s %s", f[0], f[1], f[2]);
			else
				sendkey(h);
			netunlock(nil);
			continue;
		case ANS_KEY:
			if(n != 8 || (h = gethost(f[1], 0)) == nil || h == myhost
			|| (r = gethost(f[2], 0)) == nil)
				break;
			netlock(nil);
			if(r != myhost)
				consend(nearcon(r), "%s %s %s %s %s %s %s %s",
					f[0], f[1], f[2], f[3],
					f[4], f[5], f[6], f[7]);
			else
				recvkey(h, &f[3]);
			netunlock(nil);
			continue;
		case TCP_PACKET:
			if(n != 2)
				return;
			n = atoi(f[1]);
			if(n < 0 || n > MaxPacket)
				return;
			while((c->wp - c->rp) < n && conread(c) > 0)
				;
			if(c->wp - c->rp < n)
				return;
			routepkt(c->host, (uchar*)c->rp, n);
			c->rp += n;
			continue;
		}
	}
}

void
tcpclient(int fd, int incoming)
{
	Conn *c;
	char dir[128];

	c = emalloc(sizeof(Conn));
	c->host = nil;
	c->fd = fd;
	c->rp = c->wp = c->buf;
	c->port = dir2ipport(fd2dir(fd, dir, sizeof(dir)), c->ip);
	procsetname("tcpclient %s %s %s %I!%d", myhost->name,
		incoming ? "in" : "out", dir, c->ip, c->port);
	if(metaauth(c) == 0){
		procsetname("tcpclient %s %s %s %I!%d %s", myhost->name,
			incoming ? "in" : "out", dir, c->ip, c->port, c->host->name);
		metapeer(c);
	}
	netlock(c);
	if(c->host != nil && c->host->conn == c){
		c->host->conn = nil;
		if(c->edge != nil && c->edge->dst == c->host){
			deledge(c->edge->rev);
			deledge(c->edge);
		}
		hangupfd(c->host->udpfd);
	}
	netunlock(c);
	memset(c, 0, sizeof(*c));
	free(c);
	close(fd);
}

void
udpclient(int fd, int incoming)
{
	uchar ip[IPaddrlen];
	char dir[128];
	int port;
	Host *h;

	port = dir2ipport(fd2dir(fd, dir, sizeof(dir)), ip);
	h = findhost(ip, port);
	if(h == nil && incoming)
		h = findhost(ip, -1);	/* might be behind NAT */
	if(h != nil && h != myhost){
		procsetname("udpclient %s %s %s %I!%d %s", myhost->name,
			incoming ? "in": "out", dir, ip, port, h->name);

		if(!incoming){
			lock(h->cin);
			if(h->udpfd == -1)
				h->udpfd = fd;
			unlock(h->cin);
		}

		do {
			alarm(15*1000);
		} while(recvudp(h, fd) == 0);

		lock(h->cin);
		if(h->udpfd == fd)
			h->udpfd = -1;
		unlock(h->cin);

		wlock(&netlk);
		wunlock(&netlk);
	}
	close(fd);
}

int
dialer(char *proto, char *host, int rport, int lport)
{
	char addr[40], local[16];
	int dfd;

	snprint(local, sizeof(local), "%d", lport);
	snprint(addr, sizeof(addr), "%s/%s!%s!%d", outside, proto, host, rport);
	procsetname("dialer %s %s", myhost->name, addr);

	for(;;){
		if((dfd = dial(addr, lport ? local : nil, nil, nil)) >= 0){
			switch(rfork(RFPROC|RFMEM)){
			case 0:
				return dfd;
			case -1:
				close(dfd);
				continue;
			}
			if(waitpid() < 0)
				return -1;
		}
		sleep(10000);
	}
}

int
listener(char *proto, int port, int nprocs)
{
	char addr[40], adir[40], ldir[40];
	int acfd, lcfd, dfd;

	snprint(addr, sizeof(addr), "%s/%s!*!%d", outside, proto, port);
	procsetname("listener %s %s", myhost->name, addr);

	if((acfd = announce(addr, adir)) < 0)
		return -1;
	while((lcfd = listen(adir, ldir)) >= 0){
		if((dfd = accept(lcfd, ldir)) >= 0)
			switch(rfork(RFPROC|RFMEM)){
			default:
				if(nprocs > 1 || waitpid() < 0) nprocs--;
				break;
			case 0:
				return dfd;
			case -1:
				close(dfd);
			}
		close(lcfd);
	}
	close(acfd);
	return -1;
}

void
pingpong(void)
{
	Edge *e;
	Conn *c;

	procsetname("pingpong %s", myhost->name);
	for(;;){
		sleep(15*1000 + (rand() % 3000));
		netlock(nil);
		for(e = myhost->link; e != nil; e = e->next){
			if((c = e->dst->conn) != nil){
				if(c->pingtime != 0){
					hangupfd(c->fd);
					continue;
				}
				c->pingtime = nsec();
				consend(c, "%d %x", PING, rand());
			}
		}
		netunlock(nil);
	}
}

void
ipifcsetup(void)
{
	int n;

	snprint(device, sizeof device, "%s/ipifc/clone", inside);
	if((ipcfd = open(device, ORDWR)) < 0)
		sysfatal("can't open ip interface: %r");
	if((n = read(ipcfd, device, sizeof device - 1)) <= 0)
		sysfatal("can't read interface number: %r");
	device[n] = 0;
	snprint(device, sizeof device, "%s/ipifc/%d/data", inside, atoi(device));
	if((ipdfd = open(device, ORDWR)) < 0)
		sysfatal("can't open ip data: %r");
	fprint(ipcfd, "bind pkt");
	fprint(ipcfd, "mtu %d", myhost->pmtu-EtherHdr);
	fprint(ipcfd, "add %I %M", localip, localmask);
	*strrchr(device, '/') = 0;
}

void
ip2tunnel(void)
{
	uchar buf[MaxPacket];
	int n;

	procsetname("ip2tunnel %s %s %I %M", myhost->name,
		fd2dir(ipdfd, (char*)buf, sizeof(buf)),
		localip, localmask);
	while((n = read(ipdfd, buf+EtherHdr, sizeof buf-EtherHdr)) > 0){
		memset(buf, 0, 2*Eaddrlen);
		if((buf[EtherHdr]&0xF0) == 0x60){
			buf[EtherType+0] = 0x86;
			buf[EtherType+1] = 0xDD;
		} else{
			buf[EtherType+0] = 0x08;
			buf[EtherType+1] = 0x00;
		}
		routepkt(myhost, buf, n+EtherHdr);
	}
}

void
catch(void*, char *msg)
{
	if(strcmp(msg, "alarm") == 0 || strcmp(msg, "interrupt") == 0)
		noted(NCONT);
	noted(NDFLT);
}
void
shutdown(void)
{
	postnote(PNGROUP, getpid(), "shutdown");
}

void
usage(void)
{
	fprint(2, "%s [-d] [-p maxprocs] [-x inside] [-o outside] [-c confdir] [-n myname] "
		"localip localmask [host...]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Host *h;
	Snet *t;
	AuthRpc *rpc;
	int i, pfd[2];

	quotefmtinstall();
	fmtinstall('I', eipfmt);
	fmtinstall('M', eipfmt);
	fmtinstall('H', encodefmt);

	ARGBEGIN {
	case 'd':
		debug++;
		break;
	case 'p':
		if((maxprocs = atoi(EARGF(usage()))) < 1)
			sysfatal("bad number of procs");
		break;
	case 'c':
		if(chdir(EARGF(usage())) < 0)
			sysfatal("can't change directory: %r");
		break;
	case 'n':
		myname = EARGF(usage());
		break;
	case 'x':
		outside = inside = EARGF(usage());
		break;
	case 'o':
		outside = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND;

	if(argc < 2)
		usage();
	if(parseipandmask(localip, localmask, argv[0], argv[1]) == -1)
		sysfatal("bad local ip/mask: %s/%s", argv[0], argv[1]);
	argv += 2, argc -= 2;

	srand(fastrand());
	if(myname == nil)
		myname = sysname();
	if((myhost = gethost(myname, 0)) == nil)
		sysfatal("can't get my host: %r");
	if((rpc = getrsarpc()) == nil)
		sysfatal("can't find my key in factotum: %r");
	putrsarpc(rpc);

	for(i = 0; i < argc; i++){
		if((h = gethost(argv[i], 0)) == nil)
			sysfatal("unknown host: %s", argv[i]);
		if(h == myhost)
			sysfatal("will not connect to myself");
		if(h->rsapub == nil)
			sysfatal("no RSA public key for: %s", h->name);
	}

	if(myhost->snet == nil){
		char snet[64];
		snprint(snet, sizeof(snet), "%I/128", localip);
		getsubnet(myhost, snet, 1);
	}
	if((t = lookupnet(localip)) == nil)
		sysfatal("no subnet found for local ip %I", localip);
	if(t->owner != myhost)
		sysfatal("local ip %I belongs to host %s subnet %I %M",
			localip, t->owner->name, t->ip, t->mask);

	if(pipe(pfd) < 0)
		sysfatal("can't create pipe: %r");
	switch(rfork(RFPROC|RFFDG|RFREND|RFNOTEG|RFENVG)){
	case -1:
		sysfatal("can't fork: %r");
	case 0:
		dup(pfd[1], 0);
		close(pfd[0]);
		close(pfd[1]);
		if(!debug){
			close(2);
			while(open("/dev/null", OWRITE) == 1)
				;
		}
		execl("/bin/rc", "rc", debug? "-v": nil, nil);
		sysfatal("can't exec: %r");
	}
	rcfd = pfd[0];
	close(pfd[1]);

	ipifcsetup();
	notify(catch);
	switch(rfork(RFPROC|RFFDG|RFREND|RFNOTEG)){
	case -1:
		sysfatal("can't fork: %r");
	case 0:
		break;
	default:
		if(debug){
			waitpid();
			fprint(ipcfd, "unbind");
		}
		exits(nil);
	}
	atexit(shutdown);

	fprint(rcfd, "NAME=%s NODE=%s DEVICE=%s INTERFACE=%I ./tinc-up\n",
		myname, myhost->name, device, localip);

	if(rfork(RFPROC|RFMEM) == 0){
		tcpclient(listener("tcp", myhost->port, maxprocs), 1);
		exits(nil);
	}
	if((myhost->options & OptTcpOnly) == 0)
	if(rfork(RFPROC|RFMEM) == 0){
		udpclient(listener("udp", myhost->port, maxprocs), 1);
		exits(nil);
	}
	for(i = 0; i < argc; i++){
		if((h = gethost(argv[i], 0)) == nil)
			continue;
		if(rfork(RFPROC|RFMEM) == 0){
			tcpclient(dialer("tcp", h->addr, h->port, myhost->port), 0);
			exits(nil);
		}
		if((h->options & OptTcpOnly) == 0)
		if(rfork(RFPROC|RFMEM) == 0){
			udpclient(dialer("udp", h->addr, h->port, myhost->port), 0);
			exits(nil);
		}
	}
	if(rfork(RFPROC|RFMEM) == 0){
		pingpong();
		exits(nil);
	}
	ip2tunnel();

	fprint(rcfd, "NAME=%s NODE=%s DEVICE=%s INTERFACE=%I ./tinc-down\n",
		myname, myhost->name, device, localip);
}
