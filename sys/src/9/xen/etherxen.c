/*
 * Xen virtual network interface frontend
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/netif.h"
#include "../port/etherif.h"

#define LOG(a)

enum {
	Nvif	= 4,
	Ntb		= 16,
	Nrb		= 32,
};

typedef struct Ctlr Ctlr;
typedef union Txframe Txframe;
typedef union Rxframe Rxframe;

struct Ctlr {
	int	attached;
	int	backend;
	int	vifno;
	int	evtchn;
	int rxcopy;
	Txframe	*txframes;
	Txframe	*freetxframe;
	Rxframe	*rxframes;
	netif_tx_front_ring_t txring;
	netif_rx_front_ring_t rxring;
	int	*txrefs;
	int	*rxrefs;
	int	txringref;
	int	rxringref;
	Lock	txlock;
	QLock	attachlock;
	Rendez	wtxframe;
	Rendez	wtxblock;

	ulong interrupts;
	ulong transmits;
	ulong receives;
	ulong txerrors;
	ulong rxerrors;
	ulong rxoverflows;
};

union Txframe {
	struct {
		Txframe *next;
		char data[2];
	} tf;
	uchar page[BY2PG];
};

union Rxframe {
	uchar page[BY2PG];
};

static int nvif;

/*
 * conversions to machine page numbers, pages and addresses
 */
#define MFN(pa)		(patomfn[(pa)>>PGSHIFT])
#define MFNPG(pa)		(MFN(pa)<<PGSHIFT)
#define PA2MA(pa)		(MFNPG(pa) | PGOFF(pa))
#define VA2MA(va)		PA2MA(PADDR(va))

static int
puttxrequest(Ctlr *ctlr, netif_tx_request_t *tr)
{
	netif_tx_request_t *req;
	int i, notify;

	LOG(dprint("puttxrequest id %d ref %d size %d\n", tr->id, tr->gref, tr->size);)
	i = ctlr->txring.req_prod_pvt;
	req = RING_GET_REQUEST(&ctlr->txring, i);
	memmove(req, tr, sizeof(*req));
	ctlr->txring.req_prod_pvt = i+1;
	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&ctlr->txring, notify);
	return notify;
}

static int
putrxrequest(Ctlr *ctlr, netif_rx_request_t *rr)
{
	netif_rx_request_t *req;
	int i;
	int notify;

	LOG(dprint("putrxrequest %d %d\n", rr->id, rr->gref);)
	i = ctlr->rxring.req_prod_pvt;
	req = RING_GET_REQUEST(&ctlr->rxring, i);
	memmove(req, rr, sizeof(*req));
	ctlr->rxring.req_prod_pvt = i+1;
	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&ctlr->rxring, notify);
	return notify;
}

static int
gettxresponse(Ctlr *ctlr, netif_tx_response_t *tr)
{
	int i, avail;
	netif_tx_response_t *rx;

	RING_FINAL_CHECK_FOR_RESPONSES(&ctlr->txring, avail);
	if (!avail)
		return 0;
	i = ctlr->txring.rsp_cons;
	rx = RING_GET_RESPONSE(&ctlr->txring, i);
	LOG(dprint("gettxresponse id %d status %d\n", rx->id, rx->status);)
	if(rx->status)
		ctlr->txerrors++;
	*tr = *rx;
	ctlr->txring.rsp_cons = ++i;
	return 1;
}

static int
getrxresponse(Ctlr *ctlr, netif_rx_response_t* rr)
{
	int i, avail;
	netif_rx_response_t *rx;

	RING_FINAL_CHECK_FOR_RESPONSES(&ctlr->rxring, avail);
	if (!avail)
		return 0;
	i = ctlr->rxring.rsp_cons;
	rx = RING_GET_RESPONSE(&ctlr->rxring, i);
	LOG(dprint("getrxresponse id %d offset %d flags %ux status %d\n", rx->id, rx->offset, rx->flags, rx->status);)
	*rr = *rx;
	ctlr->rxring.rsp_cons = ++i;
	return 1;
}

