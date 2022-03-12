#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"ip.h"
#include	"ipv6.h"

char *v6hdrtypes[Maxhdrtype] =
{
	[HBH]		"HopbyHop",
	[ICMP]		"ICMP",
	[IGMP]		"IGMP",
	[GGP]		"GGP",
	[IPINIP]	"IP",
	[ST]		"ST",
	[TCP]		"TCP",
	[UDP]		"UDP",
	[ISO_TP4]	"ISO_TP4",
	[RH]		"Routinghdr",
	[FH]		"Fraghdr",
	[IDRP]		"IDRP",
	[RSVP]		"RSVP",
	[AH]		"Authhdr",
	[ESP]		"ESP",
	[ICMPv6]	"ICMPv6",
	[NNH]		"Nonexthdr",
	[ISO_IP]	"ISO_IP",
	[IGRP]		"IGRP",
	[OSPF]		"OSPF",
};

/*
 *  well known IPv6 addresses
 */
uchar v6Unspecified[IPaddrlen] = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};
uchar v6loopback[IPaddrlen] = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x01
};

uchar v6linklocal[IPaddrlen] = {
	0xfe, 0x80, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};
uchar v6linklocalmask[IPaddrlen] = {
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0, 0, 0, 0,
	0, 0, 0, 0
};
int v6llpreflen = 8;	/* link-local prefix length in bytes */

uchar v6multicast[IPaddrlen] = {
	0xff, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};
uchar v6multicastmask[IPaddrlen] = {
	0xff, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};
int v6mcpreflen = 1;	/* multicast prefix length */

uchar v6allnodesN[IPaddrlen] = {
	0xff, 0x01, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x01
};
uchar v6allroutersN[IPaddrlen] = {
	0xff, 0x01, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x02
};
uchar v6allnodesNmask[IPaddrlen] = {
	0xff, 0xff, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};
int v6aNpreflen = 2;	/* all nodes (N) prefix */

uchar v6allnodesL[IPaddrlen] = {
	0xff, 0x02, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x01
};
uchar v6allroutersL[IPaddrlen] = {
	0xff, 0x02, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x02
};
uchar v6allnodesLmask[IPaddrlen] = {
	0xff, 0xff, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};
int v6aLpreflen = 2;	/* all nodes (L) prefix */

uchar v6solicitednode[IPaddrlen] = {
	0xff, 0x02, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x01,
	0xff, 0, 0, 0
};
uchar v6solicitednodemask[IPaddrlen] = {
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0xff, 0x0, 0x0, 0x0
};
int v6snpreflen = 13;

ushort
ptclcsum(Block *bp, int offset, int len)
{
	uchar *addr;
	ulong losum, hisum;
	ushort csum;
	int odd, blocklen, x;

	/* Correct to front of data area */
	while(bp != nil && offset && offset >= BLEN(bp)) {
		offset -= BLEN(bp);
		bp = bp->next;
	}
	if(bp == nil)
		return 0;

	addr = bp->rp + offset;
	blocklen = BLEN(bp) - offset;

	if(bp->next == nil) {
		if(blocklen < len)
			len = blocklen;
		return ptclbsum(addr, len) ^ 0xffff;
	}

	losum = 0;
	hisum = 0;

	odd = 0;
	while(len) {
		x = blocklen;
		if(len < x)
			x = len;

		csum = ptclbsum(addr, x);
		if(odd)
			hisum += csum;
		else
			losum += csum;
		odd = (odd+x) & 1;
		len -= x;

		bp = bp->next;
		if(bp == nil)
			break;
		blocklen = BLEN(bp);
		addr = bp->rp;
	}

	losum += hisum>>8;
	losum += (hisum&0xff)<<8;
	while((csum = losum>>16) != 0)
		losum = csum + (losum & 0xffff);

	return losum ^ 0xffff;
}

enum
{
	Isprefix= 16,
};

#define CLASS(p) ((*(uchar*)(p))>>6)

void
ipv62smcast(uchar *smcast, uchar *a)
{
	assert(IPaddrlen == 16);
	memmove(smcast, v6solicitednode, IPaddrlen);
	smcast[13] = a[13];
	smcast[14] = a[14];
	smcast[15] = a[15];
}

/*
 *  parse a hex mac address
 */
int
parsemac(uchar *to, char *from, int len)
{
	char nip[4];
	char *p;
	int i;

	p = from;
	memset(to, 0, len);
	for(i = 0; i < len; i++){
		if(p[0] == '\0' || p[1] == '\0')
			break;

		nip[0] = p[0];
		nip[1] = p[1];
		nip[2] = '\0';
		p += 2;

		to[i] = strtoul(nip, 0, 16);
		if(*p == ':')
			p++;
	}
	return i;
}

/*
 *  return multicast version if any
 */
