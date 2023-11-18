#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include "../port/netif.h"
#include "ip.h"
#include "ipv6.h"

typedef struct Etherhdr Etherhdr;
struct Etherhdr
{
	uchar	d[6];
	uchar	s[6];
	uchar	t[2];
};

static void	etherread4(void *a);
static void	etherread6(void *a);
static void	etherbind(Ipifc *ifc, int argc, char **argv);
static void	etherunbind(Ipifc *ifc);
static void	etherbwrite(Ipifc *ifc, Block *bp, int version, uchar *ip, Routehint *rh);
static void	etheraddmulti(Ipifc *ifc, uchar *a, uchar *ia);
static void	etherdelmulti(Ipifc *ifc, uchar *a, uchar *ia);
static void	etherareg(Fs *f, Ipifc *ifc, Iplifc *lifc, uchar *ip);
static Block*	multicastarp(Fs *f, Arpent *a, uchar *mac, Routehint *rh);
static Block*	newEARP(void);
static void	sendarpreq(Fs *f, Arpent *a);
static int	multicastea(uchar *ea, uchar *ip);
static void	recvarpproc(void*);
static void	etherpref2addr(uchar *pref, uchar *ea);

Medium ethermedium =
{
.name=		"ether",
.hsize=		ETHERHDRSIZE,
.mintu=		ETHERMINTU,
.maxtu=		ETHERHDRSIZE+1500,
.maclen=	6,
.bind=		etherbind,
.unbind=	etherunbind,
.bwrite=	etherbwrite,
.addmulti=	etheraddmulti,
.delmulti=	etherdelmulti,
.areg=		etherareg,
.pref2addr=	etherpref2addr,
};

Medium gbemedium =
{
.name=		"gbe",
.hsize=		ETHERHDRSIZE,
.mintu=		ETHERMINTU,
.maxtu=		ETHERHDRSIZE+9000,
.maclen=	6,
.bind=		etherbind,
.unbind=	etherunbind,
.bwrite=	etherbwrite,
.addmulti=	etheraddmulti,
.delmulti=	etherdelmulti,
.areg=		etherareg,
.pref2addr=	etherpref2addr,
};

typedef struct	Etherrock Etherrock;
struct Etherrock
{
	Fs	*f;		/* file system we belong to */
	Proc	*arpp;		/* arp process */
	Proc	*read4p;	/* reading process (v4)*/
	Proc	*read6p;	/* reading process (v6)*/
	Chan	*mchan4;	/* Data channel for v4 */
	Chan	*achan;		/* Arp channel */
	Chan	*cchan4;	/* Control channel for v4 */
	Chan	*mchan6;	/* Data channel for v6 */
	Chan	*cchan6;	/* Control channel for v6 */
};

/*
 *  ethernet arp request
 */
enum
{
	ARPREQUEST	= 1,
	ARPREPLY	= 2,
};

typedef struct Etherarp Etherarp;
struct Etherarp
{
	uchar	d[6];
	uchar	s[6];
	uchar	type[2];
	uchar	hrd[2];
	uchar	pro[2];
	uchar	hln;
	uchar	pln;
	uchar	op[2];
	uchar	sha[6];
	uchar	spa[4];
	uchar	tha[6];
	uchar	tpa[4];
};

static char *nbmsg = "nonblocking";

/*
 *  called to bind an IP ifc to an ethernet device
 */
