#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	"ip.h"
#include	"ipv6.h"


#define DPRINT if(0)print

enum
{
	UDP_UDPHDR_SZ	= 8,

	UDP4_PHDR_OFF = 8,
	UDP4_PHDR_SZ = 12,
	UDP4_IPHDR_SZ = 20,
	UDP6_IPHDR_SZ = 40,
	UDP6_PHDR_SZ = 40,
	UDP6_PHDR_OFF = 0,

	IP_UDPPROTO	= 17,
	UDP_USEAD7	= 52,

	Udprxms		= 200,
	Udptickms	= 100,
	Udpmaxxmit	= 10,
};

typedef struct Udp4hdr Udp4hdr;
struct Udp4hdr
{
	/* ip header */
	uchar	vihl;		/* Version and header length */
	uchar	tos;		/* Type of service */
	uchar	length[2];	/* packet length */
	uchar	id[2];		/* Identification */
	uchar	frag[2];	/* Fragment information */
	uchar	ttl;      	/* Time to live */
	uchar	udpproto;	/* Protocol */
	uchar	udpplen[2];	/* Header plus data length */
	uchar	udpsrc[IPv4addrlen];	/* Ip source */
	uchar	udpdst[IPv4addrlen];	/* Ip destination */

	/* udp header */
	uchar	udpsport[2];	/* Source port */
	uchar	udpdport[2];	/* Destination port */
	uchar	udplen[2];	/* data length */
	uchar	udpcksum[2];	/* Checksum */
};

typedef struct Udp6hdr Udp6hdr;
struct Udp6hdr {
	uchar viclfl[4];
	uchar len[2];
	uchar nextheader;
	uchar hoplimit;
	uchar udpsrc[IPaddrlen];
	uchar udpdst[IPaddrlen];

	/* udp header */
	uchar	udpsport[2];	/* Source port */
	uchar	udpdport[2];	/* Destination port */
	uchar	udplen[2];	/* data length */
	uchar	udpcksum[2];	/* Checksum */
};

/* MIB II counters */
typedef struct Udpstats Udpstats;
struct Udpstats
{
	uvlong	udpInDatagrams;
	ulong	udpNoPorts;
	ulong	udpInErrors;
	uvlong	udpOutDatagrams;
};

typedef struct Udppriv Udppriv;
struct Udppriv
{
	Ipht		ht;

	/* MIB counters */
	Udpstats	ustats;

	/* non-MIB stats */
	ulong		csumerr;		/* checksum errors */
	ulong		lenerr;			/* short packet */
};

void udpkick(void *x, Block *bp);

/*
 *  protocol specific part of Conv
 */
typedef struct Udpcb Udpcb;
struct Udpcb
{
	uchar	headers;

	/* source ip used for transmission */
	uchar	srcip[IPaddrlen];
};

static char*
udpconnect(Conv *c, char **argv, int argc)
{
	char *e;
	Udppriv *upriv;

	upriv = c->p->priv;
	e = Fsstdconnect(c, argv, argc);
	Fsconnected(c, e);
	if(e != nil)
		return e;
	ipmove(((Udpcb*)c->ptcl)->srcip, c->laddr);
	iphtadd(&upriv->ht, c);
	return nil;
}


static int
udpstate(Conv *c, char *state, int n)
{
	return snprint(state, n, "%s qin %d qout %d\n",
		c->inuse ? "Open" : "Closed",
		c->rq ? qlen(c->rq) : 0,
		c->wq ? qlen(c->wq) : 0
	);
}

static char*
udpannounce(Conv *c, char** argv, int argc)
{
	char *e;
	Udppriv *upriv;

	upriv = c->p->priv;
	e = Fsstdannounce(c, argv, argc);
	if(e != nil)
		return e;
	Fsconnected(c, nil);
	ipmove(((Udpcb*)c->ptcl)->srcip, c->laddr);
	iphtadd(&upriv->ht, c);
	return nil;
}

static void
udpcreate(Conv *c)
{
	c->rq = qopen(512*1024, Qmsg, 0, 0);
	c->wq = qbypass(udpkick, c);
}

static void
udpclose(Conv *c)
{
	Udpcb *ucb;
	Udppriv *upriv;

	upriv = c->p->priv;
	iphtrem(&upriv->ht, c);

	c->state = 0;
	qclose(c->rq);
	qclose(c->wq);
	qclose(c->eq);
	c->lport = 0;
	ipmove(c->laddr, IPnoaddr);
	c->rport = 0;
	ipmove(c->raddr, IPnoaddr);

	ucb = (Udpcb*)c->ptcl;
	ucb->headers = 0;
	ipmove(ucb->srcip, IPnoaddr);
}

