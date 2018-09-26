/* minimal stateless DHCPv6 server for network boot */
#include <u.h>
#include <libc.h>
#include <ip.h>
#include <bio.h>
#include <ndb.h>

enum {
	Eaddrlen = 6,

	SOLICIT	= 1,
	ADVERTISE,
	REQUEST,
	CONFIRM,
	RENEW,
	REBIND,
	REPLY,
	RELEASE,
	DECLINE,
	RECONFIGURE,
	INFOREQ,
	RELAYFORW,
	RELAYREPL,
};

typedef struct Req Req;
struct Req
{
	int		tra;

	Udphdr		*udp;
	Ipifc		*ifc;

	uchar		mac[Eaddrlen];
	uchar		ips[IPaddrlen*8];
	int		nips;

	Ndb		*db;
	Ndbtuple	*t;

	struct {
		int	t;
		uchar	*p;
		uchar	*e;
	} req;

	struct {
		int	t;
		uchar	*p;
		uchar	*e;
	} resp;
};

typedef struct Otab Otab;
struct Otab
{
	int	t;
	int	(*f)(uchar *, int, Otab*, Req*);
	char	*q[3];
	int	done;
};

static Otab otab[];
static Ipifc *ipifcs;
static ulong starttime;
static char *ndbfile;
static char *netmtpt = "/net";
static int debug;

static uchar v6loopback[IPaddrlen] = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 1
};

/*
 * open ndbfile as db if not already open.  also check for stale data
 * and reload as needed.
 */
static Ndb *
opendb(void)
{
	static ulong lastcheck;
	static Ndb *db;
	ulong now = time(nil);

	/* check no more often than once every minute */
	if(db == nil) {
		db = ndbopen(ndbfile);
		if(db != nil)
			lastcheck = now;
	} else if(now >= lastcheck + 60) {
		if (ndbchanged(db))
			ndbreopen(db);
		lastcheck = now;
	}
	return db;
}

static Ipifc*
findifc(char *net, uchar ip[IPaddrlen])
{
	Ipifc *ifc;
	Iplifc *lifc;

	ipifcs = readipifc(net, ipifcs, -1);
	for(ifc = ipifcs; ifc != nil; ifc = ifc->next)
		for(lifc = ifc->lifc; lifc != nil; lifc = lifc->next)
			if(ipcmp(lifc->ip, ip) == 0)
				return ifc;

	return nil;
}

static Iplifc*
localonifc(Ipifc *ifc, uchar ip[IPaddrlen])
{
	Iplifc *lifc;
	uchar net[IPaddrlen];

	for(lifc = ifc->lifc; lifc != nil; lifc = lifc->next){
		maskip(ip, lifc->mask, net);
		if(ipcmp(net, lifc->net) == 0)
			return lifc;
	}

	return nil;
}

static int
openlisten(char *net)
{
	int fd, cfd;
	char data[128], devdir[40];
	Ipifc *ifc;
	Iplifc *lifc;

	sprint(data, "%s/udp!*!dhcp6s", net);
	cfd = announce(data, devdir);
	if(cfd < 0)
		sysfatal("can't announce: %r");
	if(fprint(cfd, "headers") < 0)
		sysfatal("can't set header mode: %r");

	ipifcs = readipifc(net, ipifcs, -1);
	for(ifc = ipifcs; ifc != nil; ifc = ifc->next){
		if(strcmp(ifc->dev, "/dev/null") == 0)
			continue;
		for(lifc = ifc->lifc; lifc != nil; lifc = lifc->next){
			if(!ISIPV6LINKLOCAL(lifc->ip))
				continue;
			if(fprint(cfd, "addmulti %I ff02::1:2", lifc->ip) < 0)
				fprint(2, "addmulti: %I: %r\n", lifc->ip);
		}
	}

	sprint(data, "%s/data", devdir);
	fd = open(data, ORDWR);
	if(fd < 0)
		sysfatal("open udp data: %r");

	return fd;
}