int
ipismulticast(uchar *ip)
{
	if(isv4(ip)){
		if(isv4mcast(&ip[IPv4off]))
			return V4;
	}
	else if(isv6mcast(ip))
		return V6;
	return 0;
}

/*
 *  return ip version of a connection
 */
int
convipvers(Conv *c)
{
	if(isv4(c->raddr) && isv4(c->laddr) || ipcmp(c->raddr, IPnoaddr) == 0)
		return V4;
	else
		return V6;
}

/*
 *  hashing tcp, udp, ... connections
 */
static ulong
iphash(uchar *sa, ushort sp, uchar *da, ushort dp)
{
	return ((sa[IPaddrlen-1]<<24) ^ (sp << 16) ^ (da[IPaddrlen-1]<<8) ^ dp ) % Nipht;
}

void
iphtadd(Ipht *ht, Iphash *h)
{
	ulong hv;

	if(ipcmp(h->raddr, IPnoaddr) != 0)
		h->match = IPmatchexact;
	else {
		if(ipcmp(h->laddr, IPnoaddr) != 0){
			if(h->lport == 0)
				h->match = IPmatchaddr;
			else
				h->match = IPmatchpa;
		} else {
			if(h->lport == 0)
				h->match = IPmatchany;
			else
				h->match = IPmatchport;
		}
	}
	lock(ht);
	hv = iphash(h->raddr, h->rport, h->laddr, h->lport);
	h->nextiphash = ht->tab[hv];
	ht->tab[hv] = h;
	unlock(ht);
}

void
iphtrem(Ipht *ht, Iphash *h)
{
	ulong hv;
	Iphash **l;

	lock(ht);
	hv = iphash(h->raddr, h->rport, h->laddr, h->lport);
	for(l = &ht->tab[hv]; (*l) != nil; l = &(*l)->nextiphash)
		if(*l == h){
			(*l) = h->nextiphash;
			h->nextiphash = nil;
			break;
		}
	unlock(ht);
}

/* look for a matching iphash with the following precedence
 *	raddr,rport,laddr,lport
 *	laddr,lport
 *	*,lport
 *	laddr,*
 *	*,*
 */
Iphash*
iphtlook(Ipht *ht, uchar *sa, ushort sp, uchar *da, ushort dp)
{
	ulong hv;
	Iphash *h;

	lock(ht);
	/* exact 4 pair match (connection) */
	hv = iphash(sa, sp, da, dp);
	for(h = ht->tab[hv]; h != nil; h = h->nextiphash){
		if(h->match != IPmatchexact)
			continue;
		if(sp == h->rport && dp == h->lport
		&& ipcmp(sa, h->raddr) == 0 && ipcmp(da, h->laddr) == 0){
			unlock(ht);
			return h;
		}
	}

	/* match local address and port */
	hv = iphash(IPnoaddr, 0, da, dp);
	for(h = ht->tab[hv]; h != nil; h = h->nextiphash){
		if(h->match != IPmatchpa)
			continue;
		if(dp == h->lport && ipcmp(da, h->laddr) == 0){
			unlock(ht);
			return h;
		}
	}

	/* match just port */
	hv = iphash(IPnoaddr, 0, IPnoaddr, dp);
	for(h = ht->tab[hv]; h != nil; h = h->nextiphash){
		if(h->match != IPmatchport)
			continue;
		if(dp == h->lport){
			unlock(ht);
			return h;
		}
	}

	/* match local address */
	hv = iphash(IPnoaddr, 0, da, 0);
	for(h = ht->tab[hv]; h != nil; h = h->nextiphash){
		if(h->match != IPmatchaddr)
			continue;
		if(ipcmp(da, h->laddr) == 0){
			unlock(ht);
			return h;
		}
	}

	/* look for something that matches anything */
	hv = iphash(IPnoaddr, 0, IPnoaddr, 0);
	for(h = ht->tab[hv]; h != nil; h = h->nextiphash){
		if(h->match != IPmatchany)
			continue;
		unlock(ht);
		return h;
	}
	unlock(ht);
	return nil;
}

/*
 * Move entry to front of Proto.translations
 * and update the timestamp.
 *
 * Proto is locked.
 */
static Translation*
transupdate(Proto *p, Translation *q)
{
	q->time = NOW;

	/* unlink */
	if(q->link != nil && (*q->link = q->next) != nil)
		q->next->link = q->link;

	/* link to front */
	if((q->next = p->translations) != nil)
		q->next->link = &q->next;
	p->translations = q;
	q->link = &p->translations;

	return q;
}

/*
 * Called with the 4-tuple (sa,sp,da,dp)
 * that should be source translated,
 * returning the translation.
 *
 * Proto is locked.
 */
