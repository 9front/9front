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

/*
 *  create a new arp entry for an ip address on ifc.
 */
static Arpent*
newarp6(Arp *arp, uchar *ip, Ipifc *ifc, int addrxt)
{
	uint t;
	Block *xp;
	Arpent *a, *e, *f, **l;
	int empty;

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

	/* dump waiting packets */
	xp = a->hold;
	a->hold = nil;
	if(isv4(a->ip))
		freeblistchain(xp);
	else { /* queue icmp unreachable for rxmitproc later on, w/o arp lock */
		if(xp != nil){
			if(arp->dropf == nil) 
				arp->dropf = xp;
			else
				arp->dropl->list = xp;
			arp->dropl = a->last;
			wakeup(&arp->rxmtq);
		}
	}
	a->last = nil;

	/* take out of current chain */
	l = &arp->hash[haship(a->ip)];
	for(f = *l; f != nil; f = f->hash){
		if(f == a){
			*l = a->hash;
			break;
		}
		l = &f->hash;
	}

	/* insert into new chain */
	l = &arp->hash[haship(ip)];
	a->hash = *l;
	*l = a;

	ipmove(a->ip, ip);
	a->utime = NOW;
	a->ctime = 0;

	a->rtime = NOW + ReTransTimer;
	a->rxtsrem = MAX_MULTICAST_SOLICIT;
	a->ifc = ifc;
	a->ifcid = ifc->ifcid;

	/* put to the end of re-transmit chain; addrxt is 0 when isv4(a->ip) */
	if(!ipismulticast(a->ip) && addrxt){
		l = &arp->rxmt;
		empty = (*l == nil);

		for(f = *l; f != nil; f = f->nextrxt){
			if(f == a){
				*l = a->nextrxt;
				break;
			}
			l = &f->nextrxt;
		}
		for(f = *l; f != nil; f = f->nextrxt)
			l = &f->nextrxt;

		*l = a;
		if(empty) 
			wakeup(&arp->rxmtq);
	}

	a->nextrxt = nil;

	return a;
}

/* called with arp qlocked */

static void
cleanarpent(Arp *arp, Arpent *a)
{
	Arpent *f, **l;

	a->utime = 0;
	a->ctime = 0;
	a->state = 0;

	a->ifc = nil;
	a->ifcid = 0;
	
	/* take out of current chain */
	l = &arp->hash[haship(a->ip)];
	for(f = *l; f != nil; f = f->hash){
		if(f == a){
			*l = a->hash;
			break;
		}
		l = &f->hash;
	}

	/* take out of re-transmit chain */
	l = &arp->rxmt;
	for(f = *l; f != nil; f = f->nextrxt){
		if(f == a){
			*l = a->nextrxt;
			break;
		}
		l = &f->nextrxt;
	}
	a->nextrxt = nil;
	a->hash = nil;
	freeblistchain(a->hold);
	a->hold = a->last = nil;
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
		a = newarp6(arp, ip, ifc, (version != V4));
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
	Arpent *f, **l;

	if(!isv4(a->ip)){
		l = &arp->rxmt;
		for(f = *l; f != nil; f = f->nextrxt){
			if(f == a){
				*l = a->nextrxt;
				break;
			}
			l = &f->nextrxt;
		}
	}
	memmove(a->mac, mac, type->maclen);
	a->state = AOK;
	a->utime = NOW;
	bp = a->hold;
	a->hold = a->last = nil;
	qunlock(arp);

	return bp;
}

