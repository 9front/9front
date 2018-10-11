/*
 * domain name resolvers, see rfcs 1035 and 1123
 */
#include <u.h>
#include <libc.h>
#include <ip.h>
#include <bio.h>
#include <ndb.h>
#include "dns.h"

typedef struct Dest Dest;
typedef struct Query Query;

enum
{
	Udp, Tcp,

	Answerr=	-1,
	Answnone,

	Maxdest=	32,	/* maximum destinations for a request message */
	Maxoutstanding=	15,	/* max. outstanding queries per domain name */

	/*
	 * these are the old values; we're trying longer timeouts now
	 * primarily for the benefit of remote nameservers querying us
	 * during times of bad connectivity.
	 */
	Maxtrans=	5,	/* maximum transmissions to a server */
	Maxretries=	10,	/* cname+actual resends: was 32; have pity on user */
};
enum { Hurry, Patient, };
enum { Outns, Inns, };

struct Dest
{
	uchar	a[IPaddrlen];	/* ip address */
	DN	*s;		/* name server name */
	RR	*n;		/* name server rr */
	int	nx;		/* number of transmissions */
	int	code;		/* response code; used to clear dp->respcode */
};

struct Query {
	DN	*dp;		/* domain */
	ushort	type;		/* and type to look up */
	Request *req;
	Query	*prev;		/* previous query */

	RR	*nsrp;		/* name servers to consult */

	Dest	*dest;		/* array of destinations */
	Dest	*curdest;	/* pointer to next to fill */
	int	ndest;		/* transmit to this many on this round */

	int	udpfd;

	int	tcpset;
	int	tcpfd;		/* if Tcp, read replies from here */
	int	tcpctlfd;
	uchar	tcpip[IPaddrlen];
};

static RR*	dnresolve1(char*, int, int, Request*, int, int);
static int	netquery(Query *, int);

/*
 * reading /proc/pid/args yields either "name args" or "name [display args]",
 * so return only display args, if any.
 */
static char *
procgetname(void)
{
	int fd, n;
	char *lp, *rp;
	char buf[256];

	snprint(buf, sizeof buf, "#p/%d/args", getpid());
	if((fd = open(buf, OREAD)) < 0)
		return strdup("");
	*buf = '\0';
	n = read(fd, buf, sizeof buf-1);
	close(fd);
	if (n >= 0)
		buf[n] = '\0';
	if ((lp = strchr(buf, '[')) == nil ||
	    (rp = strrchr(buf, ']')) == nil)
		return strdup("");
	*rp = '\0';
	return strdup(lp+1);
}

void
rrfreelistptr(RR **rpp)
{
	RR *rp;

	if (rpp == nil || *rpp == nil)
		return;
	rp = *rpp;
	*rpp = nil;	/* update pointer in memory before freeing list */
	rrfreelist(rp);
}

/*
 *  lookup 'type' info for domain name 'name'.  If it doesn't exist, try
 *  looking it up as a canonical name.
 *
 *  this process can be quite slow if time-outs are set too high when querying
 *  nameservers that just don't respond to certain query types.  in that case,
 *  there will be multiple udp retries, multiple nameservers will be queried,
 *  and this will be repeated for a cname query.  the whole thing will be
 *  retried several times until we get an answer or a time-out.
 */
RR*
dnresolve(char *name, int class, int type, Request *req, RR **cn, int depth,
	int recurse, int rooted, int *status)
{
	RR *rp, *nrp, *drp;
	DN *dp;
	int loops;
	char *procname;
	char nname[Domlen];

	if(status)
		*status = 0;

	if(depth > 12)			/* in a recursive loop? */
		return nil;

	procname = procgetname();
	/*
	 *  hack for systems that don't have resolve search
	 *  lists.  Just look up the simple name in the database.
	 */
	if(!rooted && strchr(name, '.') == nil){
		rp = nil;
		drp = domainlist(class);
		for(nrp = drp; rp == nil && nrp != nil; nrp = nrp->next){
			snprint(nname, sizeof nname, "%s.%s", name,
				nrp->ptr->name);
			rp = dnresolve(nname, class, type, req, cn, depth+1,
				recurse, rooted, status);
			rrfreelist(rrremneg(&rp));
		}
		if(drp != nil)
			rrfreelist(drp);
		procsetname("%s", procname);
		free(procname);
		return rp;
	}

	/*
	 *  try the name directly
	 */
	rp = dnresolve1(name, class, type, req, depth, recurse);
	if(rp == nil && (dp = idnlookup(name, class, 0)) != nil) {
		/*
		 * try it as a canonical name if we weren't told
		 * that the name didn't exist
		 */
		if(type != Tptr && dp->respcode != Rname)
			for(loops = 0; rp == nil && loops < Maxretries; loops++){
				/* retry cname, then the actual type */
				rp = dnresolve1(name, class, Tcname, req,
					depth, recurse);
				if(rp == nil)
					break;

				/* rp->host == nil shouldn't happen, but does */
				if(rp->negative || rp->host == nil){
					rrfreelist(rp);
					rp = nil;
					break;
				}

				name = rp->host->name;
				if(cn)
					rrcat(cn, rp);
				else
					rrfreelist(rp);

				rp = dnresolve1(name, class, type, req,
					depth, recurse);
			}

		/* distinction between not found and not good */
		if(rp == nil && status != nil && dp->respcode != Rok)
			*status = dp->respcode;
	}
	procsetname("%s", procname);
	free(procname);
	return randomize(rp);
}

