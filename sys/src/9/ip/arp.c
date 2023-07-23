#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include "ip.h"
#include "ipv6.h"

/*
 *  address resolution tables
 */

enum
{
	NHASH		= (1<<6),
	NCACHE		= 256,

	AOK		= 1,
	AWAIT		= 2,

	MAXAGE_TIMER	= 15*60*1000,
};

char *arpstate[] =
{
	"UNUSED",
	"OK",
	"WAIT",
};

/*
 *  one per Fs
 */
struct Arp
{
	RWlock;
	Fs	*f;
	Arpent	*hash[NHASH];
	Arpent	cache[NCACHE];
	Arpent	*rxmt[2];
	Proc	*rxmitp;	/* neib sol re-transmit proc */
	Rendez	rxmtq;
	Block 	*dropf, *dropl;
};

char *Ebadarp = "bad arp";

/* quick hash for ip addresses */
#define hashipa(a) (((a)[IPaddrlen-2] + (a)[IPaddrlen-1])%NHASH)

static void 	rxmitproc(void*);

void
arpinit(Fs *f)
{
	f->arp = smalloc(sizeof(Arp));
	f->arp->f = f;
	f->arp->rxmt[0] = nil;
	f->arp->rxmt[1] = nil;
	f->arp->dropf = f->arp->dropl = nil;
	kproc("rxmitproc", rxmitproc, f->arp);
}

static void
freeblistchain(Block *bp)
{
	Block *next;

	while(bp != nil){
		next = bp->list;
		freeblist(bp);
		bp = next;
	}
}

/* take out of re-transmit chain */
static Arpent**
rxmtunchain(Arp *arp, Arpent *a)
{
	Arpent **l;

	for(l = &arp->rxmt[isv4(a->ip) != 0]; *l != nil; l = &((*l)->nextrxt)){
		if(*l == a){
			*l = a->nextrxt;
			break;
		}
	}
	a->nextrxt = nil;
	return l;
}

static void
cleanarpent(Arp *arp, Arpent *a)
{
	Arpent **l;
	Block *bp;

	/* take out of current chain */
	for(l = &arp->hash[hashipa(a->ip)]; *l != nil; l = &((*l)->hash)){
		if(*l == a){
			*l = a->hash;
			break;
		}
	}
	a->hash = nil;

	/* remove from retransmit / timout chain */
	rxmtunchain(arp, a);

	/* queue packets for icmp unreachable for rxmitproc later on, w/o arp lock */
	if((bp = a->hold) != nil){
		if(arp->dropf == nil)
			arp->dropf = bp;
		else
			arp->dropl->list = bp;
		arp->dropl = a->last;
		if(bp == arp->dropf)
			wakeup(&arp->rxmtq);
	}
	a->hold = nil;
	a->last = nil;

	a->ifc = nil;
	a->ifcid = 0;

	a->state = 0;
	a->rxtsrem = 0;

	a->utime = 0;
	a->ctime = 0;
}

/*
 *  create a new arp entry for an ip address on ifc.
 */
static Arpent*
newarpent(Arp *arp, uchar *ip, Ipifc *ifc)
{
	Arpent *a, *e, *f, **l;
	ulong t;

	/* find oldest entry */
	e = &arp->cache[NCACHE];
	a = arp->cache;
	t = a->utime;
	for(f = a+1; t > 0 && f < e; f++){
		if(f->utime < t){
			t = f->utime;
			a = f;
		}
	}
	cleanarpent(arp, a);

	ipmove(a->ip, ip);
	a->ifc = ifc;
	a->ifcid = ifc->ifcid;

	/* insert into new chain */
	l = &arp->hash[hashipa(ip)];
	a->hash = *l;
	*l = a;

	return a;
}

static Arpent*
arplookup(Arp *arp, Ipifc *ifc, uchar *ip)
{
	Arpent *a;

	for(a = arp->hash[hashipa(ip)]; a != nil; a = a->hash){
		if(a->ifc == ifc && a->ifcid == ifc->ifcid && ipcmp(ip, a->ip) == 0)
			return a;
	}
	return nil;
}

