#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include "ip.h"

typedef struct Icmp {
	uchar	vihl;		/* Version and header length */
	uchar	tos;		/* Type of service */
	uchar	length[2];	/* packet length */
	uchar	id[2];		/* Identification */
	uchar	frag[2];	/* Fragment information */
	uchar	ttl;		/* Time to live */
	uchar	proto;		/* Protocol */
	uchar	ipcksum[2];	/* Header checksum */
	uchar	src[4];		/* Ip source */
	uchar	dst[4];		/* Ip destination */
	uchar	type;
	uchar	code;
	uchar	cksum[2];
	uchar	icmpid[2];
	uchar	seq[2];
	uchar	data[1];
} Icmp;

enum {			/* Packet Types */
	EchoReply	= 0,
	Unreachable	= 3,
	SrcQuench	= 4,
	Redirect	= 5,
	EchoRequest	= 8,
	TimeExceed	= 11,
	InParmProblem	= 12,
	Timestamp	= 13,
	TimestampReply	= 14,
	InfoRequest	= 15,
	InfoReply	= 16,
	AddrMaskRequest = 17,
	AddrMaskReply   = 18,

	Maxtype		= 18,
};

char *icmpnames[Maxtype+1] =
{
[EchoReply]		"EchoReply",
[Unreachable]		"Unreachable",
[SrcQuench]		"SrcQuench",
[Redirect]		"Redirect",
[EchoRequest]		"EchoRequest",
[TimeExceed]		"TimeExceed",
[InParmProblem]		"InParmProblem",
[Timestamp]		"Timestamp",
[TimestampReply]	"TimestampReply",
[InfoRequest]		"InfoRequest",
[InfoReply]		"InfoReply",
[AddrMaskRequest]	"AddrMaskRequest",
[AddrMaskReply  ]	"AddrMaskReply  ",
};

enum {
	IP_ICMPPROTO	= 1,
	ICMP_IPSIZE	= 20,
	ICMP_HDRSIZE	= 8,

	MinAdvise	= IP4HDR+4,	/* minimum needed for us to advise another protocol */
};

enum
{
	InMsgs,
	InErrors,
	OutMsgs,
	CsumErrs,
	LenErrs,
	HlenErrs,

	Nstats,
};

static char *statnames[Nstats] =
{
[InMsgs]	"InMsgs",
[InErrors]	"InErrors",
[OutMsgs]	"OutMsgs",
[CsumErrs]	"CsumErrs",
[LenErrs]	"LenErrs",
[HlenErrs]	"HlenErrs",
};

typedef struct Icmppriv Icmppriv;
struct Icmppriv
{
	ulong	stats[Nstats];

	/* message counts */
	ulong	in[Maxtype+1];
	ulong	out[Maxtype+1];

	Ipht	ht;
};

static void icmpkick(void *x, Block*);

static void
icmpcreate(Conv *c)
{
	c->rq = qopen(64*1024, Qmsg, 0, c);
	c->wq = qbypass(icmpkick, c);
}

char*
icmpconnect(Conv *c, char **argv, int argc)
{
	char *e;

	e = Fsstdconnect(c, argv, argc);
	if(e != nil)
		return e;
	Fsconnected(c, e);

	return nil;
}

int
icmpstate(Conv *c, char *state, int n)
{
	USED(c);
	return snprint(state, n, "%s qin %d qout %d\n",
		"Datagram",
		c->rq ? qlen(c->rq) : 0,
		c->wq ? qlen(c->wq) : 0
	);
}

char*
icmpannounce(Conv *c, char **argv, int argc)
{
	char *e;

	e = Fsstdannounce(c, argv, argc);
	if(e != nil)
		return e;
	Fsconnected(c, nil);

	return nil;
}

void
icmpclose(Conv *c)
{
	qclose(c->rq);
	qclose(c->wq);
	ipmove(c->laddr, IPnoaddr);
	ipmove(c->raddr, IPnoaddr);
	c->lport = 0;
}