static void
queryinit(Query *qp, DN *dp, int type, Request *req)
{
	assert(dp && dp->magic == DNmagic);

	memset(qp, 0, sizeof *qp);
	qp->udpfd = qp->tcpfd = qp->tcpctlfd = -1;
	qp->dp = dp;
	qp->type = type;
	if (qp->type != type)
		dnslog("queryinit: bogus type %d", type);
	qp->nsrp = nil;
	qp->dest = qp->curdest = nil;
	qp->prev = req->aux;
	qp->req = req;
	req->aux = qp;
}

static void
querydestroy(Query *qp)
{
	if(qp->req->aux == qp)
		qp->req->aux = qp->prev;
	/* leave udpfd open */
	if (qp->tcpfd >= 0)
		close(qp->tcpfd);
	if (qp->tcpctlfd >= 0) {
		hangup(qp->tcpctlfd);
		close(qp->tcpctlfd);
	}
	memset(qp, 0, sizeof *qp);	/* prevent accidents */
	qp->udpfd = qp->tcpfd = qp->tcpctlfd = -1;
}

/*
 * if the response to a query hasn't arrived within 100 ms.,
 * it's unlikely to arrive at all.  after 1 s., it's really unlikely.
 * queries for missing RRs are likely to produce time-outs rather than
 * negative responses, so cname and aaaa queries are likely to time out,
 * thus we don't wait very long for them.
 */
static void
notestats(vlong start, int tmout, int type)
{
	qlock(&stats);
	if (tmout) {
		stats.tmout++;
		if (type == Taaaa)
			stats.tmoutv6++;
		else if (type == Tcname)
			stats.tmoutcname++;
	} else {
		long wait10ths = NS2MS(nsec() - start) / 100;

		if (wait10ths <= 0)
			stats.under10ths[0]++;
		else if (wait10ths >= nelem(stats.under10ths))
			stats.under10ths[nelem(stats.under10ths) - 1]++;
		else
			stats.under10ths[wait10ths]++;
	}
	qunlock(&stats);
}

static void
noteinmem(void)
{
	qlock(&stats);
	stats.answinmem++;
	qunlock(&stats);
}

/* netquery with given name servers, free ns rrs when done */
static int
netqueryns(Query *qp, int depth, RR *nsrp)
{
	int rv;

	if(nsrp == nil)
		return Answnone;
	qp->nsrp = nsrp;
	rv = netquery(qp, depth);
	qp->nsrp = nil;		/* prevent accidents */
	rrfreelist(nsrp);
	return rv;
}

static RR*
issuequery(Query *qp, char *name, int class, int depth, int recurse)
{
	char *cp;
	DN *nsdp;
	RR *rp, *nsrp, *dbnsrp;

	/*
	 *  if we're running as just a resolver, query our
	 *  designated name servers
	 */
	if(cfg.resolver){
		nsrp = randomize(getdnsservers(class));
		if(nsrp != nil)
			if(netqueryns(qp, depth+1, nsrp) > Answnone)
				return rrlookup(qp->dp, qp->type, OKneg);
	}

	/*
 	 *  walk up the domain name looking for
	 *  a name server for the domain.
	 */
	for(cp = name; cp; cp = walkup(cp)){
		/*
		 *  if this is a local (served by us) domain,
		 *  return answer
		 */
		dbnsrp = randomize(dblookup(cp, class, Tns, 0, 0));
		if(dbnsrp && dbnsrp->local){
			rp = dblookup(name, class, qp->type, 1, dbnsrp->ttl);
			rrfreelist(dbnsrp);
			return rp;
		}

		/*
		 *  if recursion isn't set, just accept local
		 *  entries
		 */
		if(recurse == Dontrecurse){
			if(dbnsrp)
				rrfreelist(dbnsrp);
			continue;
		}

		/* look for ns in cache */
		nsdp = idnlookup(cp, class, 0);
		nsrp = nil;
		if(nsdp)
			nsrp = randomize(rrlookup(nsdp, Tns, NOneg));

		/* if the entry timed out, ignore it */
		if(nsrp && !nsrp->db && (long)(nsrp->expire - now) <= 0)
			rrfreelistptr(&nsrp);

		if(nsrp){
			rrfreelistptr(&dbnsrp);

			/* query the name servers found in cache */
			if(netqueryns(qp, depth+1, nsrp) > Answnone)
				return rrlookup(qp->dp, qp->type, OKneg);
		} else if(dbnsrp)
			/* try the name servers found in db */
			if(netqueryns(qp, depth+1, dbnsrp) > Answnone)
				return rrlookup(qp->dp, qp->type, NOneg);
	}
	return nil;
}