static void
etherbind(Ipifc *ifc, int argc, char **argv)
{
	char addr[Maxpath], dir[Maxpath];
	Etherrock *er;
	Chan *c;
	int n;

	if(argc < 2)
		error(Ebadarg);

	/*
	 *  get mac address
	 */
	snprint(addr, sizeof(addr), "%s/addr", argv[2]);
	c = namec(addr, Aopen, OREAD, 0);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	n = devtab[c->type]->read(c, addr, sizeof(addr)-1, 0);
	if(n < 0)
		error(Eio);
	addr[n] = 0;
	if(parsemac(ifc->mac, addr, sizeof(ifc->mac)) != 6)
		error("could not find mac address");
	cclose(c);
	poperror();

	er = smalloc(sizeof(*er));
	er->read4p = er->read6p = er->arpp = (void*)-1;
	er->mchan4 = er->cchan4 = er->mchan6 = er->cchan6 = er->achan = nil;
	er->f = ifc->conv->p->f;

	if(waserror()){
		if(er->mchan4 != nil)
			cclose(er->mchan4);
		if(er->cchan4 != nil)
			cclose(er->cchan4);
		if(er->mchan6 != nil)
			cclose(er->mchan6);
		if(er->cchan6 != nil)
			cclose(er->cchan6);
		if(er->achan != nil)
			cclose(er->achan);
		free(er);
		nexterror();
	}

	/*
	 *  open ipv4 conversation
	 *
	 *  the dial will fail if the type is already open on
	 *  this device.
	 */
	snprint(addr, sizeof(addr), "%s!0x800", argv[2]);	/* ETIP4 */
	er->mchan4 = chandial(addr, nil, dir, &er->cchan4);

	/*
	 *  make it non-blocking
	 */
	devtab[er->cchan4->type]->write(er->cchan4, nbmsg, strlen(nbmsg), 0);

	/*
	 *  open ipv6 conversation
	 *
	 *  the dial will fail if the type is already open on
	 *  this device.
	 */
	snprint(addr, sizeof(addr), "%s!0x86DD", argv[2]);	/* ETIP6 */
	er->mchan6 = chandial(addr, nil, dir, &er->cchan6);

	/*
	 *  make it non-blocking
	 */
	devtab[er->cchan6->type]->write(er->cchan6, nbmsg, strlen(nbmsg), 0);

	/*
 	 *  open arp conversation
	 */
	snprint(addr, sizeof(addr), "%s!0x806", argv[2]);	/* ETARP */
	er->achan = chandial(addr, nil, nil, nil);
	poperror();

	ifc->arg = er;

	kproc("etherread4", etherread4, ifc);
	kproc("etherread6", etherread6, ifc);
	kproc("recvarpproc", recvarpproc, ifc);
}

/*
 *  called from devip due to
 *  manual unbind or from one of the reader procs
 *  due to read error from the ethernet device.
 *  this is called only once!
 */
static void
etherunbind(Ipifc *ifc)
{
	Etherrock *er = ifc->arg;

	while(waserror())
		;
	/* wait for readers to start */
	while(er->arpp == (void*)-1 || er->read4p == (void*)-1 || er->read6p == (void*)-1)
		tsleep(&up->sleep, return0, 0, 300);

	if(er->read4p != nil && er->read4p != up)
		postnote(er->read4p, 1, "unbind", 0);
	if(er->read6p != nil && er->read6p != up)
		postnote(er->read6p, 1, "unbind", 0);
	if(er->arpp != nil && er->arpp != up)
		postnote(er->arpp, 1, "unbind", 0);
	poperror();

	while(waserror())
		;
	/* wait for readers to die */
	while(er->arpp != nil && er->arpp != up
	|| er->read4p != nil && er->read4p != up
	|| er->read6p != nil && er->read6p != up)
		tsleep(&up->sleep, return0, 0, 300);
	poperror();

	if(er->mchan4 != nil)
		cclose(er->mchan4);
	if(er->cchan4 != nil)
		cclose(er->cchan4);
	if(er->mchan6 != nil)
		cclose(er->mchan6);
	if(er->cchan6 != nil)
		cclose(er->cchan6);
	if(er->achan != nil)
		cclose(er->achan);

	free(er);
}

/*
 *  called by ipoput with a single block to write with ifc rlock'd
 */
static void
etherbwrite(Ipifc *ifc, Block *bp, int version, uchar *ip, Routehint *rh)
{
	Etherhdr *eh;
	Arpent *a;
	uchar mac[6];
	Etherrock *er = ifc->arg;

	/* get mac address of destination */
	a = arpget(er->f->arp, bp, version, ifc, ip, mac, rh);
	if(a != nil){
		/* check for broadcast or multicast */
		bp = multicastarp(er->f, a, mac, rh);
		if(bp == nil){
			/* don't do anything if it's been less than a second since the last */
			if(a->utime - a->ctime < RETRANS_TIMER){
				arprelease(er->f->arp, a);	/* unlocks arp */
				return;
			}
			switch(version){
			case V4:
				sendarpreq(er->f, a);	/* unlocks arp */
				break;
			case V6:
				ndpsendsol(er->f, a);	/* unlocks arp */
				break;
			default:
				panic("etherbwrite: version %d", version);
			}
			return;
		}
	}

	/* need to have single block */
	assert(bp->list == nil);

	/* make it a single block with space for the ether header */
	bp = padblock(bp, ETHERHDRSIZE);
	if(BLEN(bp) < ETHERMINTU)
		bp = adjustblock(bp, ETHERMINTU);
	eh = (Etherhdr*)bp->rp;

	/* copy in mac addresses and ether type */
	memmove(eh->s, ifc->mac, sizeof(eh->s));
	memmove(eh->d, mac, sizeof(eh->d));

 	switch(version){
	case V4:
		eh->t[0] = 0x08;
		eh->t[1] = 0x00;
		devtab[er->mchan4->type]->bwrite(er->mchan4, bp, 0);
		break;
	case V6:
		eh->t[0] = 0x86;
		eh->t[1] = 0xDD;
		devtab[er->mchan6->type]->bwrite(er->mchan6, bp, 0);
		break;
	default:
		panic("etherbwrite2: version %d", version);
	}
	ifc->out++;
}


