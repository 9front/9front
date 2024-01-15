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

	/*
	 * these are the old values; we're trying longer timeouts now
	 * primarily for the benefit of remote nameservers querying us
	 * during times of bad connectivity.
	 */
	Maxtrans=	5,	/* maximum transmissions to a server */
	Maxretries=	10,	/* cname+actual resends: was 32; have pity on user */
};

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

	int	depth;
	ushort	id;		/* request id */

	RR	*nsrp;		/* name servers to consult */
};

static RR*	dnresolve1(char*, int, int, Request*, int, int);
static int	netquery(Query *);

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

	snprint(buf, sizeof buf, "/proc/%d/args", getpid());
	if((fd = open(buf, OREAD|OCEXEC)) < 0)
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
	int recurse, int rooted, int *rcode)
{
	RR *rp, *nrp, *drp;
	DN *dp;
	int loops;
	char *procname;
	char nname[Domlen];

	if(rcode)
		*rcode = Rok;

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
				recurse, rooted, rcode);
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
		if(rp == nil && rcode != nil && dp->respcode != Rok)
			*rcode = dp->respcode;
	}
	procsetname("%s", procname);
	free(procname);
	return randomize(rp);
}

static void
initquery(Query *qp, DN *dp, int type, Request *req, int depth)
{
	assert(dp != nil);

	memset(qp, 0, sizeof *qp);
	qp->dp = dp;
	qp->type = type;
	qp->nsrp = nil;
	qp->depth = depth;
	qp->prev = req->aux;
	qp->req = req;
	req->aux = qp;
}

static void
exitquery(Query *qp)
{
	if(qp->req->aux == qp)
		qp->req->aux = qp->prev;
	memset(qp, 0, sizeof *qp);	/* prevent accidents */
}

/*
 * if the response to a query hasn't arrived within 100 ms.,
 * it's unlikely to arrive at all.  after 1 s., it's really unlikely.
 * queries for missing RRs are likely to produce time-outs rather than
 * negative responses, so cname and aaaa queries are likely to time out,
 * thus we don't wait very long for them.
 */
static void
notestats(long ms, int tmout, int type)
{
	if (tmout) {
		stats.tmout++;
		if (type == Taaaa)
			stats.tmoutv6++;
		else if (type == Tcname)
			stats.tmoutcname++;
	} else {
		long wait10ths = ms / 100;

		if (wait10ths <= 0)
			stats.under10ths[0]++;
		else if (wait10ths >= nelem(stats.under10ths))
			stats.under10ths[nelem(stats.under10ths) - 1]++;
		else
			stats.under10ths[wait10ths]++;
	}
}

static void
noteinmem(void)
{
	stats.answinmem++;
}

/* netquery with given name servers, free ns rrs when done */
static int
netqueryns(Query *qp, RR *nsrp)
{
	int rv;

	if(nsrp == nil)
		return Answnone;
	qp->nsrp = nsrp;
	rv = netquery(qp);
	qp->nsrp = nil;		/* prevent accidents */
	rrfreelist(nsrp);
	return rv;
}

static RR*
issuequery(Query *qp, char *name, int class, int recurse)
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
			if(netqueryns(qp, nsrp) > Answnone)
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

			/* hack: stop search if ns is authoritative in db */
			if(cfg.cachedb && nsrp->db && nsrp->auth)
				cp = "";

			/* query the name servers found in cache */
			if(netqueryns(qp, nsrp) > Answnone)
				return rrlookup(qp->dp, qp->type, OKneg);
		} else if(dbnsrp)
			/* try the name servers found in db */
			if(netqueryns(qp, dbnsrp) > Answnone)
				return rrlookup(qp->dp, qp->type, NOneg);
	}
	return nil;
}