static int
ringinit(Ctlr *ctlr, char *a)
{
	netif_tx_sring_t *txr;
	netif_rx_sring_t *rxr;

	txr = (netif_tx_sring_t*)a;
	memset(txr, 0, BY2PG);
	SHARED_RING_INIT(txr);
	FRONT_RING_INIT(&ctlr->txring, txr, BY2PG);
	ctlr->txringref = shareframe(ctlr->backend, txr, 1);

	rxr = (netif_rx_sring_t*)(a+BY2PG);
	SHARED_RING_INIT(rxr);
	FRONT_RING_INIT(&ctlr->rxring, rxr, BY2PG);
	ctlr->rxringref = shareframe(ctlr->backend, rxr, 1);

	return 2*BY2PG;
}

static int
vifsend(Ctlr *ctlr, Block *bp)
{
	netif_tx_request_t tr;
	Txframe *tx;
	int id;

	ilock(&ctlr->txlock);
	tx = ctlr->freetxframe;
	ctlr->freetxframe = tx->tf.next;
	iunlock(&ctlr->txlock);
	id = tx - ctlr->txframes;
	tr.gref = ctlr->txrefs[id];
	tr.offset = tx->tf.data - (char*)tx;
	tr.flags = 0;	// XXX checksum?
	tr.id = id;
	tr.size = BLEN(bp);
	memmove(tx->tf.data, bp->rp, tr.size);
	return puttxrequest(ctlr, &tr);
}

static int
vifsenddone(Ctlr *ctlr, netif_tx_response_t *tr)
{
	Txframe *tx;

	tx = &ctlr->txframes[tr->id];	// XXX check validity of id
	ilock(&ctlr->txlock);
	tx->tf.next = ctlr->freetxframe;
	ctlr->freetxframe = tx;
	iunlock(&ctlr->txlock);
	return 1;
}

static int
vifrecv(Ctlr *ctlr, Rxframe *rx)
{
	netif_rx_request_t rr;
	int id;
	int ref;

	id = rx - ctlr->rxframes;
	if (ctlr->rxcopy)
		ref = ctlr->rxrefs[id];
	else {
		ref = donateframe(ctlr->backend, rx);
		ctlr->rxrefs[id] = ref;
	}
	rr.id = id;
	rr.gref = ref;
	return putrxrequest(ctlr, &rr);
}

static int
vifrecvdone(Ether *ether, netif_rx_response_t *rr)
{
	Ctlr *ctlr;
	Rxframe *rx;
	Block *bp;
	int len;

	ctlr = ether->ctlr;
	rx = &ctlr->rxframes[rr->id];	// XXX check validity of id
	if (!ctlr->rxcopy)
		acceptframe(ctlr->rxrefs[rr->id], rx);
	if ((len = rr->status) <= 0) {
		ctlr->rxerrors++;
		vifrecv(ctlr, rx);
		return 1;
	}
	if(len > sizeof(Etherpkt) || (bp = iallocb(sizeof(Etherpkt))) == nil) {
		ctlr->rxoverflows++;
		vifrecv(ctlr, rx);
		return 1;
	}

	ctlr->receives++;
	memmove(bp->base, rx->page + rr->offset, len);
	vifrecv(ctlr, rx);

	bp->rp = bp->base;
	bp->wp = bp->rp + len;
	bp->free = 0;
	bp->next = 0;
	bp->list = 0;
	if (rr->flags & NETRXF_data_validated)
		bp->flag |= Btcpck|Budpck;
	etheriq(ether, bp);
	return 0;
}

static int
wtxframe(void *a)
{
	return ((struct Ctlr*)a)->freetxframe != 0;
}

static int
wtxblock(void *a)
{
	return qcanread(((struct Ether*)a)->oq);
}

