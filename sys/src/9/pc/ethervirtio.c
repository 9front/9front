#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/netif.h"
#include "etherif.h"

/*
 * virtio ethernet driver
 * http://docs.oasis-open.org/virtio/virtio/v1.0/virtio-v1.0.html
 *
 * TODO
 *
 * implement control queue
 */

typedef struct Vring Vring;
typedef struct Vdesc Vdesc;
typedef struct Vused Vused;
typedef struct Vheader Vheader;
typedef struct Vqueue Vqueue;
typedef struct Ctlr Ctlr;

enum {
	/* §2.1 Device Status Field */
	Sacknowledge = 1,
	Sdriver = 2,
	Sdriverok = 4,
	Sfeatureok = 8,
	Sfailed = 128,

	/* §4.1.4.8 Legacy Interfaces: A Note on PCI Device Layout */
	Qdevfeat = 0,
	Qdrvfeat = 4,
	Qaddr = 8,
	Qsize = 12,
	Qselect = 14,
	Qnotify = 16,
	Qstatus = 18,
	Qisr = 19,
	Qmac = 20,
	Qnetstatus = 26,

	/* flags in Qnetstatus */
	Nlinkup = (1<<0),
	Nannounce = (1<<1),

	/* feature bits */
	Fmac = (1<<5),
	Fstatus = (1<<16),
	Fctrlvq = (1<<17),

	/* vring used flags */
	Unonotify = 1,
	/* vring avail flags */
	Rnointerrupt = 1,

	/* descriptor flags */
	Dnext = 1,
	Dwrite = 2,
	Dindirect = 4,

	/* struct sizes */
	VringSize = 4,
	VdescSize = 16,
	VusedSize = 8,
	VheaderSize = 10,

	/* §4.1.5.1.4.1 says pages are 4096 bytes
	 * for the purposes of the driver.
	 */
	VBY2PG	= 4096,
#define VPGROUND(s)	ROUND(s, VBY2PG)

	Vrxq	= 0,
	Vtxq	= 1,
	Vctlq	= 2,
};

struct Vring
{
	u16int	flags;
	u16int	idx;
};

struct Vdesc
{
	u64int	addr;
	u32int	len;
	u16int	flags;
	u16int	next;
};

struct Vused
{
	u32int	id;
	u32int	len;
};

struct Vheader
{
	u8int	flags;
	u8int	segtype;
	u16int	hlen;
	u16int	seglen;
	u16int	csumstart;
	u16int	csumend;
};

/* §2.4 Virtqueues */
struct Vqueue
{
	Rendez;

	uint	qsize;
	uint	qmask;

	Vdesc	*desc;

	Vring	*avail;
	u16int	*availent;
	u16int	*availevent;

	Vring	*used;
	Vused	*usedent;
	u16int	*usedevent;
	u16int	lastused;

	Vheader *header;
	Block	**block;
};

struct Ctlr {
	Lock;

	int		attached;

	int		port;
	Pcidev*	pcidev;
	Ctlr*	next;
	int		active;
	int		id;
	int		typ;
	ulong	feat;
	int		nqueue;

	/* virtioether has 3 queues: rx, tx and ctl */
	Vqueue	*queue[3];

	/* MAC address */
	uchar	ea[Eaddrlen];
};

static Ctlr *ctlrhead;

static int
vhasroom(void *v)
{
	Vqueue *q = v;
	return q->lastused != q->used->idx;
}