static RR*
dnresolve1(char *name, int class, int type, Request *req, int depth, int recurse)
{
	Area *area;
	DN *dp;
	RR *rp;
	Query q;

	if(debug)
		dnslog("%d: dnresolve1 %s %d %d",
			req->id, name, type, class);

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
					dnslog("%d: dnresolve1 %s %d %d: auth rr in db",
						req->id, name, type, class);
				return rp;
			}
		} else
			/* cached entry must still be valid */
			if((long)(rp->expire - now) > 0)
				/* but Tall entries are special */
				if(type != Tall || rp->query == Tall) {
					noteinmem();
					if(debug)
						dnslog("%d: dnresolve1 %s %d %d: rr not in db",
							req->id, name, type, class);
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
				dnslog("%d: dnresolve1 %s %d %d: rr from rrlookup for non-cname",
					req->id, name, type, class);
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

	initquery(&q, dp, type, req, depth);
	rp = issuequery(&q, name, class, recurse);
	exitquery(&q);

	if(rp){
		if(debug)
			dnslog("%d: dnresolve1 %s %d %d: rr from query",
				req->id, name, type, class);
		return rp;
	}

	/* settle for a non-authoritative answer */
	rp = rrlookup(dp, type, OKneg);
	if(rp){
		if(debug)
			dnslog("%d: dnresolve1 %s %d %d: rr from rrlookup",
				req->id, name, type, class);
		return rp;
	}

	/* noone answered.  try the database, we might have a chance. */
	rp = dblookup(name, class, type, 0, 0);
	if (rp) {
		if(debug)
			dnslog("%d: dnresolve1 %s %d %d: rr from dblookup",
				req->id, name, type, class);
	}else{
		if(debug)
			dnslog("%d: dnresolve1 %s %d %d: no rr from dblookup; crapped out",
				req->id, name, type, class);
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
 *  Get a udp port for sending requests and reading replies.
 *  Put the port into "headers" mode.
 */
int
udpport(char *mntpt)
{
	static char hmsg[] = "headers";
	static char imsg[] = "ignoreadvice";

	char adir[NETPATHLEN], buf[NETPATHLEN];
	int fd, ctl;

	/* get a udp port */
	snprint(buf, sizeof buf, "%s/udp!*!0", mntpt);
	ctl = announce(buf, adir);
	if(ctl < 0)
		return -1;

	/* turn on header style interface */
	if(write(ctl, hmsg, sizeof(hmsg)-1) < 0){
		warning("can't enable %s on %s: %r", hmsg, adir);
		close(ctl);
		return -1;
	}

	/* ignore ICMP advice */
	write(ctl, imsg, sizeof(imsg)-1);

	/* grab the data file */
	snprint(buf, sizeof buf, "%s/data", adir);
	fd = open(buf, ORDWR|OCEXEC);
	if(fd < 0)
		warning("can't open udp port %s: %r", buf);
	close(ctl);
	return fd;
}

static void
initdnsmsg(DNSmsg *mp, RR *rp, int flags, ushort reqno)
{
	memset(mp, 0, sizeof *mp);
	mp->flags = flags;
	mp->id = reqno;
	mp->qd = rp;
	if(rp != nil)
		mp->qdcount = 1;
}

RR*
getednsopt(DNSmsg *mp, int *rcode)
{
	RR *rp, *x;

	rp = rrremtype(&mp->ar, Topt);
	if(rp == nil)
		return nil;

	mp->arcount--;
	while((x = rp->next) != nil){
		rp->next = x->next;
		rrfree(x);
		mp->arcount--;
		*rcode = Rformat;
	}

	if(rp->eflags & Evers)
		*rcode = Rbadvers;

	if(rp->udpsize < 512)
		rp->udpsize = 512;

	return rp;
}

int
getercode(DNSmsg *mp)
{
	if(mp->edns == nil)
		return mp->flags & Rmask;
	return (mp->flags & 0xF) | (mp->edns->eflags & Ercode) >> 20;
}

void
setercode(DNSmsg *mp, int rcode)
{
	if(mp->edns){
		mp->edns->eflags = (mp->edns->eflags & ~Ercode) | ((rcode << 20) & Ercode);
		rcode &= 0xF;
	}
	mp->flags = (mp->flags & ~Rmask) | (rcode & Rmask);
}

RR*
mkednsopt(void)
{
	RR *rp;

	rp = rralloc(Topt);
	rp->owner = dnlookup("", Cin, 1);
	rp->eflags = 0;

	/*
	 * Advertise a safe UDP response size
	 * instead of Maxudp as that is just
	 * the worst case we can accept.
	 *
	 * 1232 = MTU(1280)-IPv6(40)-UDP(8).
	 */
	rp->udpsize = 1232;

	return rp;
}

/* generate a DNS UDP query packet, return size of request (without Udphdr) */
int
mkreq(DN *dp, int type, uchar *pkt, int flags, ushort id)
{
	Udphdr *uh = (Udphdr*)pkt;
	DNSmsg m;
	RR *rp;
	int len;

	/* stuff port number into output buffer */
	memset(uh, 0, Udphdrsize);
	uh->rport[1] = 53;

	/* make request and convert it to output format */
	rp = rralloc(type);
	rp->owner = dp;
	initdnsmsg(&m, rp, flags, id);
	m.edns = mkednsopt();
	len = convDNS2M(&m, &pkt[Udphdrsize], Maxudp);
	rrfreelist(m.edns);
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

/* timed read of reply. sets srcip if UDP. */
static int
readnet(Query *qp, int medium, int fd, uchar pkt[Maxpkt], uvlong endms,
	uchar **replyp, uchar *srcip)
{
	int len;
	long ms;
	uvlong startms;
	uchar *reply;

	*replyp = nil;

	startms = nowms;
	ms = (long)(endms - startms);
	if (ms < 1)
		return -1;		/* taking too long */

	len = -1;			/* pessimism */
	reply = pkt;
	switch (medium) {
	case Udp:
		alarm(ms);
		len = read(fd, pkt, Udphdrsize + Maxudp);
		alarm(0);
		if(len < 0)
			break;
		if(len <= Udphdrsize){
			len = -1;
			break;
		}
		ipmove(srcip, pkt);
		len   -= Udphdrsize;
		reply += Udphdrsize;
		break;
	case Tcp:
		alarm(ms);
		len = readn(fd, pkt, 2);
		if(len < 0){
			alarm(0);
			break;
		}
		if(len != 2){
			alarm(0);
			dnslog("%d: readnet: short read of 2-byte tcp msg size from %I",
				qp->req->id, srcip);
			len = -1;
			break;
		}
		len = pkt[0]<<8 | pkt[1];
		if(len <= 0 || len > Maxtcp || readn(fd, pkt+2, len) != len){
			alarm(0);
			dnslog("%d: readnet: short read of tcp data from %I",
				qp->req->id, srcip);
			len = -1;
			break;
		}
		alarm(0);
		reply += 2;
		break;
	}

	/* file statistics */
	ms = (long)(timems() - startms);
	notestats(ms, len < 0, qp->type);

	*replyp = reply;
	return len;
}

/*
 *  read replies to a request and remember the rrs in the answer(s).
 *  ignore any of the wrong type for UDP.
 *  wait at most until endms.
 */
static int
readreply(Query *qp, int medium, int fd, uvlong endms,
	DNSmsg *mp, uchar *srcip)
{
	uchar pkt[Maxpkt];
	uchar *reply;
	char *err;
	int len;
	RR *rp;

	for(;;){
		len = readnet(qp, medium, fd, pkt, endms, &reply, srcip);
		if (len < 0)
			break;

		/* convert into internal format  */
		memset(mp, 0, sizeof *mp);
		err = convM2DNS(reply, len, mp, nil);
		if (mp->flags & Ftrunc) {
			free(err);
			return 1;	/* signal truncation */
		}
		if(err){
			dnslog("%d: readreply: input err, len %d: %s from %I",
				qp->req->id, len, err, srcip);
			free(err);
		} else {
			logreply(qp->req->id, "rcvd", srcip, mp);

			/* answering the right question? */
			if(mp->id != qp->id)
				dnslog("%d: id %d instead of %d from %I",
					qp->req->id, mp->id, qp->id, srcip);
			else if(mp->qd == 0)
				dnslog("%d: no question RR from %I", qp->req->id, srcip);
			else if(mp->qd->owner != qp->dp)
				dnslog("%d: owner %s instead of %s from %I", qp->req->id,
					mp->qd->owner->name, qp->dp->name, srcip);
			else if(mp->qd->type != qp->type)
				dnslog("%d: qp->type %d instead of %d from %I",
					qp->req->id, mp->qd->type, qp->type, srcip);
			else {
				/* remember what request this is in answer to */
				for(rp = mp->an; rp; rp = rp->next)
					rp->query = qp->type;
				return 0;
			}
		}
		freeanswers(mp);

		/* only single reply is expected from TCP */
		if(medium == Tcp)
			break;
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
 *  Get next server type address(es) into dest[nd] and beyond
 */
static int
serveraddrs(Query *qp, Dest dest[Maxdest], int nd, int type)
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
			if(strncmp(rp->owner->name, "local#", 6) == 0)
				continue;
			arp = dnresolve(rp->host->name, Cin, type, qp->req, 0,
				qp->depth+1, Recurse, 1, 0);
			rrfreelist(rrremneg(&arp));
			if(arp)
				break;
		}
	}

	/* use any addresses that we found */
	for(trp = arp; trp && nd < Maxdest; trp = trp->next){
		p = &dest[nd];
		memset(p, 0, sizeof *p);
		if(parseip(p->a, trp->ip->name) == -1)
			continue;
		if(ipcmp(p->a, IPnoaddr) == 0)
			continue;
		if(ipisbm(p->a))
			continue;
		if(cfg.serve && myip(p->a))
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

	stats.negcached++;

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
procansw(Query *qp, Dest *p, DNSmsg *mp)
{
	Query nq;
	DN *ndp;
	RR *tp, *soarr;
	int rv, rcode;

	if(mp->an == nil)
		stats.negans++;

	/* get extended rcode */
	rcode = Rok;
	mp->edns = getednsopt(mp, &rcode);
	if(rcode == Rok)
		rcode = getercode(mp);
	rrfreelistptr(&mp->edns);

	/* ignore any error replies */
	switch(rcode){
	case Rformat:
	case Rrefused:
	case Rserver:
	case Rbadvers:
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
		if(mp->an == nil && rcode == Rname)
			qp->dp->respcode = Rname;
		else
			qp->dp->respcode = Rok;

		/*
		 *  cache any negative responses, free soarr.
		 *  negative responses need not be authoritative:
		 *  they can legitimately come from a cache.
		 */
		if( /* (mp->flags & Fauth) && */ mp->an == nil)
			cacheneg(qp->dp, qp->type, rcode, soarr);
		else
			rrfreelist(soarr);
		return 1;
	} else if (mp->an == nil && rcode == Rname) {
		qp->dp->respcode = Rname;
		/*
		 *  cache negative response.
		 *  negative responses need not be authoritative:
		 *  they can legitimately come from a cache.
		 */
		cacheneg(qp->dp, qp->type, rcode, soarr);
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

	initquery(&nq, qp->dp, qp->type, qp->req, qp->depth+1);
	rv = netqueryns(&nq, tp);
	exitquery(&nq);

	return rv;
}

static int
writenet(Query *qp, int medium, int fd, uchar *pkt, int len, Dest *p)
{
	uchar tmp[2];
	int rv;

	logrequest(qp->req->id, qp->depth, "send", p->a, p->s->name,
		qp->dp->name, qp->type);

	rv = -1;
	switch (medium) {
	case Udp:
		/* fill in UDP destination addr & send it */
		ipmove(pkt, p->a);
		if (write(fd, pkt, len+Udphdrsize) != len+Udphdrsize)
			warning("sending udp msg to %I/%s: %r", p->a, p->s->name);
		else {
			stats.qsentudp++;
			rv = 0;
		}
		break;
	case Tcp:
		tmp[0] = pkt[Udphdrsize-2], pkt[Udphdrsize-2] = len >> 8;
		tmp[1] = pkt[Udphdrsize-1], pkt[Udphdrsize-1] = len;
		len += 2;
		if (write(fd, pkt + Udphdrsize-2, len) != len)
			warning("sending tcp msg to %I/%s: %r", p->a, p->s->name);
		else {
			stats.qsenttcp++;
			rv = 0;
		}
		pkt[Udphdrsize-2] = tmp[0];
		pkt[Udphdrsize-1] = tmp[1];
		break;
	}
	return rv;
}

/*
 * send a query via tcp to a single address
 * and read the answer(s) into mp->an.
 */
static int
tcpquery(Query *qp, uchar *pkt, int len, Dest *p, uvlong endms, DNSmsg *mp)
{
	char buf[NETPATHLEN];
	int fd, rv;
	long ms;

	memset(mp, 0, sizeof *mp);

	ms = (long)(endms - nowms);
	if(ms < Minreqtm)
		return -1;	/* takes too long */
	if(ms > Maxtcpdialtm)
		ms = Maxtcpdialtm;

	procsetname("tcp query to %I/%s for %s %s", p->a, p->s->name,
		qp->dp->name, rrname(qp->type, buf, sizeof buf));

	snprint(buf, sizeof buf, "%s/tcp!%I!53", mntpt, p->a);

	alarm(ms);
	fd = dial(buf, nil, nil, nil);
	alarm(0);
	if (fd < 0) {
		dnslog("%d: can't dial %s for %I/%s: %r",
			qp->req->id, buf, p->a, p->s->name);
		return -1;
	}
	rv = writenet(qp, Tcp, fd, pkt, len, p);
	if(rv == 0){
		timems();	/* account for time dialing and sending */
		rv = readreply(qp, Tcp, fd, endms, mp, pkt);
	}
	close(fd);
	return rv;
}

/*
 *  query name servers.  fill in pkt with on-the-wire representation of a
 *  DNSmsg derived from qp. if the name server returns a pointer to another
 *  name server, recurse.
 */
static int
udpqueryns(Query *qp, int fd, uchar *pkt)
{
	Dest dest[Maxdest], *edest, *p, *np;
	int ndest, replywaits, len, flag, rv, n;
	uchar srcip[IPaddrlen];
	char buf[32];
	uvlong endms;
	DNSmsg m;
	RR *rp;

	/* prepare server RR's for incremental lookup */
	for(rp = qp->nsrp; rp; rp = rp->next)
		rp->marker = 0;

	/* request recursion only for local/override dns servers */
	flag = Oquery;
	if(strncmp(qp->nsrp->owner->name, "local#", 6) == 0
	|| strncmp(qp->nsrp->owner->name, "override#", 9) == 0)
		flag |= Frecurse;

	/* pack request into a udp message */
	qp->id = rand();
	len = mkreq(qp->dp, qp->type, pkt, flag, qp->id);

	/* no destination yet */
	edest = dest;

	/*
	 *  transmit udp requests and wait for answers.
	 *  at most Maxtrans attempts to each address.
	 *  each cycle send one more message than the previous.
	 *  retry a query via tcp if its response is truncated.
	 */
	for(ndest = 2; ndest < Maxdest; ndest += 2){
		endms = nowms;
		if((long)(qp->req->aborttime - nowms) < Minreqtm)
			break;

		/*
		 * get a nameserver address if we need one.
		 * we're to transmit to more destinations than we currently have,
		 * so get another.
		 */
		n = edest - dest;
		if (n < ndest) {
			/* populates dest with v4 and v6 addresses. */
			n = serveraddrs(qp, dest, n, Ta);
			n = serveraddrs(qp, dest, n, Taaaa);
			edest = dest + n;
		}

		n = 0;
		for(p = dest; p < edest && p < &dest[ndest]; p++){
			/* skip destinations we've finished with */
			if(p->nx >= Maxtrans)
				continue;
			/* exponential backoff of requests */
			if((1UL<<p->nx) > ndest)
				continue;
			p->nx++;
			if(writenet(qp, Udp, fd, pkt, len, p) < 0)
				continue;
			n++;
		}
		/* nothing left to send to */
		if (n == 0)
			break;

		/* set the timeout for replies */
		endms += 500;
		if(endms > qp->req->aborttime)
			endms = qp->req->aborttime;

		procsetname("reading replies from %I...: %s %s from %s",
			pkt, qp->dp->name,
			rrname(qp->type, buf, sizeof buf), qp->req->from);

		for(replywaits = 0; replywaits < ndest; replywaits++){
			/* read udp answer into m, fill srcip */
			if(readreply(qp, Udp, fd, endms, &m, srcip) < 0)
				break;

			/* find responder */
			for(p = dest; p < edest; p++)
				if(ipcmp(p->a, srcip) == 0)
					break;
			if(p >= edest){
				freeanswers(&m);
				continue;
			}

			/* if response was truncated, try tcp */
			if(m.flags & Ftrunc){
				freeanswers(&m);
				if(tcpquery(qp, pkt, len, p, endms, &m) < 0)
					break;	/* failed via tcp too */
				if(m.flags & Ftrunc){
					freeanswers(&m);
					break;
				}
			}

			/* remove all addrs of responding server from list */
			for(np = dest; np < edest; np++)
				if(np->s == p->s)
					np->nx = Maxtrans;

			/* free or incorporate RRs in m */
			rv = procansw(qp, p, &m);
			if(rv > Answnone)
				return rv;
		}
	}

	/* if all servers returned failure, propagate it */
	qp->dp->respcode = Rserver;
	for(p = dest; p < edest; p++)
		if(p->code != Rserver)
			qp->dp->respcode = Rok;

	return Answnone;
}

/*
 * in principle we could use a single descriptor for a udp port
 * to send all queries and receive all the answers to them,
 * but we'd have to sort out the answers by dns-query id.
 */
static int
udpquery(Query *qp)
{
	int fd, rv;
	uchar *pkt;

	pkt = emalloc(Maxudp+Udphdrsize);
	fd = udpport(mntpt);
	if (fd < 0) {
		dnslog("%d: can't get udpport for %s query of name %s: %r",
			qp->req->id, mntpt, qp->dp->name);
		rv = -1;
		goto Out;
	}
	rv = udpqueryns(qp, fd, pkt);
	close(fd);
Out:
	free(pkt);
	return rv;
}

/*
 * look up (qp->dp->name, qp->type) rr in dns,
 * using nameservers in qp->nsrp.
 */
static int
netquery(Query *qp)
{
	if(qp->depth > 12)			/* in a recursive loop? */
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

	return udpquery(qp);
}
