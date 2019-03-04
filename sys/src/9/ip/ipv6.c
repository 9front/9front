#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	"ip.h"
#include	"ipv6.h"

static Block*		ip6reassemble(IP*, int, Block*);
static Fragment6*	ipfragallo6(IP*);
static void		ipfragfree6(IP*, Fragment6*);
static Block*		procopts(Block *bp);
static Block*		procxtns(IP *ip, Block *bp, int doreasm);
static int		unfraglen(Block *bp, uchar *nexthdr, int setfh, int popfh);

void
ip_init_6(Fs *f)
{
	v6params *v6p;

	v6p = smalloc(sizeof(v6params));

	v6p->rp.mflag		= 0;		/* default not managed */
	v6p->rp.oflag		= 0;
	v6p->rp.maxraint	= 600000;	/* millisecs */
	v6p->rp.minraint	= 200000;
	v6p->rp.linkmtu		= 0;		/* no mtu sent */
	v6p->rp.reachtime	= 0;
	v6p->rp.rxmitra		= 0;
	v6p->rp.ttl		= MAXTTL;
	v6p->rp.routerlt	= (3 * v6p->rp.maxraint) / 1000;

	v6p->hp.rxmithost	= 1000;		/* v6 RETRANS_TIMER */

	f->v6p			= v6p;
}