static void
etherxenproc(void *a)
{
	Ether *ether = a;
	Ctlr *ctlr = ether->ctlr;
	Block *bp;
	int notify;

	for (;;) {
		while (ctlr->freetxframe == 0)
			sleep(&ctlr->wtxframe, wtxframe, ctlr);
		while ((bp = qget(ether->oq)) == 0)
			sleep(&ctlr->wtxblock, wtxblock, ether);
		notify = vifsend(ctlr, bp);
		freeb(bp);
		if (notify)
			xenchannotify(ctlr->evtchn);
	}
}

static void
etherxentransmit(Ether *ether)
{
	Ctlr *ctlr;
	
	ctlr = ether->ctlr;
	ctlr->transmits++;
	wakeup(&ctlr->wtxblock);
}

static void
etherxenintr(Ureg*, void *a)
{
	Ether *ether = a;
	Ctlr *ctlr = ether->ctlr;
	int txnotify;
	netif_tx_response_t tr;
	netif_rx_response_t rr;

	ctlr->interrupts++;
	txnotify = 0;
	while (getrxresponse(ctlr, &rr))
		vifrecvdone(ether, &rr);
	while (gettxresponse(ctlr, &tr)) {
		if (vifsenddone(ctlr, &tr))
			txnotify = 1;
	}
	if (txnotify)
		wakeup(&ctlr->wtxframe);
}

static long
etherxenctl(Ether *ether, void *buf, long n)
{
	uchar ea[Eaddrlen];
	Cmdbuf *cb;

	cb = parsecmd(buf, n);
	if(cb->nf >= 2
	&& strcmp(cb->f[0], "ea")==0
	&& parseether(ea, cb->f[1]) == 0){
		free(cb);
		memmove(ether->ea, ea, Eaddrlen);
		memmove(ether->addr, ether->ea, Eaddrlen);
		return 0;
	}
	free(cb);
	error(Ebadctl);
	return -1;	/* not reached */
}

static void
backendconnect(Ctlr *ctlr)
{
	char dir[64];
	char buf[64];

	sprint(dir, "device/vif/%d/", ctlr->vifno);
	xenstore_setd(dir, "state", XenbusStateInitialising);
	xenstore_setd(dir, "tx-ring-ref", ctlr->txringref);
	xenstore_setd(dir, "rx-ring-ref", ctlr->rxringref);
	xenstore_setd(dir, "event-channel", ctlr->evtchn);
	print("etherxen: request-rx-copy=%d\n", ctlr->rxcopy);
	if (ctlr->rxcopy)
		xenstore_setd(dir, "request-rx-copy", 1);
	xenstore_setd(dir, "state", XenbusStateConnected);
	xenstore_gets(dir, "backend", buf, sizeof buf);
	sprint(dir, "%s/", buf);
	HYPERVISOR_yield();
	xenstore_gets(dir, "state", buf, sizeof buf);
	while (strtol(buf, 0, 0) != XenbusStateConnected) {
		print("etherxen: waiting for vif %d to connect\n", ctlr->vifno);
		tsleep(&up->sleep, return0, 0, 50);
		xenstore_gets(dir, "state", buf, sizeof buf);
	}
}