static RR*
dnresolve1(char *name, int class, int type, Request *req, int depth,
	int recurse)
{
	Area *area;
	DN *dp;
	RR *rp;
	Query q;

	if(debug)
		dnslog("[%d] dnresolve1 %s %d %d", getpid(), name, type, class);

	/* only class Cin implemented so far */
	if(class != Cin)
		return nil;

	dp = idnlookup(name, class, 1);

	/*
	 *  Try the cache first
	 */
	rp = rrlookup(dp, type, OKneg);
	if(rp)
		if(rp->db){
			/* unauthoritative db entries are hints */
			if(rp->auth) {
				noteinmem();
				if(debug)
					dnslog("[%d] dnresolve1 %s %d %d: auth rr in db",
						getpid(), name, type, class);
				return rp;
			}
		} else
			/* cached entry must still be valid */
			if((long)(rp->expire - now) > 0)
				/* but Tall entries are special */
				if(type != Tall || rp->query == Tall) {
					noteinmem();
					if(debug)
						dnslog("[%d] dnresolve1 %s %d %d: rr not in db",
							getpid(), name, type, class);
					return rp;
				}
	rrfreelist(rp);
	rp = nil;		/* accident prevention */
	USED(rp);

	/*
	 * try the cache for a canonical name. if found punt
	 * since we'll find it during the canonical name search
	 * in dnresolve().
	 */
	if(type != Tcname){
		rp = rrlookup(dp, Tcname, NOneg);
		rrfreelist(rp);
		if(rp){
			if(debug)
				dnslog("[%d] dnresolve1 %s %d %d: rr from rrlookup for non-cname",
					getpid(), name, type, class);
			return nil;
		}
	}

	/*
	 * if the domain name is within an area of ours,
	 * we should have found its data in memory by now.
	 */
	area = inmyarea(dp->name);
	if (area || strncmp(dp->name, "local#", 6) == 0)
		return nil;

	queryinit(&q, dp, type, req);
	rp = issuequery(&q, name, class, depth, recurse);
	querydestroy(&q);

	if(rp){
		if(debug)
			dnslog("[%d] dnresolve1 %s %d %d: rr from query",
				getpid(), name, type, class);
		return rp;
	}

	/* settle for a non-authoritative answer */
	rp = rrlookup(dp, type, OKneg);
	if(rp){
		if(debug)
			dnslog("[%d] dnresolve1 %s %d %d: rr from rrlookup",
				getpid(), name, type, class);
		return rp;
	}

	/* noone answered.  try the database, we might have a chance. */
	rp = dblookup(name, class, type, 0, 0);
	if (rp) {
		if(debug)
			dnslog("[%d] dnresolve1 %s %d %d: rr from dblookup",
				getpid(), name, type, class);
	}else{
		if(debug)
			dnslog("[%d] dnresolve1 %s %d %d: no rr from dblookup; crapped out",
				getpid(), name, type, class);
	}
	return rp;
}

/*
 *  walk a domain name one element to the right.
 *  return a pointer to that element.
 *  in other words, return a pointer to the parent domain name.
 */
char*
walkup(char *name)
{
	char *cp;

	cp = strchr(name, '.');
	if(cp)
		return cp+1;
	else if(*name)
		return "";
	else
		return 0;
}

/*
 *  Get a udp port for sending requests and reading replies.  Put the port
 *  into "headers" mode.
 */
static char *hmsg = "headers";

int
udpport(char *mtpt)
{
	int fd, ctl;
	char ds[64], adir[64];

	/* get a udp port */
	snprint(ds, sizeof ds, "%s/udp!*!0", (mtpt && *mtpt) ? mtpt : "/net");
	ctl = announce(ds, adir);
	if(ctl < 0){
		/* warning("can't get udp port"); */
		return -1;
	}

	/* turn on header style interface */
	if(write(ctl, hmsg, strlen(hmsg)) != strlen(hmsg)){
		close(ctl);
		warning(hmsg);
		return -1;
	}

	/* grab the data file */
	snprint(ds, sizeof ds, "%s/data", adir);
	fd = open(ds, ORDWR);
	close(ctl);
	if(fd < 0)
		warning("can't open udp port %s: %r", ds);
	return fd;
}

void
initdnsmsg(DNSmsg *mp, RR *rp, int flags, ushort reqno)
{
	memset(mp, 0, sizeof *mp);
	mp->flags = flags;
	mp->id = reqno;
	mp->qd = rp;
	if(rp != nil)
		mp->qdcount = 1;
}

/* generate a DNS UDP query packet */
int
mkreq(DN *dp, int type, uchar *buf, int flags, ushort reqno)
{
	DNSmsg m;
	int len;
	Udphdr *uh = (Udphdr*)buf;
	RR *rp;

	/* stuff port number into output buffer */
	memset(uh, 0, sizeof *uh);
	hnputs(uh->rport, 53);

	/* make request and convert it to output format */
	rp = rralloc(type);
	rp->owner = dp;
	initdnsmsg(&m, rp, flags, reqno);
	len = convDNS2M(&m, &buf[Udphdrsize], Maxudp);
	rrfreelist(rp);
	return len;
}

void
freeanswers(DNSmsg *mp)
{
	rrfreelistptr(&mp->qd);
	rrfreelistptr(&mp->an);
	rrfreelistptr(&mp->ns);
	rrfreelistptr(&mp->ar);
	mp->qdcount = mp->ancount = mp->nscount = mp->arcount = 0;
}

/* timed read of reply.  sets srcip.  ibuf must be 64K to handle tcp answers. */
static int
readnet(Query *qp, int medium, uchar *ibuf, uvlong endms, uchar **replyp,
	uchar *srcip)
{
	int len, fd;
	long ms;
	vlong startns = nsec();
	uchar *reply;
	uchar lenbuf[2];

	len = -1;			/* pessimism */
	*replyp = nil;
	memset(srcip, 0, IPaddrlen);
	ms = endms - NS2MS(startns);
	if (ms <= 0)
		return -1;		/* taking too long */

	reply = ibuf;
	alarm(ms);
	if (medium == Udp)
		if (qp->udpfd < 0)
			dnslog("readnet: qp->udpfd closed");
		else {
			len = read(qp->udpfd, ibuf, Udphdrsize+Maxudpin);
			alarm(0);
			notestats(startns, len < 0, qp->type);
			if (len >= IPaddrlen)
				memmove(srcip, ibuf, IPaddrlen);
			if (len >= Udphdrsize) {
				len   -= Udphdrsize;
				reply += Udphdrsize;
			}
		}
	else {
		if (!qp->tcpset)
			dnslog("readnet: tcp params not set");
		fd = qp->tcpfd;
		if (fd < 0)
			dnslog("readnet: %s: tcp fd unset for dest %I",
				qp->dp->name, qp->tcpip);
		else if (readn(fd, lenbuf, 2) != 2) {
			dnslog("readnet: short read of 2-byte tcp msg size from %I",
				qp->tcpip);
			/* probably a time-out */
			notestats(startns, 1, qp->type);
		} else {
			len = lenbuf[0]<<8 | lenbuf[1];
			if (readn(fd, ibuf, len) != len) {
				dnslog("readnet: short read of tcp data from %I",
					qp->tcpip);
				/* probably a time-out */
				notestats(startns, 1, qp->type);
				len = -1;
			}
		}
		memmove(srcip, qp->tcpip, IPaddrlen);
	}
	alarm(0);
	*replyp = reply;
	return len;
}