static void
icmpkick(void *x, Block *bp)
{
	Conv *c = x;
	Icmp *p;
	Icmppriv *ipriv;

	if(bp == nil)
		return;
	if(BLEN(bp) < ICMP_IPSIZE + ICMP_HDRSIZE){
		freeblist(bp);
		return;
	}
	p = (Icmp *)(bp->rp);
	p->vihl = IP_VER4;
	ipriv = c->p->priv;
	if(p->type <= Maxtype)	
		ipriv->out[p->type]++;
	
	v6tov4(p->dst, c->raddr);
	v6tov4(p->src, c->laddr);
	p->proto = IP_ICMPPROTO;
	hnputs(p->icmpid, c->lport);
	memset(p->cksum, 0, sizeof(p->cksum));
	hnputs(p->cksum, ptclcsum(bp, ICMP_IPSIZE, blocklen(bp) - ICMP_IPSIZE));
	ipriv->stats[OutMsgs]++;
	ipoput4(c->p->f, bp, nil, c->ttl, c->tos, nil);
}

static int
ip4reply(Fs *f, uchar ip4[4])
{
	uchar addr[IPaddrlen];
	int i;

	if(isv4mcast(ip4))
		return 0;
	v4tov6(addr, ip4);
	i = ipforme(f, addr);
	return i == 0 || i == Runi;
}

static int
ip4me(Fs *f, uchar ip4[4])
{
	uchar addr[IPaddrlen];

	if(isv4mcast(ip4))
		return 0;
	v4tov6(addr, ip4);
	return ipforme(f, addr) == Runi;
}

/*
 *  Send a ICMP packet back of type/code/arg in response to packet bp
 *  which was received on ifc. The route hint rh is valid for the
 *  reverse route.
 */
static void
icmpsend(Fs *f, Ipifc *ifc, Block *bp, int type, int code, int arg, Routehint *rh)
{
	Proto	*icmp;
	uchar	ia[IPv4addrlen];
	Block	*nbp;
	Icmp	*np, *p;
	int	sz, osz;

	osz = BLEN(bp);
	if(osz < IP4HDR)
		return;

	p = (Icmp *)bp->rp;
	if(!ip4reply(f, p->dst) || !ip4reply(f, p->src) || !ipv4local(ifc, ia, 0, p->src))
		return;

	netlog(f, Logicmp, "icmpsend %s (%d) %d ia %V -> src %V dst %V\n",
		icmpnames[type], type, code, ia, p->src, p->dst);

	sz = MIN(ICMP_IPSIZE + ICMP_HDRSIZE + osz, v4MINTU);
	nbp = allocb(sz);
	nbp->wp += sz;
	memset(nbp->rp, 0, sz);
	np = (Icmp *)nbp->rp;
	np->vihl = IP_VER4;
	memmove(np->src, ia, sizeof(np->src));
	memmove(np->dst, p->src, sizeof(np->dst));
	memmove(np->data, bp->rp, sz - ICMP_IPSIZE - ICMP_HDRSIZE);
	np->type = type;
	np->code = code;
	np->proto = IP_ICMPPROTO;
	hnputs(np->icmpid, 0);
	hnputs(np->seq, arg);
	memset(np->cksum, 0, sizeof(np->cksum));
	hnputs(np->cksum, ptclcsum(nbp, ICMP_IPSIZE, blocklen(nbp) - ICMP_IPSIZE));

	icmp = f->t2p[IP_ICMPPROTO];
	((Icmppriv*)icmp->priv)->out[type]++;

	if(rh != nil && rh->r != nil && (rh->r->type & Runi) != 0){
		np = (Icmp *)nbp->rp;
		np->vihl = IP_VER4|IP_HLEN4;
		np->tos = DFLTTOS;
		np->ttl = MAXTTL;

		(*icmp->rcv)(icmp, ifc, nbp);
		return;
	}

	ipoput4(f, nbp, nil, MAXTTL, DFLTTOS, rh);
}

void
icmpnohost(Fs *f, Ipifc *ifc, Block *bp, Routehint *rh)
{
	icmpsend(f, ifc, bp, Unreachable, 1, 0, rh);
}