static void
etherxenattach(Ether *ether)
{
	Ctlr *ctlr;
	char *p;
	Txframe *tx;
	int npage, i;

	LOG(dprint("etherxenattach\n");)
	ctlr = ether->ctlr;
	qlock(&ctlr->attachlock);
	if (ctlr->attached) {
		qunlock(&ctlr->attachlock);
		return;
	}

	npage = 2 + Ntb + Nrb;
	p = (char*)xspanalloc(npage<<PGSHIFT, BY2PG, 0);
	p += ringinit(ctlr, p);
	ctlr->txrefs = malloc(Ntb*sizeof(int));
	ctlr->rxrefs = malloc(Nrb*sizeof(int));
	ctlr->txframes = (Txframe*)p;
	for (i = 0; i < Ntb; i++, p += BY2PG) {
		tx = (Txframe*)p;
		if (i != Ntb-1)
			tx->tf.next = tx + 1;
		else
			tx->tf.next = 0;
		ctlr->txrefs[i] = shareframe(ctlr->backend, tx, 0);
	}
	ctlr->freetxframe = ctlr->txframes;
	ctlr->rxframes = (Rxframe*)p;
	for (i = 0; i < Nrb; i++, p += BY2PG) {
		if (ctlr->rxcopy)
			ctlr->rxrefs[i] = shareframe(ctlr->backend, (Rxframe*)p, 1);
		vifrecv(ctlr, (Rxframe*)p);
	}
	
	ctlr->evtchn = xenchanalloc(ctlr->backend);
	intrenable(ctlr->evtchn, etherxenintr, ether, BUSUNKNOWN, "vif");

	kproc("vif", etherxenproc, ether);
	backendconnect(ctlr);
	ctlr->attached = 1;
	qunlock(&ctlr->attachlock);
}

static void
etherxenmulticast(void* arg, uchar* addr, int on)
{
	USED(arg, addr, on);
}

static long
ifstat(Ether* ether, void* a, long n, ulong offset)
{
	Ctlr *ctlr;
	char *buf, *p;
	int l, len;

	ctlr = ether->ctlr;
	if(n == 0)
		return 0;
	if((p = malloc(READSTR)) == nil)
		error(Enomem);
	l = snprint(p, READSTR, "intr: %lud\n", ctlr->interrupts);
	l += snprint(p+l, READSTR-l, "transmits: %lud\n", ctlr->transmits);
	l += snprint(p+l, READSTR-l, "receives: %lud\n", ctlr->receives);
	l += snprint(p+l, READSTR-l, "txerrors: %lud\n", ctlr->txerrors);
	l += snprint(p+l, READSTR-l, "rxerrors: %lud\n", ctlr->rxerrors);
	snprint(p+l, READSTR-l, "rxoverflows: %lud\n", ctlr->rxoverflows);

	buf = a;
	len = readstr(offset, buf, n, p);
	free(p);

	return len;
}

static int
pnp(Ether* ether)
{
	uchar ea[Eaddrlen];
	char dir[64];
	char buf[64];
	Ctlr *ctlr;
	int domid, rxcopy;

	if (nvif > Nvif)
		return -1;
	sprint(dir, "device/vif/%d/", nvif);
	if (xenstore_gets(dir, "backend-id", buf, sizeof buf) <= 0)
		return -1;
	domid = strtol(buf, 0, 0);
	if (xenstore_gets(dir, "mac", buf, sizeof buf) <= 0)
		return -1;
	if (parseether(ea, buf) < 0)
		return -1;
	if (xenstore_gets(dir, "backend", buf, sizeof buf) <= 0)
		return 1;
	sprint(dir, "%s/", buf);
	rxcopy = 0;
	if (xenstore_gets(dir, "feature-rx-copy", buf, sizeof buf) >= 0)
		rxcopy = strtol(buf, 0, 0);
	ether->ctlr = ctlr = malloc(sizeof(Ctlr));
	memset(ctlr, 0, sizeof(Ctlr));
	ctlr->backend = domid;
	ctlr->vifno = nvif++;
	ctlr->rxcopy = rxcopy;

	memmove(ether->ea, ea, sizeof ether->ea);
	ether->mbps = 100;	// XXX what speed?
	ether->attach = etherxenattach;
	ether->transmit = etherxentransmit;
	ether->irq = -1;
	ether->tbdf = BUSUNKNOWN;
	ether->ifstat = ifstat;
	ether->ctl = etherxenctl;
	ether->promiscuous = nil;
	ether->multicast = etherxenmulticast;
	ether->arg = ether;

	intrenable(ether->irq, etherxenintr, ether, ether->tbdf, ether->name);

	return 0;
}

void
etherxenlink(void)
{
	addethercard("xen", pnp);
}