/*
 *  read replies to a request and remember the rrs in the answer(s).
 *  ignore any of the wrong type.
 *  wait at most until endms.
 */
static int
readreply(Query *qp, int medium, ushort req, uchar *ibuf, DNSmsg *mp,
	uvlong endms)
{
	int len;
	char *err;
	char tbuf[32];
	uchar *reply;
	uchar srcip[IPaddrlen];
	RR *rp;

	for (; timems() < endms &&
	    (len = readnet(qp, medium, ibuf, endms, &reply, srcip)) >= 0;
	    freeanswers(mp)){
		/* convert into internal format  */
		memset(mp, 0, sizeof *mp);
		err = convM2DNS(reply, len, mp, nil);
		if (mp->flags & Ftrunc) {
			free(err);
			freeanswers(mp);
			/* notify our caller to retry the query via tcp. */
			return -1;
		} else if(err){
			dnslog("readreply: %s: input err, len %d: %s: %I",
				qp->dp->name, len, err, srcip);
			free(err);
			continue;
		}
		if(debug)
			logreply(qp->req->id, srcip, mp);

		/* answering the right question? */
		if(mp->id != req)
			dnslog("%d: id %d instead of %d: %I", qp->req->id,
				mp->id, req, srcip);
		else if(mp->qd == 0)
			dnslog("%d: no question RR: %I", qp->req->id, srcip);
		else if(mp->qd->owner != qp->dp)
			dnslog("%d: owner %s instead of %s: %I", qp->req->id,
				mp->qd->owner->name, qp->dp->name, srcip);
		else if(mp->qd->type != qp->type)
			dnslog("%d: qp->type %d instead of %d: %I",
				qp->req->id, mp->qd->type, qp->type, srcip);
		else {
			/* remember what request this is in answer to */
			for(rp = mp->an; rp; rp = rp->next)
				rp->query = qp->type;
			return 0;
		}
	}
	if (timems() >= endms) {
		;				/* query expired */
	} else if (0) {
		/* this happens routinely when a read times out */
		dnslog("readreply: %s type %s: ns %I read error or eof "
			"(returned %d): %r", qp->dp->name, rrname(qp->type,
			tbuf, sizeof tbuf), srcip, len);
		if (medium == Udp)
			for (rp = qp->nsrp; rp != nil; rp = rp->next)
				if (rp->type == Tns)
					dnslog("readreply: %s: query sent to "
						"ns %s", qp->dp->name,
						rp->host->name);
	}
	memset(mp, 0, sizeof *mp);
	return -1;
}

/*
 *	return non-0 if first list includes second list
 */
int
contains(RR *rp1, RR *rp2)
{
	RR *trp1, *trp2;

	for(trp2 = rp2; trp2; trp2 = trp2->next){
		for(trp1 = rp1; trp1; trp1 = trp1->next)
			if(trp1->type == trp2->type)
			if(trp1->host == trp2->host)
			if(trp1->owner == trp2->owner)
				break;
		if(trp1 == nil)
			return 0;
	}
	return 1;
}


/*
 *  return multicast version if any
 */
int
ipisbm(uchar *ip)
{
	if(isv4(ip)){
		if (ip[IPv4off] >= 0xe0 && ip[IPv4off] < 0xf0 ||
		    ipcmp(ip, IPv4bcast) == 0)
			return 4;
	} else
		if(ip[0] == 0xff)
			return 6;
	return 0;
}

static int
queryloops(Query *qp, RR *rp)
{
	DN *ns = rp->host;

	/*
	 *  looking up a server under itself
	 */
	if(subsume(rp->owner->name, ns->name))
		return 1;

	/*
	 *  cycle on name servers refering
	 *  to each another.
	 */
	for(; qp; qp = qp->prev)
		if(qp->dp == ns)
			return 1;

	return 0;
}

/*
 *  Get next server type address(es) into qp->dest[nd] and beyond
 */