int
arpenter(Fs *fs, int version, uchar *ip, uchar *mac, int n, uchar *src, int refresh)
{
	Arp *arp;
	Route *r;
	Arpent *a, *f, **l;
	Ipifc *ifc;
	Block *bp, *next;
	Medium *m;
	uchar v6ip[IPaddrlen];

	arp = fs->arp;
	switch(version){
	case V4:
		r = v4lookup(fs, ip, src, nil);
		v4tov6(v6ip, ip);
		ip = v6ip;
		break;
	case V6:
		r = v6lookup(fs, ip, src, nil);
		break;
	default:
		panic("arpenter: version %d", version);
		return -1;	/* to supress warnings */
	}
	if(r == nil || (ifc = r->ifc) == nil || (m = ifc->m) == nil || m->maclen != n || m->maclen == 0)
		return -1;

	qlock(arp);
	for(a = arp->hash[haship(ip)]; a != nil; a = a->hash){
		if(a->state != AWAIT && a->state != AOK)
			continue;
		if(a->ifc != ifc || a->ifcid != ifc->ifcid)
			continue;
		if(ipcmp(a->ip, ip) == 0){
			a->state = AOK;
			memmove(a->mac, mac, n);

			if(version == V6){
				/* take out of re-transmit chain */
				l = &arp->rxmt;
				for(f = *l; f != nil; f = f->nextrxt){
					if(f == a){
						*l = a->nextrxt;
						break;
					}
					l = &f->nextrxt;
				}
			}

			bp = a->hold;
			a->hold = a->last = nil;
			if(version == V4)
				ip += IPv4off;
			a->utime = NOW;
			a->ctime = a->utime;
			qunlock(arp);

			while(bp != nil){
				if(!canrlock(ifc)){
					freeblistchain(bp);
					break;
				}
				if(ifc->m != m){
					runlock(ifc);
					freeblistchain(bp);
					break;
				}
				next = bp->list;
				bp->list = nil;
				if(waserror()){
					runlock(ifc);
					freeblistchain(next);
					break;
				}
				m->bwrite(ifc, concatblock(bp), version, ip);
				runlock(ifc);
				poperror();
				bp = next;
			}

			return 1;
		}
	}

	if(refresh == 0){
		a = newarp6(arp, ip, ifc, 0);
		a->state = AOK;
		a->ctime = NOW;
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
	char *f[5], buf[256];
	uchar ip[IPaddrlen], src[IPaddrlen], mac[MAClen];

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
			a->state = 0;
			a->utime = 0;
			a->ifc = nil;
			a->ifcid = 0;
			freeblistchain(a->hold);
			a->hold = a->last = nil;
		}
		memset(arp->hash, 0, sizeof(arp->hash));
		/* clear all pkts on these lists (rxmt, dropf/l) */
		arp->rxmt = nil;
		freeblistchain(arp->dropf);
		arp->dropf = arp->dropl = nil;
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
			findlocalip(fs, src, ip);
			break;
		case 4:
			m = ipfindmedium(f[1]);
			if(m == nil || m->maclen == 0)
				error(Ebadarp);
			if(parseip(ip, f[2]) == -1)
				error(Ebadip);
			if((n = parsemac(mac, f[3], sizeof(mac))) != m->maclen)
				error(Ebadarp);
			findlocalip(fs, src, ip);
			break;
		case 5:
			m = ipfindmedium(f[1]);
			if(m == nil || m->maclen == 0)
				error(Ebadarp);
			if(parseip(ip, f[2]) == -1)
				error(Ebadip);
			if((n = parsemac(mac, f[3], sizeof(mac))) != m->maclen)
				error(Ebadarp);
			if(parseip(src, f[4]) == -1)
				error(Ebadip);
			break;
		}
		if(arpenter(fs, V6, ip, mac, n, src, 0) <= 0)
			error("destination unreachable");
	} else if(strcmp(f[0], "del") == 0){
		if (n != 2)
			error(Ebadarg);
		if (parseip(ip, f[1]) == -1)
			error(Ebadip);
		qlock(arp);
		for(a = arp->hash[haship(ip)]; a != nil; a = x){
			x = a->hash;
			if(ipcmp(ip, a->ip) == 0){
				cleanarpent(arp, a);
				memset(a->ip, 0, sizeof(a->ip));
				memset(a->mac, 0, sizeof(a->mac));
			}
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
	uchar ip[IPaddrlen], src[IPaddrlen];
	char mac[2*MAClen+1], *p, *state;
	Ipifc *ifc;
	Arpent *a;
	long n, o;

	p = s;
	o = -offset;
	for(a = arp->cache; len > 0 && a < &arp->cache[NCACHE]; a++){
		if(a->state == 0 || (ifc = a->ifc) == nil || a->ifcid != ifc->ifcid)
			continue;

		qlock(arp);
		state = arpstate[a->state];
		ipmove(ip, a->ip);
		convmac(mac, a->mac, ifc->m->maclen);
		qunlock(arp);

		ipv6local(ifc, src, ip);
		n = snprint(p, len, "%-6.6s %-4.4s %-40.40I %-16.16s %I\n",
			ifc->m->name, state, ip, mac, src);
		if(o < 0) {
			if(n > -o)
				memmove(p, p-o, n+o);
			o += n;
		} else {
			len -= n;
			p += n;
		}
	}

	return p - s;
}

void
ndpsendsol(Fs *f, Ipifc *ifc, Arpent *a)
{
	uchar targ[IPaddrlen], src[IPaddrlen];

	ipmove(targ, a->ip);

	if(a->last != nil){
		ipmove(src, ((Ip6hdr*)a->last->rp)->src);
		arprelease(f->arp, a);

		if(iplocalonifc(ifc, src) != nil || ipproxyifc(f, ifc, src))
			goto send;
	} else {
		arprelease(f->arp, a);
	}

	if(!ipv6local(ifc, src, targ))
		return;
send:
	icmpns(f, src, SRC_UNI, targ, TARG_MULTI, ifc->mac);
}

int
rxmitsols(Arp *arp)
{
	Block *next, *xp;
	Arpent *a, *b, **l;
	Fs *f;
	Ipifc *ifc = nil;
	long nrxt;

	qlock(arp);
	f = arp->f;

	a = arp->rxmt;
	if(a == nil){
		nrxt = 0;
		goto dodrops; 		/* return nrxt; */
	}
	nrxt = a->rtime - NOW;
	if(nrxt > 3*ReTransTimer/4) 
		goto dodrops; 		/* return nrxt; */

	for(; a != nil; a = a->nextrxt){
		ifc = a->ifc;
		if(a->rxtsrem > 0 && ifc != nil && canrlock(ifc)){
			if(a->ifcid == ifc->ifcid)
				break;
			runlock(ifc);
		}
		xp = a->hold;
		a->hold = nil;
		if(xp != nil){
			if(arp->dropf == nil) 
				arp->dropf = xp;
			else
				arp->dropl->list = xp;
			arp->dropl = a->last;
		}
		cleanarpent(arp, a);
	}
	if(a == nil)
		goto dodrops;

	ndpsendsol(f, ifc, a);	/* unlocks arp */

	runlock(ifc);
	qlock(arp);	

	/* put to the end of re-transmit chain */
	l = &arp->rxmt;
	for(b = *l; b != nil; b = b->nextrxt){
		if(b == a){
			*l = a->nextrxt;
			break;
		}
		l = &b->nextrxt;
	}
	for(b = *l; b != nil; b = b->nextrxt)
		l = &b->nextrxt;

	*l = a;
	a->rxtsrem--;
	a->nextrxt = nil;
	a->rtime = NOW + ReTransTimer;

	a = arp->rxmt;
	if(a == nil)
		nrxt = 0;
	else 
		nrxt = a->rtime - NOW;

dodrops:
	xp = arp->dropf;
	arp->dropf = arp->dropl = nil;
	qunlock(arp);

	for(; xp != nil; xp = next){
		next = xp->list;
		icmphostunr6(f, ifc, xp, Icmp6_adr_unreach, 1);
		freeblist(xp);
	}

	return nrxt;

}

static int
rxready(void *v)
{
	Arp *arp = (Arp *) v;

	return arp->rxmt != nil || arp->dropf != nil;
}

static void
rxmitproc(void *v)
{
	Arp *arp = v;
	long wakeupat;

	arp->rxmitp = up;
	if(waserror()){
		arp->rxmitp = nil;
		pexit("hangup", 1);
	}
	for(;;){
		wakeupat = rxmitsols(arp);
		if(wakeupat == 0) 
			sleep(&arp->rxmtq, rxready, v); 
		else if(wakeupat > ReTransTimer/4) 
			tsleep(&arp->rxmtq, return0, 0, wakeupat); 
	}
}