static int
arphit(Arpent *a, uchar *mac, int maclen, Routehint *rh)
{
	if(a->state == AOK){
		memmove(mac, a->mac, maclen);
		a->utime = NOW;
		if(a->utime - a->ctime < MAXAGE_TIMER){
			if(rh != nil)
				rh->a = a;
			return 1;
		}
	}
	return 0;
}

/*
 *  fill in the media address if we have it.  Otherwise return an
 *  Arpent that represents the state of the address resolution FSM
 *  for ip.  Add the packet to be sent onto the list of packets
 *  waiting for ip->mac to be resolved.
 */
Arpent*
arpget(Arp *arp, Block *bp, int version, Ipifc *ifc, uchar *ip, uchar *mac, Routehint *rh)
{
	uchar v6ip[IPaddrlen];
	Arpent *a;

	if(version == V4){
		v4tov6(v6ip, ip);
		ip = v6ip;
	}

	if(rh != nil
	&& (a = rh->a) != nil
	&& a->ifc == ifc
	&& a->ifcid == ifc->ifcid
	&& ipcmp(ip, a->ip) == 0
	&& arphit(a, mac, ifc->m->maclen, nil))
		return nil;

	rlock(arp);
	if((a = arplookup(arp, ifc, ip)) != nil
	&& arphit(a, mac, ifc->m->maclen, rh)){
		runlock(arp);
		return nil;
	}
	if(rh != nil)
		rh->a = nil;
	runlock(arp);

	wlock(arp);
	if((a = arplookup(arp, ifc, ip)) == nil)
		a = newarpent(arp, ip, ifc);
	else if(arphit(a, mac, ifc->m->maclen, rh)){
		wunlock(arp);
		return nil;
	}
	a->state = AWAIT;
	a->utime = NOW;
	if(bp != nil){
		/* needs to be a single block */
		assert(bp->list == nil);

		if(a->hold == nil)
			a->hold = bp;
		else
			a->last->list = bp;
		a->last = bp;
	}
	return a;		/* return with arp locked */
}

/*
 * continue address resolution for the entry,
 * schedule it on the retransmit / timeout chains
 * and unlock the arp cache.
 */
void
arpcontinue(Arp *arp, Arpent *a)
{
	Arpent **l;
	Block *bp;

	/* remove all but the last message */
	while((bp = a->hold) != nil){
		if(bp == a->last)
			break;
		a->hold = bp->list;
		freeblist(bp);
	}

	/* try to keep it around for a second more */
	a->ctime = a->utime = NOW;

	/* put on end of re-transmit / timeout chain */
	for(l = rxmtunchain(arp, a); *l != nil; l = &(*l)->nextrxt)
		;
	*l = a;

	if(l == &arp->rxmt[0] || l == &arp->rxmt[1])
		wakeup(&arp->rxmtq);

	wunlock(arp);
}

/*
 * called with arp locked
 */
void
arprelease(Arp *arp, Arpent*)
{
	wunlock(arp);
}


/*
 * Copy out the mac address from the Arpent.  Return the
 * first block waiting to get sent to this mac address.
 *
 * called with arp locked
 */
Block*
arpresolve(Arp *arp, Arpent *a, uchar *mac, Routehint *rh)
{
	Block *bp;

	memmove(a->mac, mac, a->ifc->m->maclen);
	if(a->state == AWAIT) {
		rxmtunchain(arp, a);
		a->rxtsrem = 0;
	}
	bp = a->hold;
	a->hold = a->last = nil;
	a->ctime = a->utime = NOW;
	a->state = AOK;
	if(rh != nil)
		rh->a = a;
	wunlock(arp);

	if(bp != nil){
		freeblistchain(bp->list);
		bp->list = nil;
	}
	return bp;
}