static int
serveraddrs(Query *qp, int nd, int depth, int type)
{
	RR *rp, *arp, *trp;
	ulong mark;
	Dest *p;

	if(nd >= Maxdest)		/* dest array is full? */
		return Maxdest;

	/*
	 *  look for a server whose address we already know.
	 *  if we find one, mark it so we ignore this on
	 *  subsequent passes.
	 */
	mark = 1UL<<type;
	arp = nil;
	for(rp = qp->nsrp; rp; rp = rp->next){
		assert(rp->magic == RRmagic);
		if(rp->marker & mark)
			continue;
		arp = rrlookup(rp->host, type, NOneg);
		if(arp){
			rp->marker |= mark;
			break;
		}
		arp = dblookup(rp->host->name, Cin, type, 0, 0);
		if(arp){
			rp->marker |= mark;
			break;
		}
	}

	/*
	 *  if the cache and database lookup didn't find any new
	 *  server addresses, try resolving one via the network.
	 *  Mark any we try to resolve so we don't try a second time.
	 */
	if(arp == nil){
		for(rp = qp->nsrp; rp; rp = rp->next)
			if((rp->marker & mark) == 0)
			if(queryloops(qp, rp))
				/*
				 * give up as we should have got the address
				 * by higher up nameserver when recursing
				 * down, or will be queried when recursing up.
				 */
				return nd;

		for(rp = qp->nsrp; rp; rp = rp->next){
			if(rp->marker & mark)
				continue;
			rp->marker |= mark;
			arp = dnresolve(rp->host->name, Cin, type, qp->req, 0,
				depth+1, Recurse, 1, 0);
			rrfreelist(rrremneg(&arp));
			if(arp)
				break;
		}
	}

	/* use any addresses that we found */
	for(trp = arp; trp && nd < Maxdest; trp = trp->next){
		p = &qp->dest[nd];
		memset(p, 0, sizeof *p);
		if(parseip(p->a, trp->ip->name) == -1)
			continue;

		/*
		 * straddling servers can reject all nameservers if they are all
		 * inside, so be sure to list at least one outside ns at
		 * the end of the ns list in /lib/ndb for `dom='.
		 */
		if (ipisbm(p->a) ||
		    cfg.straddle && !insideaddr(qp->dp->name) && insidens(p->a))
			continue;
		p->nx = 0;
		p->n = nil;
		p->s = trp->owner;
		for(rp = qp->nsrp; rp; rp = rp->next){
			if(rp->host == p->s){
				p->n = rp;
				break;
			}
		}
		p->code = Rtimeout;
		nd++;
	}
	rrfreelist(arp);
	return nd;
}

/*
 *  cache negative responses
 */
static void
cacheneg(DN *dp, int type, int rcode, RR *soarr)
{
	RR *rp;
	DN *soaowner;
	ulong ttl;

	qlock(&stats);
	stats.negcached++;
	qunlock(&stats);

	/* no cache time specified, don't make anything up */
	if(soarr != nil){
		if(soarr->next != nil)
			rrfreelistptr(&soarr->next);
		soaowner = soarr->owner;
	} else
		soaowner = nil;

	/* the attach can cause soarr to be freed so mine it now */
	if(soarr != nil && soarr->soa != nil)
		ttl = soarr->soa->minttl;
	else
		ttl = 5*Min;

	/* add soa and negative RR to the database */
	rrattach(soarr, Authoritative);

	rp = rralloc(type);
	rp->owner = dp;
	rp->negative = 1;
	rp->negsoaowner = soaowner;
	rp->negrcode = rcode;
	rp->ttl = ttl;
	rrattach(rp, Authoritative);
}

static int
setdestoutns(Dest *p, int n)
{
	memset(p, 0, sizeof *p);
	if (outsidensip(n, p->a) < 0){
		if (n == 0)
			dnslog("[%d] no outside-ns in ndb", getpid());
		return -1;
	}
	p->s = dnlookup("outside-ns-ips", Cin, 1);
	return 0;
}

/*
 * issue query via UDP or TCP as appropriate.
 * for TCP, returns with qp->tcpip set from udppkt header.
 */
static int
mydnsquery(Query *qp, int medium, uchar *udppkt, int len)
{
	int rv, nfd;
	char conndir[40], addr[128];
	uchar belen[2];
	NetConnInfo *nci;

	rv = -1;
	if (myip(udppkt))
		return rv;
	switch (medium) {
	case Udp:
		nfd = dup(qp->udpfd, -1);
		if (nfd < 0) {
			warning("mydnsquery: qp->udpfd %d: %r", qp->udpfd);
			close(qp->udpfd);	/* ensure it's closed */
			qp->udpfd = -1;		/* poison it */
			break;
		}
		close(nfd);

		if (qp->udpfd < 0)
			dnslog("mydnsquery: qp->udpfd %d closed", qp->udpfd);
		else {
			if (write(qp->udpfd, udppkt, len+Udphdrsize) !=
			    len+Udphdrsize)
				warning("sending udp msg: %r");
			else {
				qlock(&stats);
				stats.qsent++;
				qunlock(&stats);
				rv = 0;
			}
		}
		break;
	case Tcp:
		/* send via TCP & keep fd around for reply */
		memmove(qp->tcpip, udppkt, sizeof qp->tcpip);
		snprint(addr, sizeof addr, "%s/tcp!%I!dns",
			(mntpt && *mntpt) ? mntpt : "/net", udppkt);
		alarm(10*1000);
		qp->tcpfd = dial(addr, nil, conndir, &qp->tcpctlfd);
		alarm(0);
		if (qp->tcpfd < 0) {
			dnslog("can't dial %s: %r", addr);
			break;
		}
		nci = getnetconninfo(conndir, qp->tcpfd);
		if (nci) {
			parseip(qp->tcpip, nci->rsys);
			freenetconninfo(nci);
		} else
			dnslog("mydnsquery: getnetconninfo failed");
		qp->tcpset = 1;

		belen[0] = len >> 8;
		belen[1] = len;
		if (write(qp->tcpfd, belen, 2) != 2 ||
		    write(qp->tcpfd, udppkt + Udphdrsize, len) != len)
			warning("sending tcp msg: %r");
		else
			rv = 0;
		break;
	}
	return rv;
}

/*
 * send query to all UDP destinations or one TCP destination,
 * taken from obuf (udp packet) header
 */
