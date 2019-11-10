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
	QLock;
	Fs	*f;
	Arpent	*hash[NHASH];
	Arpent	cache[NCACHE];
	Arpent	*rxmt;
	Proc	*rxmitp;	/* neib sol re-transmit proc */
	Rendez	rxmtq;
	Block 	*dropf, *dropl;
};

char *Ebadarp = "bad arp";

#define haship(s) ((s)[IPaddrlen-1]%NHASH)

int 	ReTransTimer = RETRANS_TIMER;

static void 	rxmitproc(void *v);

void
arpinit(Fs *f)
{
	f->arp = smalloc(sizeof(Arp));
	f->arp->f = f;
	f->arp->rxmt = nil;
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

	for(l = &arp->rxmt; *l != nil; l = &((*l)->nextrxt)){
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
	for(l = &arp->hash[haship(a->ip)]; *l != nil; l = &((*l)->hash)){
		if(*l == a){
			*l = a->hash;
			break;
		}
	}
	a->hash = nil;

	/* dump waiting packets */
	bp = a->hold;
	a->hold = nil;
	if(isv4(a->ip))
		freeblistchain(bp);
	else {
		rxmtunchain(arp, a);

		/* queue icmp unreachable for rxmitproc later on, w/o arp lock */
		if(bp != nil){
			if(arp->dropf == nil)
				arp->dropf = bp;
			else
				arp->dropl->list = bp;
			arp->dropl = a->last;

			if(bp == arp->dropf)
				wakeup(&arp->rxmtq);
		}
	}
	a->last = nil;

	a->ifc = nil;
	a->ifcid = 0;

	a->state = 0;
	a->rxtsrem = 0;

	a->utime = 0;
	a->ctime = 0;

	memset(a->ip, 0, sizeof(a->ip));
	memset(a->mac, 0, sizeof(a->mac));
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
	for(f = a; f < e; f++){
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
	l = &arp->hash[haship(ip)];
	a->hash = *l;
	*l = a;

	return a;
}


/*
 *  fill in the media address if we have it.  Otherwise return an
 *  Arpent that represents the state of the address resolution FSM
 *  for ip.  Add the packet to be sent onto the list of packets
 *  waiting for ip->mac to be resolved.
 */
Arpent*
arpget(Arp *arp, Block *bp, int version, Ipifc *ifc, uchar *ip, uchar *mac)
{
	int hash;
	Arpent *a;
	uchar v6ip[IPaddrlen];

	if(version == V4){
		v4tov6(v6ip, ip);
		ip = v6ip;
	}

	qlock(arp);
	hash = haship(ip);
	for(a = arp->hash[hash]; a != nil; a = a->hash){
		if(a->ifc == ifc && a->ifcid == ifc->ifcid && ipcmp(ip, a->ip) == 0)
			break;
	}
	if(a == nil){
		a = newarpent(arp, ip, ifc);
		a->state = AWAIT;
	}
	a->utime = NOW;
	if(a->state == AWAIT){
		if(bp != nil){
			bp->list = nil; 
			if(a->hold == nil)
				a->hold = bp;
			else
				a->last->list = bp;
			a->last = bp;
		}
		return a;		/* return with arp qlocked */
	}

	memmove(mac, a->mac, ifc->m->maclen);

	/* remove old entries */
	if(NOW - a->ctime > 15*60*1000)
		cleanarpent(arp, a);

	qunlock(arp);
	return nil;
}

/*
 * called with arp locked
 */
void
arprelease(Arp *arp, Arpent*)
{
	qunlock(arp);
}

/*
 * Copy out the mac address from the Arpent.  Return the
 * block waiting to get sent to this mac address.
 *
 * called with arp locked
 */
Block*
arpresolve(Arp *arp, Arpent *a, Medium *type, uchar *mac)
{
	Block *bp;

	memmove(a->mac, mac, type->maclen);
	if(a->state == AWAIT && !isv4(a->ip)){
		rxmtunchain(arp, a);
		a->rxtsrem = 0;
	}
	a->state = AOK;
	a->ctime = a->utime = NOW;
	bp = a->hold;
	a->hold = a->last = nil;
	qunlock(arp);

	return bp;
}

int
arpenter(Fs *fs, int version, uchar *ip, uchar *mac, int n, uchar *ia, Ipifc *ifc, int refresh)
{
	uchar v6ip[IPaddrlen];
	Block *bp, *next;
	Arpent *a;
	Route *r;
	Arp *arp;

	if(ifc->m == nil || ifc->m->maclen != n || ifc->m->maclen == 0)
		return -1;

	switch(version){
	case V4:
		r = v4lookup(fs, ip, ia, nil);
		v4tov6(v6ip, ip);
		ip = v6ip;
		break;
	case V6:
		r = v6lookup(fs, ip, ia, nil);
		break;
	default:
		panic("arpenter: version %d", version);
		return -1;	/* to supress warnings */
	}

	if(r == nil || r->ifc != ifc || (r->type & (Rbcast|Rmulti)) != 0)
		return -1;

	arp = fs->arp;
	qlock(arp);
	for(a = arp->hash[haship(ip)]; a != nil; a = a->hash){
		if(a->ifc != ifc || a->ifcid != ifc->ifcid)
			continue;
		if(ipcmp(a->ip, ip) == 0){
			if(version == V4)
				ip += IPv4off;
			bp = arpresolve(arp, a, ifc->m, mac);	/* unlocks arp */
			for(; bp != nil; bp = next){
				next = bp->list;
				bp->list = nil;
				if(waserror()){
					freeblistchain(next);
					break;
				}
				ipifcoput(ifc, bp, version, ip);
				poperror();
			}
			return 1;
		}
	}

	if(refresh == 0){
		a = newarpent(arp, ip, ifc);
		a->state = AOK;
		a->ctime = a->utime = NOW;
		memmove(a->mac, mac, n);
	}
	qunlock(arp);

	return refresh == 0;
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
		qlock(arp);
		for(a = arp->cache; a < &arp->cache[NCACHE]; a++){
			memset(a->ip, 0, sizeof(a->ip));
			memset(a->mac, 0, sizeof(a->mac));
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
		arp->rxmt = nil;
		qunlock(arp);
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
		if (n != 2)
			error(Ebadarg);
		if (parseip(ip, f[1]) == -1)
			error(Ebadip);
		qlock(arp);
		for(a = arp->hash[haship(ip)]; a != nil; a = x){
			x = a->hash;
			if(ipcmp(ip, a->ip) == 0)
				cleanarpent(arp, a);
		}
		qunlock(arp);
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
		qlock(arp);
		state = arpstate[a->state];
		ipmove(ip, a->ip);
		if(ifc->m == nil || a->ifcid != ifc->ifcid || !ipv6local(ifc, ia, 0, ip)){
			qunlock(arp);
			runlock(ifc);
			continue;
		}
		mname = ifc->m->name;
		convmac(mac, a->mac, ifc->m->maclen);
		qunlock(arp);
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
ndpsendsol(Fs *f, Ipifc *ifc, Arpent *a)
{
	uchar targ[IPaddrlen], src[IPaddrlen];
	Arpent **l;

	a->ctime = NOW;
	if(a->rxtsrem == 0)
		a->rxtsrem = MAX_MULTICAST_SOLICIT;
	else
		a->rxtsrem--;

	/* put on end of re-transmit chain */
	for(l = rxmtunchain(f->arp, a); *l != nil; l = &(*l)->nextrxt)
		;
	*l = a;

	if(l == &f->arp->rxmt)
		wakeup(&f->arp->rxmtq);

	/* try to use source address of original packet */
	ipmove(targ, a->ip);
	if(a->last != nil){
		ipmove(src, ((Ip6hdr*)a->last->rp)->src);
		arprelease(f->arp, a);

		if(iplocalonifc(ifc, src) != nil || ipproxyifc(f, ifc, src))
			goto send;
	} else {
		arprelease(f->arp, a);
	}
	if(!ipv6local(ifc, src, 0, targ))
		return;
send:
	if(!waserror()){
		icmpns(f, src, SRC_UNI, targ, TARG_MULTI, ifc->mac);
		poperror();
	}
}

static void
rxmitsols(Arp *arp)
{
	Block *next, *bp;
	Arpent *a;
	Ipifc *ifc;
	Route *r;

	qlock(arp);
	while((a = arp->rxmt) != nil && NOW - a->ctime > 3*ReTransTimer/4){
		if(a->rxtsrem > 0 && (ifc = a->ifc) != nil && canrlock(ifc)){
			if(a->ifcid == ifc->ifcid){
				ndpsendsol(arp->f, ifc, a);	/* unlocks arp */
				runlock(ifc);
				qlock(arp);
				continue;
			}
			runlock(ifc);
		}
		cleanarpent(arp, a);
	}
	bp = arp->dropf;
	arp->dropf = arp->dropl = nil;
	qunlock(arp);

	for(; bp != nil; bp = next){
		next = bp->list;
		bp->list = nil;
		r = v6lookup(arp->f, ((Ip6hdr*)bp->rp)->src, ((Ip6hdr*)bp->rp)->dst, nil);
		if(r != nil && (ifc = r->ifc) != nil && canrlock(ifc)){
			if(!waserror()){
				icmphostunr6(arp->f, ifc, bp, Icmp6_adr_unreach, (r->type & Runi) != 0);
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

	return arp->rxmt != nil || arp->dropf != nil;
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
		rxmitsols(arp);
		tsleep(&arp->rxmtq, return0, nil, ReTransTimer/4);
	}
}
