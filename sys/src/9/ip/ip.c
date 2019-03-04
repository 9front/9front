#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	"ip.h"

static char *statnames[] =
{
[Forwarding]	"Forwarding",
[DefaultTTL]	"DefaultTTL",
[InReceives]	"InReceives",
[InHdrErrors]	"InHdrErrors",
[InAddrErrors]	"InAddrErrors",
[ForwDatagrams]	"ForwDatagrams",
[InUnknownProtos]	"InUnknownProtos",
[InDiscards]	"InDiscards",
[InDelivers]	"InDelivers",
[OutRequests]	"OutRequests",
[OutDiscards]	"OutDiscards",
[OutNoRoutes]	"OutNoRoutes",
[ReasmTimeout]	"ReasmTimeout",
[ReasmReqds]	"ReasmReqds",
[ReasmOKs]	"ReasmOKs",
[ReasmFails]	"ReasmFails",
[FragOKs]	"FragOKs",
[FragFails]	"FragFails",
[FragCreates]	"FragCreates",
};

static Block*		ip4reassemble(IP*, int, Block*);
static void		ipfragfree4(IP*, Fragment4*);
static Fragment4*	ipfragallo4(IP*);

static void
initfrag(IP *ip, int size)
{
	Fragment4 *fq4, *eq4;
	Fragment6 *fq6, *eq6;

	ip->fragfree4 = (Fragment4*)malloc(sizeof(Fragment4) * size);
	if(ip->fragfree4 == nil)
		panic("initfrag");

	eq4 = &ip->fragfree4[size];
	for(fq4 = ip->fragfree4; fq4 < eq4; fq4++)
		fq4->next = fq4+1;

	ip->fragfree4[size-1].next = nil;

	ip->fragfree6 = (Fragment6*)malloc(sizeof(Fragment6) * size);
	if(ip->fragfree6 == nil)
		panic("initfrag");

	eq6 = &ip->fragfree6[size];
	for(fq6 = ip->fragfree6; fq6 < eq6; fq6++)
		fq6->next = fq6+1;

	ip->fragfree6[size-1].next = nil;
}

void
ip_init(Fs *f)
{
	IP *ip;

	ip = smalloc(sizeof(IP));
	ip->stats[DefaultTTL] = MAXTTL;
	initfrag(ip, 100);
	f->ip = ip;

	ip_init_6(f);
}

void
iprouting(Fs *f, int on)
{
	f->ip->iprouting = on;
	if(f->ip->iprouting==0)
		f->ip->stats[Forwarding] = 2;
	else
		f->ip->stats[Forwarding] = 1;
}