static int
xmitquery(Query *qp, int medium, int depth, uchar *obuf, int inns, int len)
{
	int n;
	char buf[32];
	Dest *p;

	if(timems() >= qp->req->aborttime)
		return -1;

	/*
	 * if we send tcp query, we just take the dest ip address from
	 * the udp header placed there by tcpquery().
	 */
	if (medium == Tcp) {
		procsetname("tcp %sside query for %s %s", (inns? "in": "out"),
			qp->dp->name, rrname(qp->type, buf, sizeof buf));
		if(mydnsquery(qp, medium, obuf, len) < 0) /* sets qp->tcpip from obuf */
			return -1;
		if(debug)
			logsend(qp->req->id, depth, qp->tcpip, "", qp->dp->name,
				qp->type);
		return 0;
	}

	/*
	 * get a nameserver address if we need one.
	 * we're to transmit to more destinations than we currently have,
	 * so get another.
	 */
	p = qp->dest;
	n = qp->curdest - p;
	if (qp->ndest > n) {
		/* populates qp->dest with v4 and v6 addresses. */
		n = serveraddrs(qp, n, depth, Ta);
		n = serveraddrs(qp, n, depth, Taaaa);
		if (n == 0 && cfg.straddle && cfg.inside) {
			/* get ips of "outside-ns-ips" */
			while(n < Maxdest){
				if (setdestoutns(&qp->dest[n], n) < 0)
					break;
				n++;
			}
			if(n == 0)
				dnslog("xmitquery: %s: no outside-ns nameservers",
					qp->dp->name);
		}
		qp->curdest = &qp->dest[n];
	}

	for(n = 0; p < &qp->dest[qp->ndest] && p < qp->curdest; p++){
		/* skip destinations we've finished with */
		if(p->nx >= Maxtrans)
			continue;
		/* exponential backoff of requests */
		if((1<<p->nx) > qp->ndest)
			continue;

		if(ipcmp(p->a, IPnoaddr) == 0)
			continue;		/* mistake */

		procsetname("udp %sside query to %I/%s %s %s",
			(inns? "in": "out"), p->a, p->s->name,
			qp->dp->name, rrname(qp->type, buf, sizeof buf));
		if(debug)
			logsend(qp->req->id, depth, p->a, p->s->name,
				qp->dp->name, qp->type);

		/* fill in UDP destination addr & send it */
		memmove(obuf, p->a, sizeof p->a);
		if(mydnsquery(qp, medium, obuf, len) == 0)
			n++;
		p->nx++;
	}

	return n == 0 ? -1 : 0;
}

/* is mp a cachable negative response (with Rname set)? */
static int
isnegrname(DNSmsg *mp)
{
	/* TODO: could add || cfg.justforw to RHS of && */
	return mp->an == nil && (mp->flags & Rmask) == Rname;
}

static int
filterhints(RR *rp, void *arg)
{
	RR *nsrp;

	if(rp->type != Ta && rp->type != Taaaa)
		return 0;

	for(nsrp = arg; nsrp; nsrp = nsrp->next)
		if(nsrp->type == Tns && rp->owner == nsrp->host)
			return 1;

	return 0;
}

static int
filterauth(RR *rp, void *arg)
{
	Dest *dest;
	RR *nsrp;

	dest = arg;
	nsrp = dest->n;
	if(nsrp == nil)
		return 0;

	if(rp->type == Tsoa && rp->owner != nsrp->owner
	&& !subsume(nsrp->owner->name, rp->owner->name)
	&& strncmp(nsrp->owner->name, "local#", 6) != 0)
		return 1;

	if(rp->type != Tns)
		return 0;

	if(rp->owner != nsrp->owner
	&& !subsume(nsrp->owner->name, rp->owner->name)
	&& strncmp(nsrp->owner->name, "local#", 6) != 0)
		return 1;

	return baddelegation(rp, nsrp, dest->a);
}

static void
reportandfree(RR *l, char *note, Dest *p)
{
	RR *rp;

	while(rp = l){
		l = l->next;
		rp->next = nil;
		if(debug)
			dnslog("ignoring %s from %I/%s: %R",
				note, p->a, p->s->name, rp);
		rrfree(rp);
	}
}