void
icmpnoconv(Fs *f, Ipifc *ifc, Block *bp)
{
	icmpsend(f, ifc, bp, Unreachable, 3, 0, nil);
}

void
icmpcantfrag(Fs *f, Ipifc *ifc, Block *bp, int mtu)
{
	icmpsend(f, ifc, bp, Unreachable, 4, mtu, nil);
}

void
icmpttlexceeded(Fs *f, Ipifc *ifc, Block *bp)
{
	icmpsend(f, ifc, bp, TimeExceed, 0, 0, nil);
}

static void
goticmpkt(Proto *icmp, Block *bp, Ipifc *ifc)
{
	uchar	dst[IPaddrlen], src[IPaddrlen];
	ushort	recid;
	Conv	**c, *s;
	Iphash	*iph;
	Icmp	*p;
	
	p = (Icmp *)bp->rp;
	v4tov6(dst, p->dst);
	v4tov6(src, p->src);
	recid = nhgets(p->icmpid);

	qlock(icmp);
	iph = iphtlook(&((Icmppriv*)icmp->priv)->ht, src, recid, dst, recid);
	if(iph != nil){
		Translation *q;
		int hop = p->ttl;

		if(hop <= 1 || (q = transbackward(icmp, iph)) == nil)
			goto raise;
		memmove(p->dst, q->forward.raddr+IPv4off, IPv4addrlen);
		hnputs_csum(p->icmpid, q->forward.rport, p->cksum);

		/* only use route-hint when from original desination */
		if(memcmp(p->src, q->forward.laddr+IPv4off, IPv4addrlen) != 0)
			q = nil;
		qunlock(icmp);

		ipoput4(icmp->f, bp, ifc, hop - 1, p->tos, q);
		return;
	}
	for(c = icmp->conv; (s = *c) != nil; c++){
		if(s->lport == recid)
		if(ipcmp(s->laddr, dst) == 0 || ipcmp(s->raddr, src) == 0)
			qpass(s->rq, copyblock(bp, blocklen(bp)));
	}
raise:
	qunlock(icmp);
	freeblist(bp);
}

static Block *
mkechoreply(Block *bp, Fs *f)
{
	Icmp	*q;
	uchar	ip[4];

	q = (Icmp *)bp->rp;
	if(!ip4me(f, q->dst) || !ip4reply(f, q->src))
		return nil;

	q->vihl = IP_VER4;
	memmove(ip, q->src, sizeof(q->dst));
	memmove(q->src, q->dst, sizeof(q->src));
	memmove(q->dst, ip,  sizeof(q->dst));
	q->type = EchoReply;
	memset(q->cksum, 0, sizeof(q->cksum));
	hnputs(q->cksum, ptclcsum(bp, ICMP_IPSIZE, blocklen(bp) - ICMP_IPSIZE));

	return bp;
}

static char *unreachcode[] =
{
[0]	"net unreachable",
[1]	"host unreachable",
[2]	"protocol unreachable",
[3]	"port unreachable",
[4]	"fragmentation needed and DF set",
[5]	"source route failed",
[6]	"destination network unknown",
[7]	"destination host unknown",
[8]	"source host isolated",
[9]	"network administratively prohibited",
[10]	"host administratively prohibited",
[11]	"network unreachable for tos",
[12]	"host unreachable for tos",
[13]	"communication administratively prohibited",
[14]	"host precedence violation",
[15]	"precedence cutoff in effect",
};