int
ipoput6(Fs *f, Block *bp, int gating, int ttl, int tos, Routehint *rh)
{
	int medialen, len, chunk, uflen, flen, seglen, lid, offset, fragoff;
	int morefrags, blklen, rv = 0;
	uchar *gate, nexthdr;
	Block *xp, *nb;
	Fraghdr6 fraghdr;
	IP *ip;
	Ip6hdr *eh;
	Ipifc *ifc;
	Route *r;

	ip = f->ip;
	ip->stats[OutRequests]++;

	/* Fill out the ip header */
	eh = (Ip6hdr*)bp->rp;
	assert(BLEN(bp) >= IP6HDR);
	len = blocklen(bp);
	if(len >= IP_MAX){
		ip->stats[OutDiscards]++;
		netlog(f, Logip, "%I -> %I: exceeded ip max size: %d\n", eh->src, eh->dst, len);
		goto free;
	}

	r = v6lookup(f, eh->dst, eh->src, rh);
	if(r == nil || (r->type & Rv4) != 0 || (ifc = r->ifc) == nil){
		ip->stats[OutNoRoutes]++;
		netlog(f, Logip, "%I -> %I: no interface\n", eh->src, eh->dst);
		rv = -1;
		goto free;
	}

	if(r->type & (Rifc|Runi|Rbcast|Rmulti))
		gate = eh->dst;
	else
		gate = r->v6.gate;

	if(!canrlock(ifc))
		goto free;

	if(waserror()){
		runlock(ifc);
		nexterror();
	}

	if(ifc->m == nil)
		goto raise;

	if(!gating){
		eh->vcf[0] = IP_VER6;
		eh->vcf[0] |= tos >> 4;
		eh->vcf[1]  = tos << 4;
	}
	eh->ttl = ttl;

	/* If we dont need to fragment just send it */
	medialen = ifc->maxtu - ifc->m->hsize;
	if(len <= medialen) {
		hnputs(eh->ploadlen, len - IP6HDR);
		ipifcoput(ifc, bp, V6, gate);
		runlock(ifc);
		poperror();
		return 0;
	}

	if(gating && !ifc->reassemble) {
		/*
		 * v6 intermediate nodes are not supposed to fragment pkts;
		 * we fragment if ifc->reassemble is turned on; an exception
		 * needed for nat.
		 */
		ip->stats[OutDiscards]++;
		icmppkttoobig6(f, ifc, bp);
		netlog(f, Logip, "%I -> %I: gated pkts not fragmented\n", eh->src, eh->dst);
		goto raise;
	}

	/* start v6 fragmentation */
	uflen = unfraglen(bp, &nexthdr, 1, 0);
	if(uflen < IP6HDR || nexthdr == FH) {
		ip->stats[FragFails]++;
		ip->stats[OutDiscards]++;
		netlog(f, Logip, "%I -> %I: fragment header botch\n", eh->src, eh->dst);
		goto raise;
	}
	if(uflen > medialen) {
		ip->stats[FragFails]++;
		ip->stats[OutDiscards]++;
		netlog(f, Logip, "%I -> %I: unfragmentable part too big: %d\n", eh->src, eh->dst, uflen);
		goto raise;
	}

	flen = len - uflen;
	seglen = (medialen - (uflen + IP6FHDR)) & ~7;
	if(seglen < 8) {
		ip->stats[FragFails]++;
		ip->stats[OutDiscards]++;
		netlog(f, Logip, "%I -> %I: seglen < 8\n", eh->src, eh->dst);
		goto raise;
	}

	lid = incref(&ip->id6);
	fraghdr.nexthdr = nexthdr;
	fraghdr.res = 0;
	hnputl(fraghdr.id, lid);

	xp = bp;
	offset = uflen;
	while (offset && offset >= BLEN(xp)) {
		offset -= BLEN(xp);
		xp = xp->next;
	}
	xp->rp += offset;

	fragoff = 0;
	morefrags = 1;

	for(; fragoff < flen; fragoff += seglen) {
		nb = allocb(uflen + IP6FHDR + seglen);

		if(fragoff + seglen >= flen) {
			seglen = flen - fragoff;
			morefrags = 0;
		}

		hnputs(eh->ploadlen, seglen+IP6FHDR);
		memmove(nb->wp, eh, uflen);
		nb->wp += uflen;

		hnputs(fraghdr.offsetRM, fragoff); /* last 3 bits must be 0 */
		fraghdr.offsetRM[1] |= morefrags;
		memmove(nb->wp, &fraghdr, IP6FHDR);
		nb->wp += IP6FHDR;

		/* Copy data */
		chunk = seglen;
		while (chunk) {
			if(xp == nil) {
				ip->stats[OutDiscards]++;
				ip->stats[FragFails]++;
				freeblist(nb);
				netlog(f, Logip, "xp == nil: chunk in v6%d\n", chunk);
				goto raise;
			}
			blklen = chunk;
			if(BLEN(xp) < chunk)
				blklen = BLEN(xp);
			memmove(nb->wp, xp->rp, blklen);

			nb->wp += blklen;
			xp->rp += blklen;
			chunk -= blklen;
			if(xp->rp == xp->wp)
				xp = xp->next;
		}
		ipifcoput(ifc, nb, V6, gate);
		ip->stats[FragCreates]++;
	}
	ip->stats[FragOKs]++;

raise:
	runlock(ifc);
	poperror();
free:
	freeblist(bp);
	return rv;
}