int
arpenter(Fs *fs, int version, uchar *ip, uchar *mac, int maclen, uchar *ia, Ipifc *ifc, int refresh)
{
	Routehint rh;
	uchar v6ip[IPaddrlen];
	Block *bp, *next;
	Arpent *a;
	Route *r;
	Arp *arp;

	if(ifc->m == nil || maclen < ifc->m->maclen || ifc->m->maclen == 0)
		return -1;

	rh.r = nil;
	rh.a = nil;
	switch(version){
	case V4:
		r = v4lookup(fs, ip, ia, &rh);
		v4tov6(v6ip, ip);
		ip = v6ip;
		break;
	case V6:
		r = v6lookup(fs, ip, ia, &rh);
		break;
	default:
		panic("arpenter: version %d", version);
		return -1;	/* to supress warnings */
	}

	if(r == nil || r->ifc != ifc || (r->type & (Rbcast|Rmulti)) != 0)
		return -1;

	arp = fs->arp;

	wlock(arp);
	if((a = arplookup(arp, ifc, ip)) == nil){
		if(refresh){
			wunlock(arp);
			return 0;
		}
		a = newarpent(arp, ip, ifc);
	}
	bp = a->hold;
	a->hold = a->last = nil;
	arpresolve(arp, a, mac, &rh);	/* unlocks arp */
	if(version == V4)
		ip += IPv4off;
	for(; bp != nil; bp = next){
		next = bp->list;
		bp->list = nil;
		if(waserror()){
			freeblistchain(next);
			break;
		}
		ipifcoput(ifc, bp, version, ip, &rh);
		poperror();
	}
	return 1;
}

/*
 * arpforme() checks if we should respond to arp/ndp on a specific interface.
 * 
 * returns Runi if targ is a non-tentative local address on ifc.
 * returns Rproxy if we have a proxy route for targ to another interface.
 */
int
arpforme(Fs *fs, int version, uchar *targ, uchar *src, Ipifc *ifc)
{
	uchar ipv6[IPaddrlen];
	Iplifc *lifc;
	Route *r;

	if(version == V4) {
		v4tov6(ipv6, targ);
		targ = ipv6;
	}
	lifc = iplocalonifc(ifc, targ);
	if(lifc != nil){
		if(lifc->tentative)
			return 0;
		return Runi;
	}
	if(version == V4){
		targ += IPv4off;
		r = v4lookup(fs, targ, src, nil);
	} else {
		r = v6lookup(fs, targ, src, nil);
	}
	if(r == nil || r->ifc == ifc && r->ifcid == ifc->ifcid)
		return 0;
	return r->type & Rproxy;
}