static void
icmpiput(Proto *icmp, Ipifc *ifc, Block *bp)
{
	int	n;
	Icmp	*p;
	Block	*r;
	Proto	*pr;
	char	*msg;
	char	m2[128];
	Icmppriv *ipriv;

	ipriv = icmp->priv;
	ipriv->stats[InMsgs]++;

	bp = concatblock(bp);
	n = BLEN(bp);
	if(n < ICMP_IPSIZE+ICMP_HDRSIZE){
		ipriv->stats[InErrors]++;
		ipriv->stats[HlenErrs]++;
		netlog(icmp->f, Logicmp, "icmp hlen %d\n", n);
		goto raise;
	}
	if(ptclcsum(bp, ICMP_IPSIZE, n - ICMP_IPSIZE)){
		ipriv->stats[InErrors]++;
		ipriv->stats[CsumErrs]++;
		netlog(icmp->f, Logicmp, "icmp checksum error\n");
		goto raise;
	}
	p = (Icmp *)bp->rp;
	netlog(icmp->f, Logicmp, "icmpiput %s (%d) %d\n",
		(p->type < nelem(icmpnames)? icmpnames[p->type]: ""),
		p->type, p->code);
	if(p->type <= Maxtype)
		ipriv->in[p->type]++;

	switch(p->type) {
	case EchoRequest:
		r = mkechoreply(bp, icmp->f);
		if(r == nil)
			goto raise;
		ipriv->out[EchoReply]++;
		ipoput4(icmp->f, r, nil, MAXTTL, DFLTTOS, nil);
		break;
	case TimeExceed:
		if(p->code == 0){
			snprint(msg = m2, sizeof m2, "ttl exceeded at %V", p->src);
			goto Advise;
		}
		goticmpkt(icmp, bp, ifc);
		break;
	case Unreachable:
		if(p->code >= nelem(unreachcode)) {
			snprint(m2, sizeof m2, "unreachable %V -> %V code %d",
				p->src, p->dst, p->code);
			msg = m2;
		} else
			msg = unreachcode[p->code];
	Advise:
		bp->rp += ICMP_IPSIZE+ICMP_HDRSIZE;
		if(BLEN(bp) < MinAdvise){
			ipriv->stats[LenErrs]++;
			goto raise;
		}
		p = (Icmp *)bp->rp;
		if(p->vihl == (IP_VER4|IP_HLEN4)	/* advise() does not expect options */
		&& (nhgets(p->frag) & IP_FO) == 0	/* first fragment */
		&& ipcsum(&p->vihl) == 0){
			pr = Fsrcvpcolx(icmp->f, p->proto);
			if(pr != nil && pr->advise != nil) {
				netlog(icmp->f, Logicmp, "advising %s!%V -> %V: %s\n", pr->name, p->src, p->dst, msg);
				(*pr->advise)(pr, bp, ifc, msg);
				return;
			}
		}
		bp->rp -= ICMP_IPSIZE+ICMP_HDRSIZE;
		/* wet floor */
	default:
		goticmpkt(icmp, bp, ifc);
		break;
	}
	return;
raise:
	freeblist(bp);
}

/*
 * called from protocol advice handlers when the advice
 * is actually for someone we source translate (ip4).
 * the caller has fixed up the ip address and ports
 * in the inner header, so we just restore the outer
 * ip/icmp headers, recalculating icmp checksum
 * and send the advice to ip4.
 */
void
icmpproxyadvice(Fs *f, Ipifc *ifc, Block *bp, uchar *ip4)
{
	Icmp	*p;
	int	hop;

	/* inner header */
	p = (Icmp *) bp->rp;
	if(p->vihl != (IP_VER4|IP_HLEN4))
		goto drop;
	if(ipcsum(&p->vihl) != 0)
		goto drop;

	/* outer header */
	bp->rp -= ICMP_IPSIZE+ICMP_HDRSIZE;
	p = (Icmp *) bp->rp;
	if(p->vihl != (IP_VER4|(ICMP_IPSIZE>>2)))
		goto drop;

	hop = p->ttl;
	if(hop <= 1)
		goto drop;

	netlog(f, Logicmp|Logtrans, "proxying icmp advice from %V to %V->%V\n",
		p->src, p->dst, ip4);
	memmove(p->dst, ip4, IPv4addrlen);

	/* recalculate ICMP checksum */
	memset(p->cksum, 0, sizeof(p->cksum));
	hnputs(p->cksum, ptclcsum(bp, ICMP_IPSIZE, blocklen(bp) - ICMP_IPSIZE));

	ipoput4(f, bp, ifc, hop - 1, p->tos, nil);
	return;
drop:
	freeblist(bp);
}