void
ipiput6(Fs *f, Ipifc *ifc, Block *bp)
{
	int hl, len, hop, tos;
	IP *ip;
	Ip6hdr *h;
	Proto *p;

	ip = f->ip;
	ip->stats[InReceives]++;

	/*
	 *  Ensure we have all the header info in the first
	 *  block.  Make life easier for other protocols by
	 *  collecting up to the first 64 bytes in the first block.
	 */
	if(BLEN(bp) < 64) {
		hl = blocklen(bp);
		if(hl < IP6HDR)
			hl = IP6HDR;
		if(hl > 64)
			hl = 64;
		bp = pullupblock(bp, hl);
		if(bp == nil)
			return;
	}

	/* Check header version */
	h = (Ip6hdr*)bp->rp;
	if((h->vcf[0] & 0xF0) != IP_VER6) {
		ip->stats[InHdrErrors]++;
		netlog(f, Logip, "ip: bad version %ux\n", (h->vcf[0]&0xF0)>>2);
		goto drop;
	}
	len = IP6HDR + nhgets(h->ploadlen);
	if((bp = trimblock(bp, 0, len)) == nil){
		ip->stats[InHdrErrors]++;
		netlog(f, Logip, "%I -> %I: bogus packet length: %d\n", h->src, h->dst, len);
		return;
	}
	h = (Ip6hdr*)bp->rp;

	/* route */
	if(!ipforme(f, h->dst)) {
		Route *r;
		Routehint rh;
		Ipifc *nifc;

		if(!ip->iprouting)
			goto drop;

		/* don't forward to link-local destinations */
		if(islinklocal(h->dst) ||
		   (isv6mcast(h->dst) && (h->dst[1]&0xF) <= Link_local_scop)){
			ip->stats[OutDiscards]++;
			goto drop;
		}
			
		/* don't forward to source's network */
		rh.r = nil;
		r  = v6lookup(f, h->dst, h->src, &rh);
		if(r == nil || (nifc = r->ifc) == nil || (r->type & Rv4) != 0
		|| (nifc == ifc && !ifc->reflect)){
			ip->stats[OutDiscards]++;
			goto drop;
		}

		/* don't forward if packet has timed out */
		hop = h->ttl;
		if(hop < 1) {
			ip->stats[InHdrErrors]++;
			icmpttlexceeded6(f, ifc, bp);
			goto drop;
		}

		/* process headers & reassemble if the interface expects it */
		bp = procxtns(ip, bp, nifc->reassemble);
		if(bp == nil)
			return;

		ip->stats[ForwDatagrams]++;
		h = (Ip6hdr*)bp->rp;
		tos = (h->vcf[0]&0x0F)<<2 | (h->vcf[1]&0xF0)>>2;
		hop = h->ttl;
		ipoput6(f, bp, 1, hop-1, tos, &rh);
		return;
	}

	/* reassemble & process headers if needed */
	bp = procxtns(ip, bp, 1);
	if(bp == nil)
		return;

	h = (Ip6hdr*)bp->rp;
	p = Fsrcvpcol(f, h->proto);
	if(p != nil && p->rcv != nil) {
		ip->stats[InDelivers]++;
		(*p->rcv)(p, ifc, bp);
		return;
	}

	ip->stats[InDiscards]++;
	ip->stats[InUnknownProtos]++;
drop:
	freeblist(bp);
}

/*
 * ipfragfree6 - copied from ipfragfree4 - assume hold fraglock6
 */
static void
ipfragfree6(IP *ip, Fragment6 *frag)
{
	Fragment6 *fl, **l;

	if(frag->blist != nil)
		freeblist(frag->blist);
	frag->blist = nil;
	frag->id = 0;
	memset(frag->src, 0, IPaddrlen);
	memset(frag->dst, 0, IPaddrlen);

	l = &ip->flisthead6;
	for(fl = *l; fl != nil; fl = fl->next) {
		if(fl == frag) {
			*l = frag->next;
			break;
		}
		l = &fl->next;
	}

	frag->next = ip->fragfree6;
	ip->fragfree6 = frag;
}

/*
 * ipfragallo6 - copied from ipfragalloc4
 */
static Fragment6*
ipfragallo6(IP *ip)
{
	Fragment6 *f;

	while(ip->fragfree6 == nil) {
		/* free last entry on fraglist */
		for(f = ip->flisthead6; f->next != nil; f = f->next)
			;
		ipfragfree6(ip, f);
	}
	f = ip->fragfree6;
	ip->fragfree6 = f->next;
	f->next = ip->flisthead6;
	ip->flisthead6 = f;
	f->age = NOW + 30000;

	return f;
}