/*
 *  process to read from the ethernet
 */
static void
etherread4(void *a)
{
	Ipifc *ifc;
	Block *bp;
	Etherrock *er;

	ifc = a;
	er = ifc->arg;
	er->read4p = up;	/* hide identity under a rock for unbind */
	if(!waserror())
	for(;;){
		bp = devtab[er->mchan4->type]->bread(er->mchan4, ifc->maxtu, 0);
		if(bp == nil){
			poperror();
			break;
		}
		rlock(ifc);
		if(waserror()){
			runlock(ifc);
			nexterror();
		}
		ifc->in++;
		if(ifc->lifc == nil || BLEN(bp) <= ETHERHDRSIZE)
			freeb(bp);
		else {
			bp->rp += ETHERHDRSIZE;
			ipiput4(er->f, ifc, bp);
		}
		runlock(ifc);
		poperror();
	}
	if(mediumunbindifc(ifc) != nil)
		er->read4p = nil;	/* someone else is doing the unbind */
	pexit("hangup", 1);
}


/*
 *  process to read from the ethernet, IPv6
 */
static void
etherread6(void *a)
{
	Ipifc *ifc;
	Block *bp;
	Etherrock *er;

	ifc = a;
	er = ifc->arg;
	er->read6p = up;	/* hide identity under a rock for unbind */
	if(!waserror())
	for(;;){
		bp = devtab[er->mchan6->type]->bread(er->mchan6, ifc->maxtu, 0);
		if(bp == nil){
			poperror();
			break;
		}
		rlock(ifc);
		if(waserror()){
			runlock(ifc);
			nexterror();
		}
		ifc->in++;
		if(ifc->lifc == nil || BLEN(bp) <= ETHERHDRSIZE)
			freeb(bp);
		else {
			bp->rp += ETHERHDRSIZE;
			ipiput6(er->f, ifc, bp);
		}
		runlock(ifc);
		poperror();
	}
	if(mediumunbindifc(ifc) != nil)
		er->read6p = nil;	/* someone else is doing the unbind */
	pexit("hangup", 1);
}

static void
etheraddmulti(Ipifc *ifc, uchar *a, uchar *)
{
	uchar mac[6];
	char buf[64];
	Etherrock *er = ifc->arg;
	int version;

	version = multicastea(mac, a);
	sprint(buf, "addmulti %E", mac);
	switch(version){
	case V4:
		devtab[er->cchan4->type]->write(er->cchan4, buf, strlen(buf), 0);
		break;
	case V6:
		devtab[er->cchan6->type]->write(er->cchan6, buf, strlen(buf), 0);
		break;
	default:
		panic("etheraddmulti: version %d", version);
	}
}

static void
etherdelmulti(Ipifc *ifc, uchar *a, uchar *)
{
	uchar mac[6];
	char buf[64];
	Etherrock *er = ifc->arg;
	int version;

	version = multicastea(mac, a);
	sprint(buf, "remmulti %E", mac);
	switch(version){
	case V4:
		devtab[er->cchan4->type]->write(er->cchan4, buf, strlen(buf), 0);
		break;
	case V6:
		devtab[er->cchan6->type]->write(er->cchan6, buf, strlen(buf), 0);
		break;
	default:
		panic("etherdelmulti: version %d", version);
	}
}

static Block*
newEARP(void)
{
	Block *bp;

	bp = allocb(ETHERMINTU);
	bp->wp += ETHERMINTU;
	memset(bp->rp, 0, ETHERMINTU);
	return bp;
}

/*
 *  send an ethernet arp
 *  (only v4, v6 uses the neighbor discovery, rfc1970)
 */