static uchar*
gettlv(int x, int *plen, uchar *p, uchar *e)
{
	int t;
	int l;

	if(plen != nil)
		*plen = 0;
	while(p+4 <= e){
		t = p[0]<<8 | p[1];
		l = p[2]<<8 | p[3];
		if(p+4+l > e)
			break;
		if(t == x){
			if(plen != nil)
				*plen = l;
			return p+4;
		}
		p += l+4;
	}
	return nil;
}

static int
getv6ips(uchar *ip, int n, Ndbtuple *t, char *attr)
{
	int r = 0;

	if(n < IPaddrlen)
		return 0;
	if(*attr == '@')
		attr++;
	for(; t != nil; t = t->entry){
		if(strcmp(t->attr, attr) != 0)
			continue;
		if(parseip(ip, t->val) == -1)
			continue;
		if(isv4(ip))
			continue;
		ip += IPaddrlen;
		r += IPaddrlen;
		if(r >= n)
			break;
	}
	return r;
}

static int
lookupips(uchar *ip, int n, Ndb *db, uchar mac[Eaddrlen])
{
	Ndbtuple *t;
	Ndbs s;
	char val[256], *attr;
	int r;

	/*
	 *  use hardware address to find an ip address
	 */
	attr = "ether";
	snprint(val, sizeof val, "%E", mac);

	t = ndbsearch(db, &s, attr, val);
	r = 0;
	while(t != nil){
		r += getv6ips(ip + r, n - r, t, "ip");
		ndbfree(t);
		if(r >= n)
			break;
		t = ndbsnext(&s, attr, val);
	}
	return r;
}

static void
clearotab(void)
{
	Otab *o;

	for(o = otab; o->t != 0; o++)
		o->done = 0;
}

static Otab*
findotab(int t)
{
	Otab *o;

	for(o = otab; o->t != 0; o++)
		if(o->t == t)
			return o;
	return nil;
}

static int
addoption(Req *r, int t)
{
	Otab *o;
	int n;

	if(r->resp.p+4 > r->resp.e)
		return -1;
	o = findotab(t);
	if(o == nil || o->f == nil || o->done)
		return -1;
	o->done = 1;
	n = (*o->f)(r->resp.p+4, r->resp.e - (r->resp.p+4), o, r);
	if(n < 0 || r->resp.p+4+n > r->resp.e)
		return -1;
	r->resp.p[0] = t>>8, r->resp.p[1] = t;
	r->resp.p[2] = n>>8, r->resp.p[3] = n;
	if(debug) fprint(2, "%d(%.*H)\n", t, n, r->resp.p+4);
	r->resp.p += 4+n;
	return n;
}