static Block*
procxtns(IP *ip, Block *bp, int doreasm)
{
	uchar proto;
	int offset;

	offset = unfraglen(bp, &proto, 0, doreasm);
	if(offset >= IP6HDR && proto == FH && doreasm) {
		bp = ip6reassemble(ip, offset, bp);
		if(bp == nil)
			return nil;
		offset = unfraglen(bp, &proto, 0, 0);
		if(proto == FH)
			offset = -1;
	}
	if(offset < IP6HDR){
		ip->stats[InHdrErrors]++;
		ip->stats[InDiscards]++;
		freeblist(bp);
		return nil;
	}
	if(proto == DOH || offset > IP6HDR)
		bp = procopts(bp);
	return bp;
}

/*
 * returns length of "Unfragmentable part", i.e., sum of lengths of ipv6 hdr,
 * hop-by-hop & routing headers if present; *nexthdr is set to nexthdr value
 * of the last header in the "Unfragmentable part"; if setfh != 0, nexthdr
 * field of the last header in the "Unfragmentable part" is set to FH.
 * When the last header is a fragment header and popfh != 0 then set
 * the nexthdr value of the previous header to the nexthdr value of the
 * fragment header. returns -1 on error.
 */
static int
unfraglen(Block *bp, uchar *nexthdr, int setfh, int popfh)
{
	uchar *e, *p, *q;

	e = bp->wp;
	p = bp->rp;
	q = p+6;   /* proto, = p+sizeof(Ip6hdr.vcf)+sizeof(Ip6hdr.ploadlen) */
	*nexthdr = *q;
	p += IP6HDR;
	while(*nexthdr == HBH || *nexthdr == RH){
		if(p+2 > e)
			return -1;
		q = p;
		*nexthdr = *q;
		p += ((int)p[1] + 1) * 8;
	}
	if(p > e)
		return -1;
	if(*nexthdr == FH){
		if(p+IP6FHDR > e || *p == FH)
			return -1;
		if(popfh)
			*q = *p;
	} else if(setfh)
		*q = FH;
	return p - bp->rp;
}

static Block*
procopts(Block *bp)
{
	return bp;
}