void
udpkick(void *x, Block *bp)
{
	Conv *c = x;
	Udp4hdr *uh4;
	Udp6hdr *uh6;
	ushort rport;
	uchar laddr[IPaddrlen], raddr[IPaddrlen];
	Udpcb *ucb;
	int dlen, ptcllen;
	Udppriv *upriv;
	Fs *f;
	int version;
	Routehint *rh;
	ushort csum;

	upriv = c->p->priv;
	f = c->p->f;

//	netlog(c->p->f, Logudp, "udp: kick\n");	/* frequent and uninteresting */
	if(bp == nil)
		return;

	ucb = (Udpcb*)c->ptcl;
	switch(ucb->headers) {
	case 7:
		/* get user specified addresses */
		bp = pullupblock(bp, UDP_USEAD7);
		if(bp == nil)
			return;
		ipmove(raddr, bp->rp);
		bp->rp += IPaddrlen;
		ipmove(laddr, bp->rp);
		bp->rp += IPaddrlen;
		switch(ipforme(f, laddr)){
		case Runi:
			break;
		case Rmulti:
		case Rbcast:
			/* use ifc address for bcast/mcast reply */
			if(ipcmp(bp->rp, IPnoaddr) != 0
			&& ipforme(f, bp->rp) == Runi
			&& v6lookup(f, raddr, bp->rp, nil) != nil){
				ipmove(laddr, bp->rp);
				break;
			}
		default:
			/* pick interface closest to dest */
			findlocalip(f, laddr, raddr);
		}
		bp->rp += IPaddrlen;		/* Ignore ifc address */
		rport = nhgets(bp->rp);
		bp->rp += 2+2;			/* Ignore local port */
		break;
	default:
		ipmove(laddr, IPnoaddr);
		rport = 0;
		break;
	}

	if(ucb->headers) {
		if(isv4(laddr) || ipcmp(laddr, IPnoaddr) == 0)
			version = V4;
		else
			version = V6;
	} else {
		version = convipvers(c);
		ipmove(laddr, ucb->srcip);
	}

	dlen = blocklen(bp);

	/* fill in pseudo header and compute checksum */
	switch(version){
	case V4:
		bp = padblock(bp, UDP4_IPHDR_SZ+UDP_UDPHDR_SZ);
		uh4 = (Udp4hdr *)(bp->rp);
		ptcllen = dlen + UDP_UDPHDR_SZ;
		uh4->ttl = 0;
		uh4->udpproto = IP_UDPPROTO;
		uh4->frag[0] = 0;
		uh4->frag[1] = 0;
		hnputs(uh4->udpplen, ptcllen);
		if(ucb->headers) {
			v6tov4(uh4->udpdst, raddr);
			hnputs(uh4->udpdport, rport);
			rh = nil;
		} else {
			v6tov4(uh4->udpdst, c->raddr);
			hnputs(uh4->udpdport, c->rport);
			rh = c;
		}
		v6tov4(uh4->udpsrc, laddr);
		hnputs(uh4->udpsport, c->lport);
		hnputs(uh4->udplen, ptcllen);
		uh4->udpcksum[0] = 0;
		uh4->udpcksum[1] = 0;
		csum = ptclcsum(bp, UDP4_PHDR_OFF, dlen+UDP_UDPHDR_SZ+UDP4_PHDR_SZ);
		if(csum == 0)
			csum = 0xffff;	/* -0 */
		hnputs(uh4->udpcksum, csum);
		uh4->vihl = IP_VER4;
		ipoput4(f, bp, nil, c->ttl, c->tos, rh);
		break;

	case V6:
		/*
		 * using the v6 ip header to create pseudo header
		 * first then reset it to the normal ip header
		 */
		bp = padblock(bp, UDP6_IPHDR_SZ+UDP_UDPHDR_SZ);
		uh6 = (Udp6hdr *)(bp->rp);
		memset(uh6, 0, 8);
		ptcllen = dlen + UDP_UDPHDR_SZ;
		hnputl(uh6->viclfl, ptcllen);
		uh6->hoplimit = IP_UDPPROTO;
		if(ucb->headers) {
			ipmove(uh6->udpdst, raddr);
			hnputs(uh6->udpdport, rport);
			rh = nil;
		} else {
			ipmove(uh6->udpdst, c->raddr);
			hnputs(uh6->udpdport, c->rport);
			rh = c;
		}
		ipmove(uh6->udpsrc, laddr);
		hnputs(uh6->udpsport, c->lport);
		hnputs(uh6->udplen, ptcllen);
		uh6->udpcksum[0] = 0;
		uh6->udpcksum[1] = 0;
		csum = ptclcsum(bp, UDP6_PHDR_OFF, dlen+UDP_UDPHDR_SZ+UDP6_PHDR_SZ);
		if(csum == 0)
			csum = 0xffff;	/* -0 */
		hnputs(uh6->udpcksum, csum);
		memset(uh6, 0, 8);
		uh6->viclfl[0] = IP_VER6;
		hnputs(uh6->len, ptcllen);
		uh6->nextheader = IP_UDPPROTO;
		ipoput6(f, bp, nil, c->ttl, c->tos, rh);
		break;

	default:
		panic("udpkick: version %d", version);
	}
	upriv->ustats.udpOutDatagrams++;
}