Translation*
transforward(Proto *p, Ipht *ht, uchar *sa, int sp, uchar *da, int dp, Route *r)
{
	uchar ia[IPaddrlen];
	Routehint rh;
	Translation *q;
	Iphash *iph;
	Ipifc *ifc;
	int lport;
	ulong now;
	int num;

	/* Translation already exists? */
	iph = iphtlook(ht, sa, sp, da, dp);
	if(iph != nil) {
		if(iph->trans != 1)
			return nil;
		return transupdate(p, iphforward(iph));
	}

	/* Bad source address? */
	if(ipismulticast(sa) || ipforme(p->f, sa) != 0){
		netlog(p->f, Logtrans, "trans: bad source address: %s!%I!%d -> %I!%d\n",
			p->name, sa, sp, da, dp);
		return nil;
	}

	/* Bad forward route? */
	if(r == nil || (ifc = r->ifc) == nil){
		netlog(p->f, Logtrans, "trans: no forward route: %s!%I!%d -> %I!%d\n",
			p->name, sa, sp, da, dp);
		return nil;
	}

	/* Find a source address on the destination interface */
	rlock(ifc);
	memmove(ia, v4prefix, IPv4off);
	if(!ipv4local(ifc, ia+IPv4off, 0, (r->type & (Rifc|Runi|Rbcast|Rmulti))? da+IPv4off: r->v4.gate)){
		runlock(ifc);
		netlog(p->f, Logtrans, "trans: no source ip: %s!%I!%d -> %I!%d\n",
			p->name, sa, sp, da, dp);
		return nil;
	}
	runlock(ifc);

	/* Check backward route */
	rh.a = nil;
	rh.r = nil;
	if(ipismulticast(da))
		r = v4lookup(p->f, sa+IPv4off, ia+IPv4off, nil);
	else
		r = v4lookup(p->f, sa+IPv4off, da+IPv4off, &rh);
	if(r == nil || (r->ifc == ifc && !ifc->reflect)){
		netlog(p->f, Logtrans, "trans: bad backward route: %s!%I!%d <- %I <- %I!%d\n",
			p->name, sa, sp, ia, da, dp);
		return nil;
	}

	/* Find local port */
	lport = unusedlport(p);
	if(lport <= 0){
		netlog(p->f, Logtrans, "trans: no local port: %s!%I!%d <- %I <- %I!%d\n",
			p->name, sa, sp, ia, da, dp);
		return nil;
	}

	/* Reuse expired entries */
	num = 0;
	now = NOW;
	for(q = p->translations; q != nil; q = q->next) {
		if(++num >= 1000 || (now - q->time) >= 5*60*1000){
			netlog(p->f, Logtrans, "trans: removing %s!%I!%d -> %I!%d -> %I!%d\n",
				p->name,
				q->forward.raddr, q->forward.rport,
				q->backward.laddr, q->backward.lport,
				q->forward.laddr, q->forward.lport);

			iphtrem(ht, &q->forward);
			iphtrem(ht, &q->backward);
			break;
		}
	}
	if(q == nil){
		q = malloc(sizeof(*q));
		if(q == nil)
			return nil;
		q->link = nil;
	}

	/* Match what needs to be forwarded */
	q->forward.trans = 1;
	q->forward.lport = dp;
	q->forward.rport = sp;
	ipmove(q->forward.laddr, da);
	ipmove(q->forward.raddr, sa);

	/* Match what comes back to us */
	q->backward.trans = 2;
	q->backward.lport = lport;
	ipmove(q->backward.laddr, ia);
	if(p->ipproto == 1 || ipismulticast(da)){
		q->backward.rport = 0;
		ipmove(q->backward.raddr, IPnoaddr);
	} else {
		q->backward.rport = dp;
		ipmove(q->backward.raddr, da);
	}
	memmove(&q->Routehint, &rh, sizeof(rh));

	netlog(p->f, Logtrans, "trans: adding %s!%I!%d -> %I!%d -> %I!%d\n",
		p->name,
		q->forward.raddr, q->forward.rport,
		q->backward.laddr, q->backward.lport,
		q->forward.laddr, q->forward.lport);

	iphtadd(ht, &q->forward);
	iphtadd(ht, &q->backward);

	return transupdate(p, q);
}

/*
 * Check if backward translation is valid and
 * update timestamp.
 *
 * Proto is locked.
 */
Translation*
transbackward(Proto *p, Iphash *iph)
{
	if(iph == nil || iph->trans != 2)
		return nil;

	return transupdate(p, iphbackward(iph));
}

/*
 * Checksum adjusting hnputs()
 */
void
hnputs_csum(void *p, ushort v, uchar *pcsum)
{
	ulong csum;

	assert((((uchar*)p - pcsum) & 1) == 0);

	csum = nhgets(pcsum)^0xFFFF;
	csum += nhgets(p)^0xFFFF;
	csum += v;
	hnputs(p, v);
	while(v = csum >> 16)
		csum = (csum & 0xFFFF) + v;
	hnputs(pcsum, csum^0xFFFF);
}