static void
sendarpreq(Fs *f, Arpent *a)
{
	Block *bp;
	Etherarp *e;
	Ipifc *ifc = a->ifc;
	Etherrock *er = ifc->arg;
	uchar targ[IPv4addrlen], src[IPv4addrlen];

	memmove(targ, a->ip+IPv4off, IPv4addrlen);
	arpcontinue(f->arp, a);

	if(!ipv4local(ifc, src, 0, targ))
		return;

	bp = newEARP();
	e = (Etherarp*)bp->rp;
	memmove(e->tpa, targ, sizeof(e->tpa));
	memmove(e->spa, src, sizeof(e->spa));
	memmove(e->sha, ifc->mac, sizeof(e->sha));
	memset(e->d, 0xff, sizeof(e->d));		/* ethernet broadcast */
	memmove(e->s, ifc->mac, sizeof(e->s));

	hnputs(e->type, ETARP);
	hnputs(e->hrd, 1);
	hnputs(e->pro, ETIP4);
	e->hln = sizeof(e->sha);
	e->pln = sizeof(e->spa);
	hnputs(e->op, ARPREQUEST);

	devtab[er->achan->type]->bwrite(er->achan, bp, 0);
}

/*
 *  send a gratuitous arp to refresh arp caches
 */
static void
sendgarp(Ipifc *ifc, uchar *ip)
{
	Block *bp;
	Etherarp *e;
	Etherrock *er = ifc->arg;

	bp = newEARP();
	e = (Etherarp*)bp->rp;
	memmove(e->tpa, ip+IPv4off, sizeof(e->tpa));
	memmove(e->spa, ip+IPv4off, sizeof(e->spa));
	memmove(e->sha, ifc->mac, sizeof(e->sha));
	memset(e->d, 0xff, sizeof(e->d));		/* ethernet broadcast */
	memmove(e->s, ifc->mac, sizeof(e->s));

	hnputs(e->type, ETARP);
	hnputs(e->hrd, 1);
	hnputs(e->pro, ETIP4);
	e->hln = sizeof(e->sha);
	e->pln = sizeof(e->spa);
	hnputs(e->op, ARPREQUEST);

	devtab[er->achan->type]->bwrite(er->achan, bp, 0);
}

static void
recvarp(Ipifc *ifc)
{
	int forme;
	Block *ebp, *rbp;
	Etherarp *e, *r;
	static uchar eprinted[4];
	Etherrock *er = ifc->arg;

	ebp = devtab[er->achan->type]->bread(er->achan, ifc->maxtu, 0);
	if(ebp == nil)
		error(Ehungup);

	rlock(ifc);

	e = (Etherarp*)ebp->rp;
	switch(nhgets(e->op)) {
	default:
		break;

	case ARPREPLY:
		/* make sure not to enter multi/broadcat address */
		if(e->sha[0] & 1)
			break;

		/* check for machine using my ip address */
		if(arpforme(er->f, V4, e->spa, e->tpa, ifc)){
			if(memcmp(e->sha, ifc->mac, sizeof(e->sha)) != 0){
				print("arprep: 0x%E/0x%E also has ip addr %V\n",
					e->s, e->sha, e->spa);
				break;
			}
		}

		/* refresh what we know about sender */
		arpenter(er->f, V4, e->spa, e->sha, sizeof(e->sha), e->tpa, ifc, 1);
		break;

	case ARPREQUEST:
		/* don't reply to multi/broadcat addresses */
		if(e->sha[0] & 1)
			break;

		/* don't answer arps till we know who we are */
		if(ifc->lifc == nil)
			break;

		/* check for machine using my ip or ether address */
		if(arpforme(er->f, V4, e->spa, e->tpa, ifc)){
			if(memcmp(e->sha, ifc->mac, sizeof(e->sha)) != 0){
				if(memcmp(eprinted, e->spa, sizeof(e->spa)) != 0){
					/* print only once */
					print("arpreq: 0x%E also has ip addr %V\n",
						e->sha, e->spa);
					memmove(eprinted, e->spa, sizeof(e->spa));
				}
				break;
			}
		} else {
			if(memcmp(e->sha, ifc->mac, sizeof(e->sha)) == 0){
				print("arpreq: %V also has ether addr %E\n",
					e->spa, e->sha);
				break;
			}
		}

		/*
		 * when request is for our address or systems we're proxying for,
		 * enter senders address into arp table and reply, otherwise just
		 * refresh the senders address.
		 */
		forme = arpforme(er->f, V4, e->tpa, e->spa, ifc);
		if(arpenter(er->f, V4, e->spa, e->sha, sizeof(e->sha), e->tpa, ifc, !forme) < 0 || !forme)
			break;

		rbp = newEARP();
		r = (Etherarp*)rbp->rp;
		hnputs(r->type, ETARP);
		hnputs(r->hrd, 1);
		hnputs(r->pro, ETIP4);
		r->hln = sizeof(r->sha);
		r->pln = sizeof(r->spa);
		hnputs(r->op, ARPREPLY);
		memmove(r->tha, e->sha, sizeof(r->tha));
		memmove(r->tpa, e->spa, sizeof(r->tpa));
		memmove(r->sha, ifc->mac, sizeof(r->sha));
		memmove(r->spa, e->tpa, sizeof(r->spa));
		memmove(r->d, e->sha, sizeof(r->d));
		memmove(r->s, ifc->mac, sizeof(r->s));

		runlock(ifc);
		freeb(ebp);

		devtab[er->achan->type]->bwrite(er->achan, rbp, 0);
		return;
	}

	runlock(ifc);
	freeb(ebp);
}