void
udpiput(Proto *udp, Ipifc *ifc, Block *bp)
{
	int len;
	Udp4hdr *uh4;
	Udp6hdr *uh6;
	Iphash *iph;
	Conv *c;
	Udpcb *ucb;
	uchar raddr[IPaddrlen], laddr[IPaddrlen];
	ushort rport, lport;
	Udppriv *upriv;
	Fs *f;
	int version;
	int ottl, oviclfl, olen;
	uchar *p;

	upriv = udp->priv;
	f = udp->f;
	upriv->ustats.udpInDatagrams++;

	uh4 = (Udp4hdr*)(bp->rp);
	uh6 = (Udp6hdr*)(bp->rp);
	version = ((uh4->vihl&0xF0)==IP_VER6) ? V6 : V4;

	/* Put back pseudo header for checksum
	 * (remember old values for icmpnoconv()) */
	switch(version) {
	case V4:
		ottl = uh4->ttl;
		uh4->ttl = 0;
		len = nhgets(uh4->udplen);
		olen = nhgets(uh4->udpplen);
		hnputs(uh4->udpplen, len);

		v4tov6(raddr, uh4->udpsrc);
		v4tov6(laddr, uh4->udpdst);
		lport = nhgets(uh4->udpdport);
		rport = nhgets(uh4->udpsport);

		if(nhgets(uh4->udpcksum)) {
			if(ptclcsum(bp, UDP4_PHDR_OFF, len+UDP4_PHDR_SZ)) {
				upriv->ustats.udpInErrors++;
				netlog(f, Logudp, "udp: checksum error %I\n", raddr);
				DPRINT("udp: checksum error %I\n", raddr);
				freeblist(bp);
				return;
			}
		}
		uh4->ttl = ottl;
		hnputs(uh4->udpplen, olen);
		break;
	case V6:
		len = nhgets(uh6->udplen);
		oviclfl = nhgetl(uh6->viclfl);
		olen = nhgets(uh6->len);
		ottl = uh6->hoplimit;
		ipmove(raddr, uh6->udpsrc);
		ipmove(laddr, uh6->udpdst);
		lport = nhgets(uh6->udpdport);
		rport = nhgets(uh6->udpsport);
		memset(uh6, 0, 8);
		hnputl(uh6->viclfl, len);
		uh6->hoplimit = IP_UDPPROTO;
		if(ptclcsum(bp, UDP6_PHDR_OFF, len+UDP6_PHDR_SZ)) {
			upriv->ustats.udpInErrors++;
			netlog(f, Logudp, "udp: checksum error %I\n", raddr);
			DPRINT("udp: checksum error %I\n", raddr);
			freeblist(bp);
			return;
		}
		hnputl(uh6->viclfl, oviclfl);
		hnputs(uh6->len, olen);
		uh6->nextheader = IP_UDPPROTO;
		uh6->hoplimit = ottl;
		break;
	default:
		panic("udpiput: version %d", version);
	}

	qlock(udp);
	iph = iphtlook(&upriv->ht, raddr, rport, laddr, lport);
	if(iph == nil){
Noconv:
		/* no conversation found */
		upriv->ustats.udpNoPorts++;
		qunlock(udp);
		netlog(f, Logudp, "udp: no conv %I!%d -> %I!%d\n", raddr, rport,
		       laddr, lport);

		switch(version){
		case V4:
			icmpnoconv(f, ifc, bp);
			break;
		case V6:
			icmpnoconv6(f, ifc, bp);
			break;
		default:
			panic("udpiput2: version %d", version);
		}

		freeblist(bp);
		return;
	}
	if(iph->trans){
		Translation *q;
		int hop = uh4->ttl;
		if(hop <= 1 || (q = transbackward(udp, iph)) == nil){
			qunlock(udp);
			freeblist(bp);
			return;
		}
		hnputs_csum(uh4->udpdst+0, nhgets(q->forward.raddr+IPv4off+0), uh4->udpcksum);
		hnputs_csum(uh4->udpdst+2, nhgets(q->forward.raddr+IPv4off+2), uh4->udpcksum);
		hnputs_csum(uh4->udpdport, q->forward.rport, uh4->udpcksum);

		/* only use route-hint when from original desination */
		if(memcmp(uh4->udpsrc, q->forward.laddr+IPv4off, IPv4addrlen) != 0)
			q = nil;
		qunlock(udp);
		ipoput4(f, bp, ifc, hop - 1, uh4->tos, q);
		return;
	}
	c = iphconv(iph);
	ucb = (Udpcb*)c->ptcl;

	if(c->state == Announced){
		if(ucb->headers == 0){
			uchar srcip[IPaddrlen];

			switch(ipforme(f, laddr)){
			default:
				goto Noconv;
			case Runi:
				ipmove(srcip, laddr);
				break;
			case Rmulti:
			case Rbcast:
				/*
				 * use the multicast address for reception,
				 * and local unicast ip for transmission.
				 */
				ipv6local(ifc, srcip, 0, raddr);
				break;
			}
			c = Fsnewcall(c, raddr, rport, laddr, lport, version);
			if(c == nil){
				qunlock(udp);
				freeblist(bp);
				return;
			}
			ucb = (Udpcb*)c->ptcl;
			ipmove(ucb->srcip, srcip);
			iphtadd(&upriv->ht, c);
		}
	}

	qlock(c);
	qunlock(udp);

	/*
	 * Trim the packet down to data size
	 */
	len -= UDP_UDPHDR_SZ;
	switch(version){
	case V4:
		bp = trimblock(bp, UDP4_IPHDR_SZ+UDP_UDPHDR_SZ, len);
		break;
	case V6:
		bp = trimblock(bp, UDP6_IPHDR_SZ+UDP_UDPHDR_SZ, len);
		break;
	default:
		panic("udpiput4: version %d", version);
	}
	if(bp == nil){
		qunlock(c);
		netlog(f, Logudp, "udp: len err %I.%d -> %I.%d\n", raddr, rport,
		       laddr, lport);
		upriv->lenerr++;
		return;
	}

	netlog(f, Logudpmsg, "udp: %I.%d -> %I.%d l %d\n", raddr, rport,
	       laddr, lport, len);

	switch(ucb->headers){
	case 7:
		/* pass the src address */
		bp = padblock(bp, UDP_USEAD7);
		p = bp->rp;
		ipmove(p, raddr); p += IPaddrlen;
		ipmove(p, laddr); p += IPaddrlen;
		if(!ipv6local(ifc, p, 0, raddr))
			ipmove(p, ifc->lifc != nil ? ifc->lifc->local : IPnoaddr);
		p += IPaddrlen;
		hnputs(p, rport); p += 2;
		hnputs(p, lport);
		break;
	}

	if(qfull(c->rq)){
		netlog(f, Logudp, "udp: qfull %I.%d -> %I.%d\n",
			raddr, rport, laddr, lport);
		freeblist(bp);
	} else {
		qpass(c->rq, concatblock(bp));
	}
	qunlock(c);
}