static void
txproc(void *v)
{
	Ether *edev;
	Ctlr *ctlr;
	Vqueue *q;
	Vused *u;
	Block *b;
	int i, j;

	edev = v;
	ctlr = edev->ctlr;
	q = ctlr->queue[Vtxq];

	while(waserror())
		;

	for(i = 0; i < q->qsize/2; i++){
		j = i << 1;
		q->desc[j].addr = PADDR(q->header);
		q->desc[j].len = VheaderSize;
		q->desc[j].next = j | 1;
		q->desc[j].flags = Dnext;

		q->availent[i] = q->availent[i + q->qsize/2] = j;

		j |= 1;
		q->desc[j].next = 0;
		q->desc[j].flags = 0;
	}

	q->used->flags &= ~Rnointerrupt;

	while((b = qbread(edev->oq, 1000000)) != nil){
		i = q->avail->idx & (q->qmask >> 1);
		if(q->block[i] == nil) {
			/* slot free, fill in descriptor */
			q->block[i] = b;
			j = (i << 1) | 1;
			q->desc[j].addr = PADDR(b->rp);
			q->desc[j].len = BLEN(b);
			coherence();
			q->avail->idx++;
			outs(ctlr->port+Qnotify, Vtxq);
		} else {
			/* transmit ring is full */
			freeb(b);
			if(!vhasroom(q))
				sleep(q, vhasroom, q);
		}

		/* free completed packets */
		while((i = q->lastused) != q->used->idx){
			u = &q->usedent[i & q->qmask];
			i = (u->id & q->qmask) >> 1;
			if((b = q->block[i]) == nil)
				break;
			q->block[i] = nil;
			freeb(b);
			q->lastused++;
		}
	}

	pexit("ether out queue closed", 1);
}

static void
rxproc(void *v)
{
	Ether *edev;
	Ctlr *ctlr;
	Vqueue *q;
	Vused *u;
	Block *b;
	int i, j;

	edev = v;
	ctlr = edev->ctlr;
	q = ctlr->queue[Vrxq];

	while(waserror())
		;

	for(i = 0; i < q->qsize/2; i++){
		j = i << 1;
		q->desc[j].addr = PADDR(q->header);
		q->desc[j].len = VheaderSize;
		q->desc[j].next = j | 1;
		q->desc[j].flags = Dwrite|Dnext;

		q->availent[i] = q->availent[i + q->qsize/2] = j;

		j |= 1;
		q->desc[j].next = 0;
		q->desc[j].flags = Dwrite;
	}

	q->used->flags &= ~Rnointerrupt;

	for(;;){
		/* replenish receive ring */
		do {
			i = q->avail->idx & (q->qmask >> 1);
			if(q->block[i] != nil)
				break;
			if((b = iallocb(ETHERMAXTU)) == nil)
				break;
			q->block[i] = b;
			j = (i << 1) | 1;
			q->desc[j].addr = PADDR(b->rp);
			q->desc[j].len = BALLOC(b);
			coherence();
			q->avail->idx++;
			outs(ctlr->port+Qnotify, Vrxq);
		} while(q->avail->idx != q->used->idx);

		/* wait for any packets to complete */
		if(!vhasroom(q))
			sleep(q, vhasroom, q);

		/* retire completed packets */
		while((i = q->lastused) != q->used->idx) {
			u = &q->usedent[i & q->qmask];
			i = (u->id & q->qmask) >> 1;
			if((b = q->block[i]) == nil)
				break;

			q->block[i] = nil;

			b->wp = b->rp + u->len;
			etheriq(edev, b, 1);
			q->lastused++;
		}
	}
}

static void
interrupt(Ureg*, void* arg)
{
	Ether *edev;
	Ctlr* ctlr;
	Vqueue *q;

	edev = arg;
	ctlr = edev->ctlr;

	if(inb(ctlr->port+Qisr) & 1){
		if(vhasroom(q = ctlr->queue[Vtxq]))
			wakeup(q);
		if(vhasroom(q = ctlr->queue[Vrxq]))
			wakeup(q);
	}
}

static void
attach(Ether* edev)
{
	char name[KNAMELEN];
	Ctlr* ctlr;

	ctlr = edev->ctlr;

	lock(ctlr);
	if(!ctlr->attached){
		ctlr->attached = 1;

		/* start kprocs */
		snprint(name, sizeof name, "#l%drx", edev->ctlrno);
		kproc(name, rxproc, edev);
		snprint(name, sizeof name, "#l%dtx", edev->ctlrno);
		kproc(name, txproc, edev);

		/* ready to go */
		outb(ctlr->port+Qstatus, inb(ctlr->port+Qstatus) | Sdriverok);
	}

	unlock(ctlr);
}