int
ipoput4(Fs *f, Block *bp, int gating, int ttl, int tos, Routehint *rh)
{
	Ipifc *ifc;
	uchar *gate;
	ulong fragoff;
	Block *xp, *nb;
	Ip4hdr *eh, *feh;
	int lid, len, seglen, chunk, hlen, dlen, blklen, offset, medialen;
	Route *r;
	IP *ip;
	int rv = 0;

	ip = f->ip;
	ip->stats[OutRequests]++;

	/* Fill out the ip header */
	eh = (Ip4hdr*)bp->rp;
	assert(BLEN(bp) >= IP4HDR);
	len = blocklen(bp);
	if(len >= IP_MAX){
		ip->stats[OutDiscards]++;
		netlog(f, Logip, "%V -> %V: exceeded ip max size: %d\n", eh->src, eh->dst, len);
		goto free;
	}

	r = v4lookup(f, eh->dst, eh->src, rh);
	if(r == nil || (ifc = r->ifc) == nil){
		ip->stats[OutNoRoutes]++;
		netlog(f, Logip, "%V -> %V: no interface\n", eh->src, eh->dst);
		rv = -1;
		goto free;
	}

	if(r->type & (Rifc|Runi|Rbcast|Rmulti))
		gate = eh->dst;
	else
		gate = r->v4.gate;

	if(!canrlock(ifc))
		goto free;
	if(waserror()){
		runlock(ifc);
		nexterror();
	}
	if(ifc->m == nil)
		goto raise;

	if(!gating){
		eh->vihl = IP_VER4|IP_HLEN4;
		eh->tos = tos;
	}
	eh->ttl = ttl;

	/* If we dont need to fragment just send it */
	medialen = ifc->maxtu - ifc->m->hsize;
	if(len <= medialen) {
		hnputs(eh->length, len);
		if(!gating){
			hnputs(eh->id, incref(&ip->id4));
			eh->frag[0] = 0;
			eh->frag[1] = 0;
		}
		eh->cksum[0] = 0;
		eh->cksum[1] = 0;
		hnputs(eh->cksum, ipcsum(&eh->vihl));

		ipifcoput(ifc, bp, V4, gate);
		runlock(ifc);
		poperror();
		return 0;
	}

	if(eh->frag[0] & (IP_DF>>8)){
		ip->stats[FragFails]++;
		ip->stats[OutDiscards]++;
		icmpcantfrag(f, bp, medialen);
		netlog(f, Logip, "%V -> %V: can't fragment with DF flag set\n", eh->src, eh->dst);
		goto raise;
	}

	hlen = (eh->vihl & 0xF)<<2;
	seglen = (medialen - hlen) & ~7;
	if(seglen < 8){
		ip->stats[FragFails]++;
		ip->stats[OutDiscards]++;
		netlog(f, Logip, "%V -> %V: can't fragment with seglen < 8\n", eh->src, eh->dst);
		goto raise;
	}

	dlen = len - hlen;
	xp = bp;
	if(gating)
		lid = nhgets(eh->id);
	else
		lid = incref(&ip->id4);

	offset = hlen;
	while(offset && offset >= BLEN(xp)) {
		offset -= BLEN(xp);
		xp = xp->next;
	}
	xp->rp += offset;

	if(gating)
		fragoff = nhgets(eh->frag)<<3;
	else
		fragoff = 0;
	dlen += fragoff;
	for(; fragoff < dlen; fragoff += seglen) {
		nb = allocb(hlen+seglen);
		feh = (Ip4hdr*)nb->rp;

		memmove(nb->wp, eh, hlen);
		nb->wp += hlen;

		if((fragoff + seglen) >= dlen) {
			seglen = dlen - fragoff;
			hnputs(feh->frag, fragoff>>3);
		}
		else
			hnputs(feh->frag, (fragoff>>3)|IP_MF);

		hnputs(feh->length, seglen + hlen);
		hnputs(feh->id, lid);

		/* Copy up the data area */
		chunk = seglen;
		while(chunk) {
			if(xp == nil) {
				ip->stats[OutDiscards]++;
				ip->stats[FragFails]++;
				freeblist(nb);
				netlog(f, Logip, "xp == nil: chunk %d\n", chunk);
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

		feh->cksum[0] = 0;
		feh->cksum[1] = 0;
		hnputs(feh->cksum, ipcsum(&feh->vihl));

		ipifcoput(ifc, nb, V4, gate);
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
ipiput4(Fs *f, Ipifc *ifc, Block *bp)
{
	int hl, len, hop, tos;
	uchar v6dst[IPaddrlen];
	ushort frag;
	Ip4hdr *h;
	Proto *p;
	IP *ip;

	if((bp->rp[0]&0xF0) != IP_VER4) {
		ipiput6(f, ifc, bp);
		return;
	}

	ip = f->ip;
	ip->stats[InReceives]++;

	/*
	 *  Ensure we have all the header info in the first
	 *  block.  Make life easier for other protocols by
	 *  collecting up to the first 64 bytes in the first block.
	 */
	if(BLEN(bp) < 64) {
		hl = blocklen(bp);
		if(hl < IP4HDR)
			hl = IP4HDR;
		if(hl > 64)
			hl = 64;
		bp = pullupblock(bp, hl);
		if(bp == nil)
			return;
	}

	h = (Ip4hdr*)bp->rp;
	hl = (h->vihl & 0xF)<<2;
	if(hl < IP4HDR || hl > BLEN(bp)) {
		ip->stats[InHdrErrors]++;
		netlog(f, Logip, "%V -> %V: bad ip header length: %d\n", h->src, h->dst, hl);
		goto drop;
	}
	if((bp->flag & Bipck) == 0 && ipcsum(&h->vihl)) {
		ip->stats[InHdrErrors]++;
		netlog(f, Logip, "%V -> %V: bad ip header checksum\n", h->src, h->dst);
		goto drop;
	}
	len = nhgets(h->length);
	if(len < hl || (bp = trimblock(bp, 0, len)) == nil){
		ip->stats[InHdrErrors]++;
		netlog(f, Logip, "%V -> %V: bogus packet length: %d\n", h->src, h->dst, len);
		if(bp != nil)
			goto drop;
		return;
	}
	h = (Ip4hdr*)bp->rp;

	/* route */
	v4tov6(v6dst, h->dst);
	if(!ipforme(f, v6dst)) {
		Route *r;
		Routehint rh;
		Ipifc *nifc;

		if(!ip->iprouting)
			goto drop;

		/* don't forward to source's network */
		rh.r = nil;
		r = v4lookup(f, h->dst, h->src, &rh);
		if(r == nil || (nifc = r->ifc) == nil
		|| (nifc == ifc && !ifc->reflect)){
			ip->stats[OutDiscards]++;
			goto drop;
		}

		/* don't forward if packet has timed out */
		hop = h->ttl;
		if(hop < 1) {
			ip->stats[InHdrErrors]++;
			icmpttlexceeded(f, ifc, bp);
			goto drop;
		}

		/* reassemble if the interface expects it */
		if(nifc->reassemble){
			frag = nhgets(h->frag);
			if(frag & ~IP_DF) {
				bp = ip4reassemble(ip, frag, bp);
				if(bp == nil)
					return;
				h = (Ip4hdr*)bp->rp;
			}
		}

		ip->stats[ForwDatagrams]++;
		tos = h->tos;
		hop = h->ttl;
		ipoput4(f, bp, 1, hop - 1, tos, &rh);
		return;
	}

	/* If this is not routed strip off the options */
	if(hl > IP4HDR) {
		hl -= IP4HDR;
		len -= hl;
		bp->rp += hl;
		memmove(bp->rp, h, IP4HDR);
		h = (Ip4hdr*)bp->rp;
		h->vihl = IP_VER4|IP_HLEN4;
		hnputs(h->length, len);
	}

	frag = nhgets(h->frag);
	if(frag & ~IP_DF) {
		bp = ip4reassemble(ip, frag, bp);
		if(bp == nil)
			return;
		h = (Ip4hdr*)bp->rp;
	}

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

int
ipstats(Fs *f, char *buf, int len)
{
	IP *ip;
	char *p, *e;
	int i;

	ip = f->ip;
	p = buf;
	e = p+len;
	for(i = 0; i < Nipstats; i++)
		p = seprint(p, e, "%s: %llud\n", statnames[i], ip->stats[i]);
	return p - buf;
}

static Block*
ip4reassemble(IP *ip, int offset, Block *bp)
{
	int ovlap, fragsize, len;
	ulong src, dst;
	ushort id;
	Block *bl, **l, *prev;
	Fragment4 *f, *fnext;
	Ipfrag *fp, *fq;
	Ip4hdr *ih;

	/*
	 *  block lists are too hard, concatblock into a single block
	 */
	bp = concatblock(bp);

	ih = (Ip4hdr*)bp->rp;
	src = nhgetl(ih->src);
	dst = nhgetl(ih->dst);
	id = nhgets(ih->id);
	fragsize = BLEN(bp) - ((ih->vihl&0xF)<<2);

	qlock(&ip->fraglock4);

	/*
	 *  find a reassembly queue for this fragment
	 */
	for(f = ip->flisthead4; f != nil; f = fnext){
		fnext = f->next;	/* because ipfragfree4 changes the list */
		if(f->id == id && f->src == src && f->dst == dst)
			break;
		if(f->age < NOW){
			ip->stats[ReasmTimeout]++;
			ipfragfree4(ip, f);
		}
	}

	/*
	 *  if this isn't a fragmented packet, accept it
	 *  and get rid of any fragments that might go
	 *  with it.
	 */
	if((offset & ~IP_DF) == 0) {
		if(f != nil) {
			ip->stats[ReasmFails]++;
			ipfragfree4(ip, f);
		}
		qunlock(&ip->fraglock4);
		return bp;
	}

	if(bp->base+IPFRAGSZ > bp->rp){
		bp = padblock(bp, IPFRAGSZ);
		bp->rp += IPFRAGSZ;
	}

	fp = (Ipfrag*)bp->base;
	fp->foff = (offset & 0x1fff)<<3;
	fp->flen = fragsize;

	/* First fragment allocates a reassembly queue */
	if(f == nil) {
		f = ipfragallo4(ip);
		f->id = id;
		f->src = src;
		f->dst = dst;

		f->blist = bp;

		ip->stats[ReasmReqds]++;
		qunlock(&ip->fraglock4);

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
				qunlock(&ip->fraglock4);
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
				/* move up ip header */
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
	 *  without IP_MF set, we're done.
	 */
	offset = 0;
	for(bl = f->blist; bl != nil; bl = bl->next, offset += fp->flen) {
		fp = (Ipfrag*)bl->base;
		if(fp->foff != offset)
			break;

		ih = (Ip4hdr*)bl->rp;
		if(ih->frag[0]&(IP_MF>>8))
			continue;

		bl = f->blist;
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
			ipfragfree4(ip, f);
			ip->stats[ReasmFails]++;
			qunlock(&ip->fraglock4);
			return nil;
		}

		bl = f->blist;
		f->blist = nil;
		ipfragfree4(ip, f);

		ih = (Ip4hdr*)bl->rp;
		ih->frag[0] = 0;
		ih->frag[1] = 0;
		hnputs(ih->length, len);

		ip->stats[ReasmOKs]++;
		qunlock(&ip->fraglock4);

		return bl;
	}
	qunlock(&ip->fraglock4);
	return nil;
}

/*
 * ipfragfree4 - Free a list of fragments - assume hold fraglock4
 */
static void
ipfragfree4(IP *ip, Fragment4 *frag)
{
	Fragment4 *fl, **l;

	if(frag->blist != nil)
		freeblist(frag->blist);
	frag->blist = nil;
	frag->id = 0;
	frag->src = 0;
	frag->dst = 0;

	l = &ip->flisthead4;
	for(fl = *l; fl != nil; fl = fl->next) {
		if(fl == frag) {
			*l = frag->next;
			break;
		}
		l = &fl->next;
	}

	frag->next = ip->fragfree4;
	ip->fragfree4 = frag;

}

/*
 * ipfragallo4 - allocate a reassembly queue - assume hold fraglock4
 */
static Fragment4*
ipfragallo4(IP *ip)
{
	Fragment4 *f;

	while(ip->fragfree4 == nil) {
		/* free last entry on fraglist */
		for(f = ip->flisthead4; f->next != nil; f = f->next)
			;
		ipfragfree4(ip, f);
	}
	f = ip->fragfree4;
	ip->fragfree4 = f->next;
	f->next = ip->flisthead4;
	ip->flisthead4 = f;
	f->age = NOW + 30000;

	return f;
}

ushort
ipcsum(uchar *addr)
{
	int len;
	ulong sum;

	sum = 0;
	len = (addr[0]&0xf)<<2;

	while(len > 0) {
		sum += addr[0]<<8 | addr[1] ;
		len -= 2;
		addr += 2;
	}

	sum = (sum & 0xffff) + (sum >> 16);
	sum = (sum & 0xffff) + (sum >> 16);

	return (sum^0xffff);
}
