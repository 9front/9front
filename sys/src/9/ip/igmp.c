/*
 * IGMPv2 - internet group management protocol (and MLDv1)
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include "ip.h"
#include "ipv6.h"

enum
{
	IGMP_IPHDRSIZE	= 20,		/* size of ip header */
	IGMP_HDRSIZE	= 8,		/* size of IGMP header */
	IP_IGMPPROTO	= 2,

	IGMPquery	= 1,
	IGMPreport	= 2,
	IGMPv2report	= 6,
	IGMPv2leave	= 7,

	IP_MLDPROTO	= HBH,		/* hop-by-hop header */

	MLDquery	= 130,
	MLDreport	= 131,
	MLDdone		= 132,

	MSPTICK		= 100,
	MAXTIMEOUT	= 10000/MSPTICK,	/* at most 10 secs for a response */
};

typedef struct IGMPpkt IGMPpkt;
struct IGMPpkt
{
	/* ip header */
	uchar	vihl;		/* Version and header length */
	uchar	tos;		/* Type of service */
	uchar	len[2];		/* packet length (including headers) */
	uchar	id[2];		/* Identification */
	uchar	frag[2];	/* Fragment information */
	uchar	Unused;	
	uchar	proto;		/* Protocol */
	uchar	cksum[2];	/* checksum of ip portion */
	uchar	src[4];		/* Ip source */
	uchar	dst[4];		/* Ip destination */

	/* igmp header */
	uchar	vertype;	/* version and type */
	uchar	resptime;
	uchar	igmpcksum[2];	/* checksum of igmp portion */
	uchar	group[4];	/* multicast group */

	uchar	payload[];
};

#define IGMPPKTSZ offsetof(IGMPpkt, payload[0])

typedef struct MLDpkt MLDpkt;
struct MLDpkt {
	IPV6HDR;

	uchar	type;
	uchar	code;
	uchar	cksum[2];
	uchar	delay[2];
	uchar	res[2];
	uchar	group[IPaddrlen];
	uchar	payload[];
};

#define MLDPKTSZ offsetof(MLDpkt, payload[0])

static uchar mldhbhopt[] = {
	ICMPv6,	/* NextHeader */
	0x00,	/* HeaderLength */
		0x05,		/* Option: Router Alert */
		0x02,		/* Length */
		0x00, 0x00,	/* MLD */

		0x01,		/* Option: PadN */
		0x00,		/* Length */
};

typedef struct Report Report;
struct Report
{
	Report	*next;
	Proto	*proto;
	Ipifc	*ifc;
	int	ifcid;
	int	timeout;	/* in MSPTICK's */
	Ipmulti	*multi;
};

typedef struct Priv Priv;
struct Priv
{
	QLock;
	Rendez	r;
	Report	*reports;
};

static void
igmpsendreport(Fs *f, uchar *src, uchar *dst, uchar *group, int done)
{
	IGMPpkt *p;
	Block *bp;

	bp = allocb(IGMPPKTSZ);
	bp->wp += IGMPPKTSZ;
	p = (IGMPpkt*)bp->rp;
	memset(p, 0, IGMPPKTSZ);
	p->vihl = IP_VER4;
	memmove(p->src, src+IPv4off, IPv4addrlen);
	memmove(p->dst, dst+IPv4off, IPv4addrlen);
	p->vertype = (1<<4) | (done? IGMPv2leave: IGMPv2report);
	p->resptime = 0;
	p->proto = IP_IGMPPROTO;
	memmove(p->group, group+IPv4off, IPv4addrlen);
	hnputs(p->igmpcksum, ptclcsum(bp, IGMP_IPHDRSIZE, IGMP_HDRSIZE));
	ipoput4(f, bp, 0, 1, DFLTTOS, nil);	/* TTL of 1 */
}