static long
ifstat(Ether *edev, void *a, long n, ulong offset)
{
	int i, l;
	char *p;
	Ctlr *ctlr;
	Vqueue *q;

	ctlr = edev->ctlr;

	p = smalloc(READSTR);

	l = snprint(p, READSTR, "devfeat %32.32lub\n", ctlr->feat);
	l += snprint(p+l, READSTR-l, "drvfeat %32.32lub\n", inl(ctlr->port+Qdrvfeat));
	l += snprint(p+l, READSTR-l, "devstatus %8.8ub\n", inb(ctlr->port+Qstatus));
	l += snprint(p+l, READSTR-l, "isr %8.8ub\n",  inb(ctlr->port+Qisr));
	l += snprint(p+l, READSTR-l, "netstatus %8.8ub\n",  inb(ctlr->port+Qnetstatus));

	for(i = 0; i < ctlr->nqueue; i++){
		q = ctlr->queue[i];
		l += snprint(p+l, READSTR-l, "vq%d %#p size %d avail->idx %d used->idx %d lastused %hud\n",
			i, q, q->qsize, q->avail->idx, q->used->idx, q->lastused);
	}

	n = readstr(offset, a, n, p);
	free(p);

	return n;
}

/* XXX: not done */
static long
ctl(Ether *, void *, long)
{
	return 0;
}

/* XXX: not done */
static void
promiscuous(void *v, int on)
{
	Ether *edev;
	Ctlr *ctlr;

	edev = v;
	ctlr = edev->ctlr;

	USED(ctlr, on);
}

/* XXX: not done */
static void
shutdown(Ether* ether)
{
	Ctlr *ctlr;

	ctlr = (Ctlr*) ether;

	outb(ctlr->port+Qstatus, 0);
}

/* XXX: not done */
static void
multicast(void *arg, uchar*, int)
{
	Ether *edev;
	Ctlr *ctlr;

	edev = arg;
	ctlr = edev->ctlr;

	USED(ctlr);
}

/* §2.4.2 Legacy Interfaces: A Note on Virtqueue Layout */
static ulong
queuesize(ulong size)
{
	return VPGROUND(VdescSize*size + sizeof(u16int)*(3+size))
		+ VPGROUND(sizeof(u16int)*3 + VusedSize*size);
}

static Vqueue*
mkqueue(int size)
{
	Vqueue *q;
	uchar *p;

	/* §2.4: Queue Size value is always a power of 2 and <= 32768 */
	assert(!(size & (size - 1)) && size <= 32768);

	q = mallocz(sizeof(Vqueue), 1);
	p = mallocalign(queuesize(size), VBY2PG, 0, 0);
	if(p == nil || q == nil){
		print("ethervirtio: no memory for Vqueue\n");
		free(p);
		free(q);
		return nil;
	}

	q->desc = (void*)p;
	p += VdescSize*size;
	q->avail = (void*)p;
	p += VringSize;
	q->availent = (void*)p;
	p += sizeof(u16int)*size;
	q->availevent = (void*)p;
	p += sizeof(u16int);

	p = (uchar*)VPGROUND((uintptr)p);
	q->used = (void*)p;
	p += VringSize;
	q->usedent = (void*)p;
	p += VusedSize*size;
	q->usedevent = (void*)p;

	q->qsize = size;
	q->qmask = q->qsize - 1;

	q->lastused = q->avail->idx = q->used->idx = 0;

	q->block = mallocz(sizeof(Block*) * size, 1);
	q->header = mallocz(VheaderSize, 1);

	/* disable interrupts
	 * virtio spec says we still get interrupts if
	 * VnotifyEmpty is set in Drvfeat */
	q->used->flags |= Rnointerrupt;

	return q;
}