static Block*
ip6reassemble(IP* ip, int uflen, Block* bp)
{
	int offset, ovlap, fragsize, len;
	uchar src[IPaddrlen], dst[IPaddrlen];
	uint id;
	Block *bl, **l, *prev;
	Fraghdr6 *fraghdr;
	Fragment6 *f, *fnext;
	Ipfrag *fp, *fq;
	Ip6hdr* ih;

	/*
	 *  block lists are too hard, concatblock into a single block
	 */
	bp = concatblock(bp);

	ih = (Ip6hdr*)bp->rp;
	fraghdr = (Fraghdr6*)(bp->rp + uflen);
	id = nhgetl(fraghdr->id);
	offset = nhgets(fraghdr->offsetRM);
	fragsize = BLEN(bp) - uflen - IP6FHDR;

	memmove(src, ih->src, IPaddrlen);
	memmove(dst, ih->dst, IPaddrlen);

	qlock(&ip->fraglock6);

	/*
	 *  find a reassembly queue for this fragment
	 */
	for(f = ip->flisthead6; f != nil; f = fnext){
		fnext = f->next;
		if(f->id == id && ipcmp(f->src, src) == 0 && ipcmp(f->dst, dst) == 0)
			break;
		if(f->age < NOW){
			ip->stats[ReasmTimeout]++;
			ipfragfree6(ip, f);
		}
	}

	/*
	 *  if this isn't a fragmented packet, accept it
	 *  and get rid of any fragments that might go
	 *  with it.
	 */
	if(offset == 0) {		/* 1st frag is also last */
		if(f != nil) {
			ip->stats[ReasmFails]++;
			ipfragfree6(ip, f);
		}
		qunlock(&ip->fraglock6);

		/* get rid of frag header */
		memmove(bp->rp + IP6FHDR, bp->rp, uflen);
		bp->rp += IP6FHDR;
		ih = (Ip6hdr*)bp->rp;
		hnputs(ih->ploadlen, BLEN(bp)-IP6HDR);

		return bp;
	}

	if(bp->base+IPFRAGSZ > bp->rp){
		bp = padblock(bp, IPFRAGSZ);
		bp->rp += IPFRAGSZ;
	}

	fp = (Ipfrag*)bp->base;
	fp->foff = offset & ~7;
	fp->flen = fragsize;

	/* First fragment allocates a reassembly queue */
	if(f == nil) {
		f = ipfragallo6(ip);
		f->id = id;
		memmove(f->src, src, IPaddrlen);
		memmove(f->dst, dst, IPaddrlen);

		f->blist = bp;

		ip->stats[ReasmReqds]++;
		qunlock(&ip->fraglock6);

		return nil;
	}

	/*
	 *  find the new fragment's position in the queue
	 */
	prev = nil;
	l = &f->blist;
	bl = f->blist;
	while(bl != nil && fp->foff > ((Ipfrag*)bl->base)->foff) {
		prev = bl;
		l = &bl->next;
		bl = bl->next;
	}

	/* Check overlap of a previous fragment - trim away as necessary */
	if(prev != nil) {
		fq = (Ipfrag*)prev->base;
		ovlap = fq->foff + fq->flen - fp->foff;
		if(ovlap > 0) {
			if(ovlap >= fp->flen) {
				qunlock(&ip->fraglock6);
				freeb(bp);
				return nil;
			}
			fq->flen -= ovlap;
		}
	}

	/* Link onto assembly queue */
	bp->next = *l;
	*l = bp;

	/* Check to see if succeeding segments overlap */
	if(bp->next != nil) {
		l = &bp->next;
		offset = fp->foff + fp->flen;

		/* Take completely covered segments out */
		while((bl = *l) != nil) {
			fq = (Ipfrag*)bl->base;
			ovlap = offset - fq->foff;
			if(ovlap <= 0)
				break;
			if(ovlap < fq->flen) {
				/* move up ip and frag header */
				memmove(bl->rp + ovlap, bl->rp, BLEN(bl) - fq->flen);
				bl->rp += ovlap;
				fq->flen -= ovlap;
				fq->foff += ovlap;
				break;
			}
			*l = bl->next;
			bl->next = nil;
			freeb(bl);
		}
	}

	/*
	 *  look for a complete packet.  if we get to a fragment
	 *  with the trailing bit of fraghdr->offsetRM[1] set, we're done.
	 */
	offset = 0;
	for(bl = f->blist; bl != nil; bl = bl->next, offset += fp->flen) {
		fp = (Ipfrag*)bl->base;
		if(fp->foff != offset)
			break;

		fraghdr = (Fraghdr6*)(bl->wp - fp->flen - IP6FHDR);
		if(fraghdr->offsetRM[1] & 1)
			continue;

		bl = f->blist;
		fq = (Ipfrag*)bl->base;

		/* get rid of frag header in first fragment */
		memmove(bl->rp + IP6FHDR, bl->rp, BLEN(bl) - fq->flen - IP6FHDR);
		bl->rp += IP6FHDR;
		len = BLEN(bl);

		/*
		 * Pullup all the fragment headers and
		 * return a complete packet
		 */
		for(bl = bl->next; bl != nil && len < IP_MAX; bl = bl->next) {
			fq = (Ipfrag*)bl->base;
			fragsize = fq->flen;
			bl->rp = bl->wp - fragsize;
			len += fragsize;
		}

		if(len >= IP_MAX){
			ipfragfree6(ip, f);
			ip->stats[ReasmFails]++;
			qunlock(&ip->fraglock6);

			return nil;
		}

		bl = f->blist;
		f->blist = nil;
		ipfragfree6(ip, f);

		ih = (Ip6hdr*)bl->rp;
		hnputs(ih->ploadlen, len-IP6HDR);

		ip->stats[ReasmOKs]++;
		qunlock(&ip->fraglock6);

		return bl;
	}
	qunlock(&ip->fraglock6);
	return nil;
}