int
arpwrite(Fs *fs, char *s, int len)
{
	int n;
	Arp *arp;
	Arpent *a, *x;
	Medium *m;
	Ipifc *ifc;
	char *f[5], buf[256];
	uchar ip[IPaddrlen], ia[IPaddrlen], mac[MAClen];

	arp = fs->arp;

	if(len == 0)
		error(Ebadarp);
	if(len >= sizeof(buf))
		len = sizeof(buf)-1;
	strncpy(buf, s, len);
	buf[len] = 0;
	if(len > 0 && buf[len-1] == '\n')
		buf[len-1] = 0;

	n = getfields(buf, f, nelem(f), 1, " ");
	if(strcmp(f[0], "flush") == 0){
		wlock(arp);
		for(a = arp->cache; a < &arp->cache[NCACHE]; a++){
			a->hash = nil;
			a->nextrxt = nil;
			a->ifc = nil;
			a->ifcid = 0;
			a->state = 0;
			a->rxtsrem = 0;
			a->ctime = 0;
			a->utime = 0;
			freeblistchain(a->hold);
			a->hold = a->last = nil;
		}
		memset(arp->hash, 0, sizeof(arp->hash));
		freeblistchain(arp->dropf);
		arp->dropf = arp->dropl = nil;
		arp->rxmt[0] = arp->rxmt[1] = nil;
		wunlock(arp);
	} else if(strcmp(f[0], "add") == 0){
		switch(n){
		default:
			error(Ebadarg);
		case 3:
			if(parseip(ip, f[1]) == -1)
				error(Ebadip);
			if((n = parsemac(mac, f[2], sizeof(mac))) <= 0)
				error(Ebadarp);
			findlocalip(fs, ia, ip);
			break;
		case 4:
			m = ipfindmedium(f[1]);
			if(m == nil || m->maclen == 0)
				error(Ebadarp);
			if(parseip(ip, f[2]) == -1)
				error(Ebadip);
			if((n = parsemac(mac, f[3], sizeof(mac))) != m->maclen)
				error(Ebadarp);
			findlocalip(fs, ia, ip);
			break;
		case 5:
			m = ipfindmedium(f[1]);
			if(m == nil || m->maclen == 0)
				error(Ebadarp);
			if(parseip(ip, f[2]) == -1)
				error(Ebadip);
			if((n = parsemac(mac, f[3], sizeof(mac))) != m->maclen)
				error(Ebadarp);
			if(parseip(ia, f[4]) == -1)
				error(Ebadip);
			break;
		}
		if((ifc = findipifc(fs, ia, ia, Runi)) == nil)
			error("no interface");
		rlock(ifc);
		if(!ipv6local(ifc, ia, 0, ip) || arpenter(fs, V6, ip, mac, n, ia, ifc, 0) < 0){
			runlock(ifc);
			error("destination unreachable");
		}
		runlock(ifc);
	} else if(strcmp(f[0], "del") == 0){
		if(n != 2)
			error(Ebadarg);
		if(parseip(ip, f[1]) == -1)
			error(Ebadip);
		wlock(arp);
		for(a = arp->hash[hashipa(ip)]; a != nil; a = x){
			x = a->hash;
			if(ipcmp(ip, a->ip) == 0)
				cleanarpent(arp, a);
		}
		wunlock(arp);
	} else if(strcmp(f[0], "garp") == 0){
		Iplifc *lifc;

		if(n != 2)
			error(Ebadarg);
		if(parseip(ip, f[1]) == -1)
			error(Ebadip);

		if((ifc = findipifc(fs, ip, ip, Runi)) == nil)
			error("no interface");

		rlock(ifc);
		if(waserror()){
			runlock(ifc);
			nexterror();
		}
		if(ifc->m != nil && ifc->m->areg != nil && (lifc = iplocalonifc(ifc, ip)) != nil)
			(*ifc->m->areg)(fs, ifc, lifc, ip);
		runlock(ifc);
		poperror();
	} else
		error(Ebadarp);

	return len;
}

static void
convmac(char *p, uchar *mac, int n)
{
	while(n-- > 0)
		p += sprint(p, "%2.2ux", *mac++);
}

int
arpread(Arp *arp, char *s, ulong offset, int len)
{
	char mac[2*MAClen+1], *state, *mname, *p;
	uchar ip[IPaddrlen], ia[IPaddrlen];
	Ipifc *ifc;
	Arpent *a;
	long n, o;

	p = s;
	o = -offset;
	for(a = arp->cache; len > 0 && a < &arp->cache[NCACHE]; a++){
		if(a->state == 0 || (ifc = a->ifc) == nil)
			continue;

		rlock(ifc);
		rlock(arp);
		ipmove(ip, a->ip);
		if(ifc->m == nil || a->state == 0 || a->ifc != ifc || a->ifcid != ifc->ifcid || !ipv6local(ifc, ia, 0, ip)){
			runlock(arp);
			runlock(ifc);
			continue;
		}
		mname = ifc->m->name;
		state = arpstate[a->state];
		convmac(mac, a->mac, ifc->m->maclen);
		runlock(arp);
		runlock(ifc);

		n = snprint(up->genbuf, sizeof up->genbuf,
			"%-6.6s %-4.4s %-40.40I %-16.16s %I\n",
			mname, state, ip, mac, ia);
		o += n;
		if(o <= 0)
			continue;
		if(n > len)
			break;
		memmove(p, up->genbuf, n);
		len -= n;
		p += n;
	}

	return p - s;
}