static void
icmpadvise(Proto *icmp, Block *bp, Ipifc *ifc, char *msg)
{
	uchar	dst[IPaddrlen], src[IPaddrlen];
	ushort	recid;
	Conv	**c, *s;
	Icmp	*p;
	Iphash	*iph;

	p = (Icmp *) bp->rp;
	v4tov6(dst, p->dst);
	v4tov6(src, p->src);
	recid = nhgets(p->icmpid);

	qlock(icmp);
	iph = iphtlook(&((Icmppriv*)icmp->priv)->ht, dst, recid, src, recid);
	if(iph != nil){
		Translation *q;

		if((q = transbackward(icmp, iph)) == nil)
			goto raise;

		hnputs_csum(p->src+0, nhgets(q->forward.raddr+IPv4off+0), p->ipcksum);
		hnputs_csum(p->src+2, nhgets(q->forward.raddr+IPv4off+2), p->ipcksum);

		hnputs_csum(p->icmpid, q->forward.rport, p->cksum);
		qunlock(icmp);

		icmpproxyadvice(icmp->f, ifc, bp, p->src);
		return;
	}
	for(c = icmp->conv; (s = *c) != nil; c++){
		if(s->lport == recid)
		if(ipcmp(s->laddr, src) == 0)
		if(ipcmp(s->raddr, dst) == 0){
			if(s->ignoreadvice)
				break;
			qhangup(s->rq, msg);
			qhangup(s->wq, msg);
			break;
		}
	}
raise:
	qunlock(icmp);
	freeblist(bp);
}

static Block*
icmpforward(Proto *icmp, Block *bp, Route *r)
{
	uchar da[IPaddrlen], sa[IPaddrlen];
	ushort id;
	Icmp *p;
	Translation *q;

	p = (Icmp*)(bp->rp);
	switch(p->type){
	case EchoRequest:
	case Timestamp:
	case InfoRequest:
	case AddrMaskRequest:
		break;
	default:
		/* no icmpid, can't translate back */
		freeblist(bp);
		return nil;
	}
	v4tov6(sa, p->src);
	v4tov6(da, p->dst);
	id = nhgets(p->icmpid);

	qlock(icmp);
	q = transforward(icmp, &((Icmppriv*)icmp->priv)->ht, sa, id, da, id, r);
	if(q == nil){
		qunlock(icmp);
		freeblist(bp);
		return nil;
	}
	memmove(p->src, q->backward.laddr+IPv4off, IPv4addrlen);
	hnputs_csum(p->icmpid, q->backward.lport, p->cksum);
	qunlock(icmp);

	return bp;
}

static int
icmpstats(Proto *icmp, char *buf, int len)
{
	Icmppriv *priv;
	char *p, *e;
	int i;

	priv = icmp->priv;
	p = buf;
	e = p+len;
	for(i = 0; i < Nstats; i++)
		p = seprint(p, e, "%s: %lud\n", statnames[i], priv->stats[i]);
	for(i = 0; i <= Maxtype; i++){
		if(icmpnames[i] != nil)
			p = seprint(p, e, "%s: %lud %lud\n", icmpnames[i], priv->in[i], priv->out[i]);
		else
			p = seprint(p, e, "%d: %lud %lud\n", i, priv->in[i], priv->out[i]);
	}
	return p - buf;
}
	
void
icmpinit(Fs *fs)
{
	Proto *icmp;

	icmp = smalloc(sizeof(Proto));
	icmp->priv = smalloc(sizeof(Icmppriv));
	icmp->name = "icmp";
	icmp->connect = icmpconnect;
	icmp->announce = icmpannounce;
	icmp->state = icmpstate;
	icmp->create = icmpcreate;
	icmp->close = icmpclose;
	icmp->rcv = icmpiput;
	icmp->stats = icmpstats;
	icmp->ctl = nil;
	icmp->advise = icmpadvise;
	icmp->forward = icmpforward;
	icmp->gc = nil;
	icmp->ipproto = IP_ICMPPROTO;
	icmp->nc = 128;
	icmp->ptclsize = 0;

	Fsproto(fs, icmp);
}