static void
recvarpproc(void *v)
{
	Ipifc *ifc = v;
	Etherrock *er = ifc->arg;

	er->arpp = up;
	if(waserror()){
		if(mediumunbindifc(ifc) != nil)
			er->arpp = nil;	/* someone else is doing the unbind */
		pexit("hangup", 1);
	}
	for(;;)
		recvarp(ifc);
}

static int
multicastea(uchar *ea, uchar *ip)
{
	int x;

	switch(x = ipismulticast(ip)){
	case V4:
		ea[0] = 0x01;
		ea[1] = 0x00;
		ea[2] = 0x5e;
		ea[3] = ip[13] & 0x7f;
		ea[4] = ip[14];
		ea[5] = ip[15];
		break;
 	case V6:
 		ea[0] = 0x33;
 		ea[1] = 0x33;
 		ea[2] = ip[12];
		ea[3] = ip[13];
 		ea[4] = ip[14];
 		ea[5] = ip[15];
 		break;
	}
	return x;
}

/*
 *  fill in an arp entry for broadcast or multicast
 *  addresses.  Return the first queued packet for the
 *  IP address.
 */
static Block*
multicastarp(Fs *f, Arpent *a, uchar *mac, Routehint *rh)
{
	/* is it broadcast? */
	if(ipforme(f, a->ip) == Rbcast){
		memset(mac, 0xff, a->ifc->m->maclen);
		return arpresolve(f->arp, a, mac, rh);
	}

	/* if multicast, fill in mac */
	switch(multicastea(mac, a->ip)){
	case V4:
	case V6:
		return arpresolve(f->arp, a, mac, rh);
	}

	/* let arp take care of it */
	return nil;
}

void
ethermediumlink(void)
{
	addipmedium(&ethermedium);
	addipmedium(&gbemedium);
}


static void
etherpref2addr(uchar *pref, uchar *ea)
{
	pref[8] = ea[0] ^ 0x2;
	pref[9] = ea[1];
	pref[10] = ea[2];
	pref[11] = 0xFF;
	pref[12] = 0xFE;
	pref[13] = ea[3];
	pref[14] = ea[4];
	pref[15] = ea[5];
}

static void
etherareg(Fs *f, Ipifc *ifc, Iplifc *lifc, uchar *ip)
{
	static char tdad[] = "dad6";
	uchar a[IPaddrlen];

	if(ipcmp(ip, IPnoaddr) == 0 || ipcmp(ip, v4prefix) == 0)
		return;

	if(isv4(ip)){
		sendgarp(ifc, ip);
		return;
	}

	if((lifc->type&Rv4) != 0)
		return;

	if(!lifc->tentative){
		icmpna6(f, lifc->local, v6allnodesL, ip, ifc->mac, 6, 1<<5);
		return;
	}

	if(ipcmp(lifc->local, ip) != 0)
		return;

	/* temporarily add route for duplicate address detection */
	ipv62smcast(a, ip);
	addroute(f, a, IPallbits, v6Unspecified, IPallbits, ip, Rmulti, ifc, tdad);
	if(waserror()){
		delroute(f, a, IPallbits, v6Unspecified, IPallbits, ip, Rmulti, ifc, tdad);
		nexterror();
	}
	icmpns6(f, 0, SRC_UNSPEC, ip, TARG_MULTI, ifc->mac, 6);
	poperror();
	delroute(f, a, IPallbits, v6Unspecified, IPallbits, ip, Rmulti, ifc, tdad);
}