static Ctlr*
pciprobe(int typ)
{
	Ctlr *c, *h, *t;
	Pcidev *p;
	int n, i;

	h = t = nil;

	/* §4.1.2 PCI Device Discovery */
	for(p = nil; p = pcimatch(p, 0, 0);){
		if(p->vid != 0x1AF4)
			continue;
		/* the two possible DIDs for virtio-net
		if(p->did != 0x1000 && p->did != 0x1041)
			continue;
		/* non-transitional devices will have a revision > 0 */
		if(p->rid != 0)
			continue;
		/* non-transitional device will have typ+0x40 */
		if(pcicfgr16(p, 0x2E) != typ)
			continue;
		if((c = malloc(sizeof(Ctlr))) == nil){
			print("ethervirtio: no memory for Ctlr\n");
			break;
		}

		c->port = p->mem[0].bar & ~0x1;

		if(ioalloc(c->port, p->mem[0].size, 0, "ethervirtio") < 0){
			print("ethervirtio: port %ux in use\n", c->port);
			free(c);
			continue;
		}

		c->typ = typ;
		c->pcidev = p;
		c->id = (p->did<<16)|p->vid;

		/* §3.1.2 Legacy Device Initialization */
		outb(c->port+Qstatus, 0);

		outb(c->port+Qstatus, Sacknowledge|Sdriver);

		c->feat = inl(c->port+Qdevfeat);

		if((c->feat & (Fmac|Fstatus|Fctrlvq)) != (Fmac|Fstatus|Fctrlvq)){
			print("ethervirtio: feature mismatch %32.32lub\n", c->feat);
			outb(c->port+Qstatus, Sfailed);
			iofree(c->port);
			free(c);
			continue;
		}

		outl(c->port+Qdrvfeat, Fmac|Fstatus|Fctrlvq);

		/* part of the 1.0 spec, not used in legacy */
		/*
		outb(vd->port+Status, inb(vd->port+Status) | FeatureOk);
		i = inb(vd->port+Status);
		if(!(i & FeatureOk)){
			print("ethervirtio: feature mismatch %32.32lub\n", vd->feat);
			outb(vd->port+Status, Failed);
			iofree(vd->port);
			free(vd);
			continue;
		}
		*/

		/* §4.1.5.1.4 Virtqueue Configuration */
		for(i=0; i<nelem(c->queue); i++){
			outs(c->port+Qselect, i);
			n = ins(c->port+Qsize);
			if(n == 0 || (n & (n-1)) != 0){
				c->queue[i] = nil;
				break;
			}
			if((c->queue[i] = mkqueue(n)) == nil)
				break;
			coherence();
			outl(c->port+Qaddr, PADDR(c->queue[i]->desc)/VBY2PG);
		}
		c->nqueue = i;
	
		/* read virtio mac */
		for(i = 0; i < Eaddrlen; i++)
			c->ea[i] = inb(c->port+Qmac+i);

		if(h == nil)
			h = c;
		else
			t->next = c;
		t = c;
	}

	return h;
}


static int
reset(Ether* edev)
{
	Ctlr *ctlr;

	if(ctlrhead == nil) {
		ctlrhead = pciprobe(1);
	}

	for(ctlr = ctlrhead; ctlr != nil; ctlr = ctlr->next){
		if(ctlr->active)
			continue;
		if(edev->port == 0 || edev->port == ctlr->port){
			ctlr->active = 1;
			break;
		}
	}

	if(ctlr == nil)
		return -1;

	edev->ctlr = ctlr;
	edev->port = ctlr->port;
	edev->irq = ctlr->pcidev->intl;
	edev->tbdf = ctlr->pcidev->tbdf;
	edev->mbps = 1000;
	edev->link = 1;

	memmove(edev->ea, ctlr->ea, Eaddrlen);

	edev->arg = edev;

	edev->attach = attach;
	edev->shutdown = shutdown;

	edev->interrupt = interrupt;

	edev->ifstat = ifstat;
	edev->ctl = ctl;
	edev->promiscuous = promiscuous;
	edev->multicast = multicast;

	return 0;
}

void
ethervirtiolink(void)
{
	addethercard("ethervirtio", reset);
}