char*
udpctl(Conv *c, char **f, int n)
{
	Udpcb *ucb;

	ucb = (Udpcb*)c->ptcl;
	if(n == 1){
		if(strcmp(f[0], "hangup") == 0){
			qhangup(c->rq, nil);
			qhangup(c->wq, nil);
			return nil;
		}
		if(strcmp(f[0], "headers") == 0){
			ucb->headers = 7;	/* new headers format */
			return nil;
		}
	}
	return "unknown control request";
}

void
udpadvise(Proto *udp, Block *bp, Ipifc *ifc, char *msg)
{
	Udp4hdr *h4;
	Udp6hdr *h6;
	uchar source[IPaddrlen], dest[IPaddrlen];
	ushort psource, pdest;
	Iphash *iph;
	Conv *s;

	h4 = (Udp4hdr*)(bp->rp);
	h6 = (Udp6hdr*)(bp->rp);

	if((h4->vihl&0xF0)==IP_VER4) {
		v4tov6(dest, h4->udpdst);
		v4tov6(source, h4->udpsrc);
		psource = nhgets(h4->udpsport);
		pdest = nhgets(h4->udpdport);
	} else {
		ipmove(dest, h6->udpdst);
		ipmove(source, h6->udpsrc);
		psource = nhgets(h6->udpsport);
		pdest = nhgets(h6->udpdport);
	}

	/* Look for a connection (source/dest reversed; this is the original packet we sent) */
	qlock(udp);
	iph = iphtlook(&((Udppriv*)udp->priv)->ht, dest, pdest, source, psource);
	if(iph == nil || iph->match != IPmatchexact)
		goto raise;
	if(iph->trans){
		Translation *q;

		if((q = transbackward(udp, iph)) == nil)
			goto raise;

		/* h4->udpplen is the ip header checksum */
		hnputs_csum(h4->udpsrc+0, nhgets(q->forward.raddr+IPv4off+0), h4->udpplen);
		hnputs_csum(h4->udpsrc+2, nhgets(q->forward.raddr+IPv4off+2), h4->udpplen);

		/* dont bother fixing udp checksum, packet is most likely truncated */
		hnputs(h4->udpsport, q->forward.rport);
		qunlock(udp);

		icmpproxyadvice(udp->f, ifc, bp, h4->udpsrc);
		return;
	}
	s = iphconv(iph);
	if(s->ignoreadvice || s->state == Announced)
		goto raise;
	qlock(s);
	qunlock(udp);
	qhangup(s->rq, msg);
	qhangup(s->wq, msg);
	qunlock(s);
	freeblist(bp);
	return;
raise:
	qunlock(udp);
	freeblist(bp);
}