void
ndpsendsol(Fs *f, Arpent *a)
{
	Ipifc *ifc = a->ifc;
	uchar targ[IPaddrlen], src[IPaddrlen];

	if(a->rxtsrem == 0)
		a->rxtsrem = MAX_MULTICAST_SOLICIT;
	else
		a->rxtsrem--;

	/* try to use source address of original packet */
	ipmove(targ, a->ip);
	if(a->last != nil){
		ipmove(src, ((Ip6hdr*)a->last->rp)->src);
		arpcontinue(f->arp, a);
		if(arpforme(f, V6, src, targ, ifc))
			goto send;
	} else {
		arpcontinue(f->arp, a);
	}
	if(!ipv6local(ifc, src, 0, targ))
		return;
send:
	if(!waserror()){
		icmpns6(f, src, SRC_UNI, targ, TARG_MULTI, ifc->mac, ifc->m->maclen);
		poperror();
	}
}

static Block*
rxmt(Arp *arp)
{
	Arpent *a;
	Block *bp;
	Ipifc *ifc;

	wlock(arp);

	/* retransmit ipv6 solicitations */
	while((a = arp->rxmt[0]) != nil && NOW - a->ctime > 3*RETRANS_TIMER/4){
		if(a->rxtsrem > 0 && (ifc = a->ifc) != nil && canrlock(ifc)){
			if(a->ifcid == ifc->ifcid){
				ndpsendsol(arp->f, a);	/* unlocks arp */
				runlock(ifc);

				wlock(arp);
				continue;
			}
			runlock(ifc);
		}
		cleanarpent(arp, a);
	}

	/* timeout waiting ipv4 arp entries */
	while((a = arp->rxmt[1]) != nil && NOW - a->ctime > 3*RETRANS_TIMER)
		cleanarpent(arp, a);

	bp = arp->dropf;
	arp->dropf = arp->dropl = nil;

	wunlock(arp);

	return bp;
}

static void
drop(Fs *f, Block *bp)
{
	Routehint rh;
	Route *r;
	Ipifc *ifc;
	Block *next;

	for(; bp != nil; bp = next){
		next = bp->list;
		bp->list = nil;

		rh.r = nil;
		rh.a = nil;
		if((bp->rp[0]&0xF0) == IP_VER4)
			r = v4lookup(f, ((Ip4hdr*)bp->rp)->src, ((Ip4hdr*)bp->rp)->dst, &rh);
		else
			r = v6lookup(f, ((Ip6hdr*)bp->rp)->src, ((Ip6hdr*)bp->rp)->dst, &rh);
		if(r != nil && (ifc = r->ifc) != nil && canrlock(ifc)){
			if(!waserror()){
				if((bp->rp[0]&0xF0) == IP_VER4)
					icmpnohost(f, ifc, bp, &rh);
				else
					icmpnohost6(f, ifc, bp, &rh);
				poperror();
			}
			runlock(ifc);
		}
		freeblist(bp);
	}
}

static int
rxready(void *v)
{
	Arp *arp = (Arp *)v;

	return arp->rxmt[0] != nil || arp->rxmt[1] != nil || arp->dropf != nil;
}

static void
rxmitproc(void *v)
{
	Arp *arp = v;

	arp->rxmitp = up;
	if(waserror()){
		arp->rxmitp = nil;
		pexit("hangup", 1);
	}
	for(;;){
		sleep(&arp->rxmtq, rxready, v);
		drop(arp->f, rxmt(arp));
		tsleep(&arp->rxmtq, return0, nil, RETRANS_TIMER/4);
	}
}