static void
mldsendreport(Fs *f, uchar *src, uchar *dst, uchar *group, int done)
{
	MLDpkt *p;
	Block *bp;

	if(!islinklocal(src))
		return;

	bp = allocb(sizeof(mldhbhopt)+MLDPKTSZ);
	bp->wp += sizeof(mldhbhopt)+MLDPKTSZ;
	bp->rp += sizeof(mldhbhopt);
	p = (MLDpkt*)bp->rp;
	memset(p, 0, MLDPKTSZ);
	ipmove(p->src, src);
	ipmove(p->dst, dst);
	p->type = done? MLDdone: MLDreport;
	p->code = 0;
	ipmove(p->group, group);

	/* generate checksum */
	hnputl(p->vcf, 0);
	hnputs(p->ploadlen, MLDPKTSZ-IP6HDR);
	p->proto = 0;
	p->ttl = ICMPv6;	/* ttl gets set later */
	hnputs(p->cksum, 0);
	hnputs(p->cksum, ptclcsum(bp, 0, MLDPKTSZ));

	/* add hop-by-hop option header */
	bp->rp -= sizeof(mldhbhopt);
	memmove(bp->rp, p, IP6HDR);
	memmove(bp->rp + IP6HDR, mldhbhopt, sizeof(mldhbhopt));
	p = (MLDpkt*)bp->rp;
	p->proto = IP_MLDPROTO;
	hnputs(p->ploadlen, BLEN(bp) - IP6HDR);
	
	ipoput6(f, bp, 0, 1, DFLTTOS, nil);	/* TTL of 1 */
}

static void
sendreport(Proto *pr, uchar *ia, uchar *group, int done)
{
	switch(pr->ipproto){
	case IP_IGMPPROTO:
		igmpsendreport(pr->f, ia, group, group, done);
		break;
	case IP_MLDPROTO:
		mldsendreport(pr->f, ia, group, group, done);
		break;
	}
}

static int
isreport(void *a)
{
	return ((Priv*)a)->reports != 0;
}

static void
igmpproc(void *a)
{
	Proto *pr, *igmp = a;
	Priv *priv = igmp->priv;
	Report *rp, **lrp;
	Ipmulti *mp, **lmp;

	for(;;){
		sleep(&priv->r, isreport, priv);
		for(;;){
			qlock(priv);
			if(priv->reports == nil)
				break;
	
			/* look for a single report */
			mp = nil;
			pr = nil;
			lrp = &priv->reports;
			for(rp = *lrp; rp != nil; rp = *lrp){
				lmp = &rp->multi;
				for(mp = *lmp; mp != nil; mp = *lmp){
					if(rp->timeout <= 1 || nrand(rp->timeout) == 0){
						*lmp = mp->next;
						break;
					}
					lmp = &mp->next;
				}
				pr = rp->proto;
				if(rp->multi != nil){
					rp->timeout--;
					lrp = &rp->next;
				} else {
					*lrp = rp->next;
					free(rp);
				}
				if(mp != nil)
					break;
			}
			qunlock(priv);

			if(mp != nil){
				/* do a single report and try again */
				if(pr != nil && !waserror()){
					sendreport(pr, mp->ia, mp->ma, 0);
					poperror();
				}
				free(mp);
				continue;
			}

			tsleep(&up->sleep, return0, 0, MSPTICK);
		}
		qunlock(priv);
	}
}

/*
 *  find report list for this protocol and interface
 */
static Report*
findreport(Report *rp, Proto *pr, Ipifc *ifc)
{
	for(; rp != nil; rp = rp->next)
		if(rp->proto == pr && rp->ifc == ifc && rp->ifcid == ifc->ifcid)
			return rp;

	return nil;
}

static void
queuereport(Proto *pr, Ipifc *ifc, uchar *group, int timeout)
{
	Priv *priv = pr->priv;
	Report *rp;

	qlock(priv);
	if(findreport(priv->reports, pr, ifc) != nil){
		/*
		 *  we are already reporting on this interface,
		 *  wait for the report to time-out.
		 */
		qunlock(priv);
		return;
	}

	/*
	 *  start reporting groups that we're a member of.
	 */
	rp = smalloc(sizeof(Report));
	rp->proto = pr;
	rp->ifc = ifc;
	rp->ifcid = ifc->ifcid;
	rp->timeout = (timeout < 1 || timeout > MAXTIMEOUT) ? MAXTIMEOUT : timeout;
	rp->multi = ipifcgetmulti(pr->f, ifc, group);

	rp->next = priv->reports;
	priv->reports = rp;

	wakeup(&priv->r);
	qunlock(priv);
}

static void
purgereport(Proto *pr, Ipifc *ifc, uchar *group)
{
	Priv *priv = pr->priv;
	Report *rp;

	qlock(priv);
	if((rp = findreport(priv->reports, pr, ifc)) != nil){
		Ipmulti *mp, **lmp;

		lmp = &rp->multi;
		for(mp = *lmp; mp; mp = *lmp){
			if(ipcmp(mp->ma, group) == 0){
				*lmp = mp->next;
				free(mp);
				break;
			}
			lmp = &mp->next;
		}
	}
	qunlock(priv);
}