Block*
udpforward(Proto *udp, Block *bp, Route *r)
{
	uchar da[IPaddrlen], sa[IPaddrlen];
	ushort dp, sp;
	Udp4hdr *uh4;
	Translation *q;

	uh4 = (Udp4hdr*)(bp->rp);
	v4tov6(sa, uh4->udpsrc);
	v4tov6(da, uh4->udpdst);
	dp = nhgets(uh4->udpdport);
	sp = nhgets(uh4->udpsport);

	qlock(udp);
	q = transforward(udp, &((Udppriv*)udp->priv)->ht, sa, sp, da, dp, r);
	if(q == nil){
		qunlock(udp);
		freeblist(bp);
		return nil;
	}
	hnputs_csum(uh4->udpsrc+0, nhgets(q->backward.laddr+IPv4off+0), uh4->udpcksum);
	hnputs_csum(uh4->udpsrc+2, nhgets(q->backward.laddr+IPv4off+2), uh4->udpcksum);
	hnputs_csum(uh4->udpsport, q->backward.lport, uh4->udpcksum);
	qunlock(udp);

	return bp;
}

int
udpstats(Proto *udp, char *buf, int len)
{
	Udppriv *upriv;

	upriv = udp->priv;
	return snprint(buf, len, "InDatagrams: %llud\nNoPorts: %lud\n"
		"InErrors: %lud\nOutDatagrams: %llud\n",
		upriv->ustats.udpInDatagrams,
		upriv->ustats.udpNoPorts,
		upriv->ustats.udpInErrors,
		upriv->ustats.udpOutDatagrams);
}

void
udpinit(Fs *fs)
{
	Proto *udp;

	udp = smalloc(sizeof(Proto));
	udp->priv = smalloc(sizeof(Udppriv));
	udp->name = "udp";
	udp->connect = udpconnect;
	udp->announce = udpannounce;
	udp->ctl = udpctl;
	udp->state = udpstate;
	udp->create = udpcreate;
	udp->close = udpclose;
	udp->rcv = udpiput;
	udp->advise = udpadvise;
	udp->forward = udpforward;
	udp->stats = udpstats;
	udp->ipproto = IP_UDPPROTO;
	udp->nc = Nchans;
	udp->ptclsize = sizeof(Udpcb);

	Fsproto(fs, udp);
}