static void
usage(void)
{
	fprint(2, "%s [-d]  [-f ndbfile] [-x netmtpt]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	uchar ibuf[4096], obuf[4096];
	Req r[1];
	int fd, n, i;

	fmtinstall('H', encodefmt);
	fmtinstall('I', eipfmt);
	fmtinstall('E', eipfmt);

	ARGBEGIN {
	case 'd':
		debug++;
		break;
	case 'f':
		ndbfile = EARGF(usage());
		break;
	case 'x':
		netmtpt = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND;

	starttime = time(nil) - 946681200UL;

	if(opendb() == nil)
		sysfatal("opendb: %r");

	fd = openlisten(netmtpt);

	/* put process in background */
	if(!debug)
	switch(rfork(RFNOTEG|RFPROC|RFFDG)) {
	default:
		exits(nil);
	case -1:
		sysfatal("fork: %r");
	case 0:
		break;
	}

	while((n = read(fd, ibuf, sizeof(ibuf))) > 0){
		if(n < Udphdrsize+4)
			continue;

		r->udp = (Udphdr*)ibuf;
		if(isv4(r->udp->raddr))
			continue;
		if((r->ifc = findifc(netmtpt, r->udp->ifcaddr)) == nil)
			continue;
		if(localonifc(r->ifc, r->udp->raddr) == nil)
			continue;

		memmove(obuf, ibuf, Udphdrsize);
		r->req.p = ibuf+Udphdrsize;
		r->req.e = ibuf+n;
		r->resp.p = obuf+Udphdrsize;
		r->resp.e = &obuf[sizeof(obuf)];

		r->tra = r->req.p[1]<<16 | r->req.p[2]<<8 | r->req.p[3];
		r->req.t = r->req.p[0];

		if(debug)
		fprint(2, "%I->%I(%s) typ=%d tra=%x\n",
			r->udp->raddr, r->udp->laddr, r->ifc->dev,
			r->req.t, r->tra);

		switch(r->req.t){
		default:
			continue;
		case SOLICIT:
			r->resp.t = ADVERTISE;
			break;
		case REQUEST:
		case INFOREQ:
			r->resp.t = REPLY;
			break;
		}
		r->resp.p[0] = r->resp.t;
		r->resp.p[1] = r->tra>>16;
		r->resp.p[2] = r->tra>>8;
		r->resp.p[3] = r->tra;

		r->req.p += 4;
		r->resp.p += 4;

		r->t = nil;

		clearotab();

		/* Server Identifier */
		if(addoption(r, 2) < 0)
			continue;

		/* Client Identifier */
		if(addoption(r, 1) < 0)
			continue;

		if((r->db = opendb()) == nil)
			continue;
		r->nips = lookupips(r->ips, sizeof(r->ips), r->db, r->mac)/IPaddrlen;
		if(debug){
			for(i=0; i<r->nips; i++)
				fprint(2, "ip=%I\n", r->ips+i*IPaddrlen);
		}

		addoption(r, 3);
		addoption(r, 6);

		write(fd, obuf, r->resp.p-obuf);
		if(debug) fprint(2, "\n");
	}

	exits(nil);
}

static int
oclientid(uchar *w, int n, Otab*, Req *r)
{
	int len;
	uchar *p;

	if((p = gettlv(1, &len, r->req.p, r->req.e)) == nil)
		return -1;
	if(len < 4+4+Eaddrlen || n < len)
		return -1;
	memmove(r->mac, p+len-Eaddrlen, Eaddrlen);
	memmove(w, p, len);

	return len;
}

static int
oserverid(uchar *w, int n, Otab*, Req *r)
{
	int len;
	uchar *p;

	if(n < 4+4+Eaddrlen)
		return -1;
	w[0] = 0, w[1] = 1;	/* duid type: link layer address + time*/
	w[2] = 0, w[3] = 1;	/* hw type: ethernet */
	w[4] = starttime>>24;
	w[5] = starttime>>16;
	w[6] = starttime>>8;
	w[7] = starttime;
	myetheraddr(w+8, r->ifc->dev);

	/* check if server id matches from the request */
	p = gettlv(2, &len, r->req.p, r->req.e);
	if(p != nil && (len != 4+4+Eaddrlen || memcmp(w, p, 4+4+Eaddrlen) != 0))
		return -1;

	return 4+4+Eaddrlen;
}

static int
oiana(uchar *w, int n, Otab*, Req *r)
{
	int i, len;
	uchar *p;

	p = gettlv(3, &len, r->req.p, r->req.e);
	if(p == nil || len < 3*4)
		return -1;

	len = 3*4 + (4+IPaddrlen+2*4)*r->nips;
	if(n < len)
		return -1;

	memmove(w, p, 3*4);
	w += 3*4;

	for(i = 0; i < r->nips; i++){
		w[0] = 0, w[1] = 5;
		w[2] = 0, w[3] = IPaddrlen+2*4;
		w += 4;

		memmove(w, r->ips + i*IPaddrlen, IPaddrlen);
		w += IPaddrlen;

		memset(w, 255, 2*4);
		w += 2*4;
	}

	return len;
}

static Ndbtuple*
lookup(Req *r, char *av[], int ac)
{
	Ndbtuple *t;
	char *s;

	if(ac <= 0)
		return nil;

	t = nil;
	if(r->nips > 0){
		int i;

		/* use the target ip's to lookup info if any */
		for(i=0; i<r->nips; i++){
			s = smprint("%I", &r->ips[i*IPaddrlen]);
			t = ndbconcatenate(t, ndbipinfo(r->db, "ip", s, av, ac));
			free(s);
		}
	} else {
		Iplifc *lifc;

		/* use the ipv6 networks on the interface */
		for(lifc=r->ifc->lifc; lifc!=nil; lifc=lifc->next){
			if(isv4(lifc->ip)
			|| ipcmp(lifc->ip, v6loopback) == 0
			|| ISIPV6LINKLOCAL(lifc->ip))
				continue;
			s = smprint("%I", lifc->net);
			t = ndbconcatenate(t, ndbipinfo(r->db, "ip", s, av, ac));
			free(s);
		}
	}
	return t;
}

static int
oro(uchar*, int, Otab *o, Req *r)
{
	uchar *p;
	char *av[100];
	int i, j, l, ac;
	Ndbtuple *t;

	p = gettlv(6, &l, r->req.p, r->req.e);
	if(p == nil || l < 2)
		return -1;

	ac = 0;
	for(i=0; i<l; i+=2){
		if((o = findotab(p[i]>>8 | p[i+1])) == nil || o->done)
			continue;
		for(j=0; j<3 && o->q[j]!=nil && ac<nelem(av); j++)
			av[ac++] = o->q[j];
	}

	r->t = lookup(r, av, ac);

	if(debug){
		fprint(2, "ndb(");
		for(t = r->t; t != nil; t = t->entry){
			fprint(2, "%s=%s ", t->attr, t->val);
			if(t->entry != nil && t->entry != t->line)
				fprint(2, "\n");
		}
		fprint(2, ")\n");
	}

	/* process the options */
	for(i=0; i<l; i+=2)
		addoption(r, p[i]>>8 | p[i+1]);

	ndbfree(r->t);
	r->t = nil;

	return -1;
}

static int
oservers(uchar *w, int n, Otab *o, Req *r)
{
	return getv6ips(w, n, r->t, o->q[0]);
}

static int
odomainlist(uchar *w, int n, Otab *o, Req *q)
{
	char val[256];
	Ndbtuple *t;
	int l, r;
	char *s;

	r = 0;
	for(t = q->t; t != nil; t = t->entry){
		if(strcmp(t->attr, o->q[0]) != 0)
			continue;
		if(utf2idn(t->val, val, sizeof(val)) <= 0)
			continue;
		for(s = val; *s != 0; s++){
			for(l = 0; *s != 0 && *s != '.'; l++)
				s++;
			if(r+1+l > n)
				return -1;
			w[r++] = l;
			memmove(w+r, s-l, l);
			r += l;
			if(*s != '.')
				break;
		}
		if(r >= n)
			return -1;
		w[r++] = 0;
	}
	return r;
}

static int
obootfileurl(uchar *w, int n, Otab *, Req *q)
{
	uchar ip[IPaddrlen];
	Ndbtuple *bootf;

	if((bootf = ndbfindattr(q->t, q->t, "bootf")) == nil)
		return -1;
	if(strstr(bootf->val, "://") != nil)
		return snprint((char*)w, n, "%s", bootf->val);
	else if(getv6ips(ip, sizeof(ip), q->t, "tftp"))
		return snprint((char*)w, n, "tftp://[%I]/%s", ip, bootf->val);
	return -1;
}

static Otab otab[] = {
	{  1, oclientid, },
	{  2, oserverid, },
	{  3, oiana, },
	{  6, oro, },
	{ 23, oservers, "@dns" },
	{ 24, odomainlist, "dnsdomain" },
	{ 59, obootfileurl, "bootf", "@tftp", },
	{ 0 },
};