/* returns Answerr (-1) on errors, else number of answers, which can be zero. */
static int
procansw(Query *qp, DNSmsg *mp, int depth, Dest *p)
{
	int rv;
	char buf[32];
	DN *ndp;
	Query nq;
	RR *tp, *soarr;

	if(mp->an == nil)
		stats.negans++;

	/* ignore any error replies */
	switch(mp->flags & Rmask){
	case Rrefused:
	case Rserver:
		stats.negserver++;
		freeanswers(mp);
		p->code = Rserver;
		return Answerr;
	}

	/* ignore any bad delegations */
	if((tp = rrremfilter(&mp->ns, filterauth, p)) != 0)
		reportandfree(tp, "bad delegation", p);

	/* remove any soa's from the authority section */
	soarr = rrremtype(&mp->ns, Tsoa);

	/* only nameservers remaining */
	if((tp = rrremtype(&mp->ns, Tns)) != 0){
		reportandfree(mp->ns, "non-nameserver", p);
		mp->ns = tp;
	}

	/* remove answers not related to the question. */
	if((tp = rrremowner(&mp->an, qp->dp)) != 0){
		reportandfree(mp->an, "wrong subject answer", p);
		mp->an = tp;
	}
	if(qp->type != Tall){
		if((tp = rrremtype(&mp->an, qp->type)) != 0){
			reportandfree(mp->an, "wrong type answer", p);
			mp->an = tp;
		}
	}

	/* incorporate answers */
	unique(mp->an);
	unique(mp->ns);
	unique(mp->ar);

	if(mp->an){
		/*
		 * only use cname answer when returned. some dns servers
		 * attach (potential) spam hint address records which poisons
		 * the cache.
		 */
		if((tp = rrremtype(&mp->an, Tcname)) != 0){
			reportandfree(mp->an, "ip in cname answer", p);
			mp->an = tp;
		}
		rrattach(mp->an, (mp->flags & Fauth) != 0);
	}
	if(mp->ar){
		/* restrict hints to address rr's for nameservers only */
		if((tp = rrremfilter(&mp->ar, filterhints, mp->ns)) != 0){
			reportandfree(mp->ar, "hint", p);
			mp->ar = tp;
		}
		rrattach(mp->ar, Notauthoritative);
	}
	if(mp->ns && !cfg.justforw){
		ndp = mp->ns->owner;
		rrattach(mp->ns, Notauthoritative);
	} else {
		ndp = nil;
		rrfreelistptr(&mp->ns);
		mp->nscount = 0;
	}

	/* free the question */
	if(mp->qd) {
		rrfreelistptr(&mp->qd);
		mp->qdcount = 0;
	}

	/*
	 *  Any reply from an authoritative server
	 *  that does not provide more nameservers,
	 *  or a positive reply terminates the search.
	 *  A negative response now also terminates the search.
	 */
	if(mp->an || (mp->flags & Fauth) && mp->ns == nil){
		if(isnegrname(mp))
			qp->dp->respcode = Rname;
		else
			qp->dp->respcode = Rok;

		/*
		 *  cache any negative responses, free soarr.
		 *  negative responses need not be authoritative:
		 *  they can legitimately come from a cache.
		 */
		if( /* (mp->flags & Fauth) && */ mp->an == nil)
			cacheneg(qp->dp, qp->type, (mp->flags & Rmask), soarr);
		else
			rrfreelist(soarr);
		return 1;
	} else if (isnegrname(mp)) {
		qp->dp->respcode = Rname;
		/*
		 *  cache negative response.
		 *  negative responses need not be authoritative:
		 *  they can legitimately come from a cache.
		 */
		cacheneg(qp->dp, qp->type, (mp->flags & Rmask), soarr);
		return 1;
	}
	stats.negnorname++;
	rrfreelist(soarr);

	/*
	 *  if we've been given better name servers, recurse.
	 *  if we're a pure resolver, don't recurse, we have
	 *  to forward to a fixed set of named servers.
	 */
	if(mp->ns == nil || cfg.resolver && cfg.justforw)
		return Answnone;
	tp = rrlookup(ndp, Tns, NOneg);
	if(contains(qp->nsrp, tp)){
		rrfreelist(tp);
		return Answnone;
	}
	procsetname("recursive query for %s %s", qp->dp->name,
		rrname(qp->type, buf, sizeof buf));

	queryinit(&nq, qp->dp, qp->type, qp->req);
	rv = netqueryns(&nq, depth+1, tp);
	querydestroy(&nq);

	return rv;
}

/*
 * send a query via tcp to a single address (from ibuf's udp header)
 * and read the answer(s) into mp->an.
 */
static int
tcpquery(Query *qp, DNSmsg *mp, int depth, uchar *ibuf, uchar *obuf, int len,
	ulong waitms, int inns, ushort req)
{
	int rv = 0;
	uvlong endms;

	endms = timems() + waitms;
	if(endms > qp->req->aborttime)
		endms = qp->req->aborttime;

	if (0)
		dnslog("%s: udp reply truncated; retrying query via tcp to %I",
			qp->dp->name, qp->tcpip);

	memmove(obuf, ibuf, IPaddrlen);		/* send back to respondent */
	memset(mp, 0, sizeof *mp);
	if (xmitquery(qp, Tcp, depth, obuf, inns, len) < 0 ||
	    readreply(qp, Tcp, req, ibuf, mp, endms) < 0)
		rv = -1;
	if (qp->tcpfd >= 0) {
		hangup(qp->tcpctlfd);
		close(qp->tcpctlfd);
		close(qp->tcpfd);
	}
	qp->tcpfd = qp->tcpctlfd = -1;

	return rv;
}

/*
 *  query name servers.  fill in obuf with on-the-wire representation of a
 *  DNSmsg derived from qp.  if the name server returns a pointer to another
 *  name server, recurse.
 */