static void
mldiput(Proto *mld, Ipifc *ifc, Block *bp)
{
	MLDpkt *p;
	uchar *opt, *payload;

	p = (MLDpkt*)(bp->rp);
	/* check we have the hop-by-hop header */
	if((p->vcf[0] & 0xF0) != IP_VER6 || p->proto != IP_MLDPROTO)
		goto error;
	if(p->ttl != 1 || !isv6mcast(p->dst) || !islinklocal(p->src))
		goto error;

	/* strip the hop-by-hop option header */
	if(BLEN(bp) < IP6HDR+sizeof(mldhbhopt))
		goto error;
	opt = bp->rp + IP6HDR;
	if(opt[0] != ICMPv6)
		goto error;
	payload = opt + ((int)opt[1] + 1)*8;
	if(payload >= bp->wp)
		goto error;
	if(memcmp(opt+2, mldhbhopt+2, 4) != 0)
		goto error;
	memmove(payload-IP6HDR, bp->rp, IP6HDR);
	bp->rp = payload - IP6HDR;
	if(BLEN(bp) < MLDPKTSZ)
		goto error;
	p = (MLDpkt*)bp->rp;

	/* verify ICMPv6 checksum */
	hnputl(p->vcf, 0);  	/* borrow IP header as pseudoheader */
	p->ttl = ICMPv6;
	p->proto = 0;
	hnputs(p->ploadlen, MLDPKTSZ-IP6HDR);
	if(ptclcsum(bp, 0, MLDPKTSZ))
		goto error;

	if(ipcmp(p->group, IPnoaddr) != 0 && ipismulticast(p->group) != V6)
		goto error;

	switch(p->type){
	case MLDquery:
		queuereport(mld, ifc, p->group, nhgets(p->delay)/MSPTICK);
		break;
	case MLDreport:
		purgereport(mld, ifc, p->group);
		break;
	}
error:
	freeblist(bp);
}

static void
igmpiput(Proto *igmp, Ipifc *ifc, Block *bp)
{
	uchar group[IPaddrlen];
	IGMPpkt *p;

	p = (IGMPpkt*)bp->rp;
	if((p->vihl & 0xF0) != IP_VER4)
		goto error;
	if(BLEN(bp) < IGMP_IPHDRSIZE+IGMP_HDRSIZE)
		goto error;
	if((p->vertype>>4) != 1)
		goto error;
	if(ptclcsum(bp, IGMP_IPHDRSIZE, IGMP_HDRSIZE))
		goto error;

	v4tov6(group, p->group);
	if(ipcmp(group, v4prefix) != 0 && ipismulticast(group) != V4)
		goto error;

	switch(p->vertype & 0xF){
	case IGMPquery:
		queuereport(igmp, ifc, group, p->resptime);
		break;
	case IGMPreport:
	case IGMPv2report:
		purgereport(igmp, ifc, group);
		break;
	}
error:
	freeblist(bp);
}

static void
multicastreport(Fs *f, Ipifc *ifc, uchar *ma, uchar *ia, int done)
{
	Proto *pr = Fsrcvpcolx(f, isv4(ma)? IP_IGMPPROTO: IP_MLDPROTO);
	purgereport(pr, ifc, ma);
	sendreport(pr, ia, ma, done);
}

void
igmpinit(Fs *f)
{
	Proto *igmp, *mld;

	igmp = smalloc(sizeof(Proto));
	igmp->priv = smalloc(sizeof(Priv));
	igmp->name = "igmp";
	igmp->connect = nil;
	igmp->announce = nil;
	igmp->ctl = nil;
	igmp->state = nil;
	igmp->close = nil;
	igmp->rcv = igmpiput;
	igmp->stats = nil;
	igmp->ipproto = IP_IGMPPROTO;
	igmp->nc = 0;
	igmp->ptclsize = 0;
	Fsproto(f, igmp);

	mld = smalloc(sizeof(Proto));
	mld->priv = igmp->priv;
	mld->name = "mld";
	mld->connect = nil;
	mld->announce = nil;
	mld->ctl = nil;
	mld->state = nil;
	mld->close = nil;
	mld->rcv = mldiput;
	mld->stats = nil;
	mld->ipproto = IP_MLDPROTO;
	mld->nc = 0;
	mld->ptclsize = 0;
	Fsproto(f, mld);

	multicastreportfn = multicastreport;
	kproc("igmpproc", igmpproc, igmp);
}