static int
queryns(Query *qp, int depth, uchar *ibuf, uchar *obuf, ulong waitms, int inns)
{
	int ndest, len, replywaits, rv, flag;
	ushort req;
	uvlong endms;
	char buf[32];
	uchar srcip[IPaddrlen];
	Dest *p, *np, dest[Maxdest];

	req = rand();

	/* request recursion only for local dns servers */
	flag = Oquery;
	if(strncmp(qp->nsrp->owner->name, "local#", 6) == 0)
		flag |= Frecurse;

	/* pack request into a udp message */
	len = mkreq(qp->dp, qp->type, obuf, flag, req);

	/* no server addresses yet */
	memset(dest, 0, sizeof dest);
	qp->curdest = qp->dest = dest;

	/*
	 *  transmit udp requests and wait for answers.
	 *  at most Maxtrans attempts to each address.
	 *  each cycle send one more message than the previous.
	 *  retry a query via tcp if its response is truncated.
	 */
	for(ndest = 2; ndest < Maxdest; ndest += 2){
		qp->ndest = ndest;
		qp->tcpset = 0;
		if (xmitquery(qp, Udp, depth, obuf, inns, len) < 0)
			break;

		endms = timems() + waitms;
		if(endms > qp->req->aborttime)
			endms = qp->req->aborttime;

		for(replywaits = 0; replywaits < ndest; replywaits++){
			DNSmsg m;

			procsetname("reading %sside reply from %I: %s %s from %s",
				(inns? "in": "out"), obuf, qp->dp->name,
				rrname(qp->type, buf, sizeof buf), qp->req->from);

			/* read udp answer into m */
			if (readreply(qp, Udp, req, ibuf, &m, endms) >= 0)
				memmove(srcip, ibuf, IPaddrlen);
			else if (!(m.flags & Ftrunc)) {
				freeanswers(&m);
				break;		/* timed out on this dest */
			} else {
				/* whoops, it was truncated! ask again via tcp */
				freeanswers(&m);
				rv = tcpquery(qp, &m, depth, ibuf, obuf, len,
					waitms, inns, req);  /* answer in m */
				if (rv < 0) {
					freeanswers(&m);
					break;		/* failed via tcp too */
				}
				memmove(srcip, qp->tcpip, IPaddrlen);
			}

			/* find responder */
			if(debug)
				dnslog("queryns got reply from %I", srcip);
			for(p = qp->dest; p < qp->curdest; p++)
				if(ipcmp(p->a, srcip) == 0)
					break;
			if(p >= qp->curdest){
				dnslog("response from %I but no destination", srcip);
				continue;
			}

			/* remove all addrs of responding server from list */
			for(np = qp->dest; np < qp->curdest; np++)
				if(np->s == p->s)
					np->nx = Maxtrans;

			/* free or incorporate RRs in m */
			rv = procansw(qp, &m, depth, p);
			if (rv > Answnone) {
				qp->dest = qp->curdest = nil; /* prevent accidents */
				return rv;
			}
		}
	}

	/* if all servers returned failure, propagate it */
	qp->dp->respcode = Rserver;
	for(p = dest; p < qp->curdest; p++)
		if(p->code != Rserver)
			qp->dp->respcode = Rok;

//	if (qp->dp->respcode)
//		dnslog("queryns setting Rserver for %s", qp->dp->name);

	qp->dest = qp->curdest = nil;		/* prevent accidents */
	return Answnone;
}

/*
 * in principle we could use a single descriptor for a udp port
 * to send all queries and receive all the answers to them,
 * but we'd have to sort out the answers by dns-query id.
 */
static int
udpquery(Query *qp, char *mntpt, int depth, int patient, int inns)
{
	int fd, rv;
	uchar *obuf, *ibuf;

	/* use alloced buffers rather than ones from the stack */
	ibuf = emalloc(64*1024);		/* max. tcp reply size */
	obuf = emalloc(Maxudp+Udphdrsize);

	fd = udpport(mntpt);
	if (fd < 0) {
		dnslog("can't get udpport for %s query of name %s: %r",
			mntpt, qp->dp->name);
		rv = -1;
		goto Out;
	}
	qp->udpfd = fd;
	rv = queryns(qp, depth, ibuf, obuf, 500UL<<(patient != 0), inns);
	qp->udpfd = -1;
	close(fd);

Out:
	free(obuf);
	free(ibuf);
	return rv;
}

/*
 * look up (qp->dp->name, qp->type) rr in dns,
 * using nameservers in qp->nsrp.
 */
static int
netquery(Query *qp, int depth)
{
	int rv, triedin, inname;
	RR *rp;

	rv = Answnone;			/* pessimism */
	if(depth > 12)			/* in a recursive loop? */
		return Answnone;

	slave(qp->req);

	/*
	 * slave might have forked.  if so, the parent process longjmped to
	 * req->mret; we're usually the child slave, but if there are too
	 * many children already, we're still the same process. under no
	 * circumstances block the 9p loop.
	 */
	if(!qp->req->isslave && strcmp(qp->req->from, "9p") == 0)
		return Answnone;

	procsetname("netquery: %s", qp->dp->name);

	/* prepare server RR's for incremental lookup */
	for(rp = qp->nsrp; rp; rp = rp->next)
		rp->marker = 0;

	triedin = 0;

	/*
	 * normal resolvers and servers will just use mntpt for all addresses,
	 * even on the outside.  straddling servers will use mntpt (/net)
	 * for inside addresses and /net.alt for outside addresses,
	 * thus bypassing other inside nameservers.
	 */
	inname = insideaddr(qp->dp->name);
	if (!cfg.straddle || inname) {
		rv = udpquery(qp, mntpt, depth, Hurry, (cfg.inside? Inns: Outns));
		triedin = 1;
	}

	/*
	 * if we're still looking, are inside, and have an outside domain,
	 * try it on our outside interface, if any.
	 */
	if (rv == Answnone && cfg.inside && !inname) {
		if (triedin)
			dnslog(
	   "[%d] netquery: internal nameservers failed for %s; trying external",
				getpid(), qp->dp->name);

		/* prepare server RR's for incremental lookup */
		for(rp = qp->nsrp; rp; rp = rp->next)
			rp->marker = 0;

		rv = udpquery(qp, "/net.alt", depth, Patient, Outns);
	}

	return rv;
}

int
seerootns(void)
{
	int rv;
	char root[] = "";
	Request req;
	RR *rr, *nsrp;
	Query q;

	memset(&req, 0, sizeof req);
	req.isslave = 1;
	req.aborttime = timems() + Maxreqtm;
	req.from = "internal";
	queryinit(&q, dnlookup(root, Cin, 1), Tns, &req);
	nsrp = randomize(dblookup(root, Cin, Tns, 0, 0));
	for (rr = nsrp; rr != nil; rr = rr->next)
		dnslog("seerootns query nsrp: %R", rr);
	rv = netqueryns(&q, 0, nsrp);		/* lookup ". ns" using nsrp */
	querydestroy(&q);
	return rv;
}
