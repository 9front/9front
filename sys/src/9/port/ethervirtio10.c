/*
 * virtio 1.0 ethernet driver
 * http://docs.oasis-open.org/virtio/virtio/v1.0/virtio-v1.0.html
 *
 * In contrast to ethervirtio.c, this driver handles the non-legacy
 * interface for virtio ethernet which uses mmio for all register accesses
 * and requires a laborate pci capability structure dance to get working.
 *
 * It is kind of pointless as it is most likely slower than
 * port i/o (harder to emulate on the pc platform).
 * 
 * The reason why this driver is needed it is that vultr set the
 * disable-legacy=on option in the -device parameter for qemu
 * on their hypervisor.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "../port/error.h"
#include "../port/netif.h"
#include "../port/etherif.h"

typedef struct Vconfig Vconfig;
typedef struct Vnetcfg Vnetcfg;

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
	Sfeaturesok = 8,
	Sfailed = 128,

	/* flags in Qnetstatus */
	Nlinkup = (1<<0),
	Nannounce = (1<<1),

	/* feat[0] bits */
	Fmac = 1<<5,
	Fstatus = 1<<16,
	Fctrlvq = 1<<17,
	Fctrlrx = 1<<18,

	/* feat[1] bits */
	Fversion1 = 1<<(32-32),

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
	VheaderSize = 12,

	Vrxq	= 0,
	Vtxq	= 1,
	Vctlq	= 2,

	/* class/cmd for Vctlq */
	CtrlRx	= 0x00,
		CmdPromisc	= 0x00,
		CmdAllmulti	= 0x01,
	CtrlMac	= 0x01,
		CmdMacTableSet	= 0x00,
	CtrlVlan= 0x02,
		CmdVlanAdd	= 0x00,
		CmdVlanDel	= 0x01,
};

struct Vconfig {
	u32int	devfeatsel;
	u32int	devfeat;
	u32int	drvfeatsel;
	u32int	drvfeat;

	u16int	msixcfg;
	u16int	nqueues;

	u8int	status;
	u8int	cfggen;
	u16int	queuesel;

	u16int	queuesize;
	u16int	queuemsixvect;

	u16int	queueenable;
	u16int	queuenotifyoff;

	u64int	queuedesc;
	u64int	queueavail;
	u64int	queueused;
};

struct Vnetcfg
{
	u16int	mac0;
	u16int	mac1;
	u16int	mac2;
	u16int	status;
	u16int	maxqueuepairs;
	u16int	mtu;
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

	uint	nintr;
	uint	nnote;

	/* notify register */
	void	*notify;
};

struct Ctlr {
	Lock;

	QLock	ctllock;

	int	attached;

	/* registers */
	Vconfig	*cfg;
	Vnetcfg *dev;
	u8int	*isr;
	u8int	*notify;
	u32int	notifyoffmult;

	uvlong	port;
	Pcidev	*pcidev;
	Ctlr	*next;
	int	active;
	ulong	feat[2];
	int	nqueue;

	/* virtioether has 3 queues: rx, tx and ctl */
	Vqueue	queue[3];
};

static Ctlr *ctlrhead;

static int
vhasroom(void *v)
{
	Vqueue *q = v;
	return q->lastused != q->used->idx;
}

static void
vqnotify(Ctlr *ctlr, int x)
{
	Vqueue *q;

	coherence();
	q = &ctlr->queue[x];
	if(q->used->flags & Unonotify)
		return;
	q->nnote++;
	*((u16int*)q->notify) = x;
}

static void
txproc(void *v)
{
	Vheader *header;
	Block **blocks;
	Ether *edev;
	Ctlr *ctlr;
	Vqueue *q;
	Vused *u;
	Block *b;
	int i, j;

	edev = v;
	ctlr = edev->ctlr;
	q = &ctlr->queue[Vtxq];

	header = smalloc(VheaderSize);
	blocks = smalloc(sizeof(Block*) * (q->qsize/2));

	for(i = 0; i < q->qsize/2; i++){
		j = i << 1;
		q->desc[j].addr = PADDR(header);
		q->desc[j].len = VheaderSize;
		q->desc[j].next = j | 1;
		q->desc[j].flags = Dnext;

		q->availent[i] = q->availent[i + q->qsize/2] = j;

		j |= 1;
		q->desc[j].next = 0;
		q->desc[j].flags = 0;
	}

	q->avail->flags &= ~Rnointerrupt;

	while(waserror())
		;

	while((b = qbread(edev->oq, 1000000)) != nil){
		for(;;){
			/* retire completed packets */
			while((i = q->lastused) != q->used->idx){
				u = &q->usedent[i & q->qmask];
				i = (u->id & q->qmask) >> 1;
				if(blocks[i] == nil)
					break;
				freeb(blocks[i]);
				blocks[i] = nil;
				q->lastused++;
			}

			/* have free slot? */
			i = q->avail->idx & (q->qmask >> 1);
			if(blocks[i] == nil)
				break;

			/* ring full, wait and retry */
			if(!vhasroom(q))
				sleep(q, vhasroom, q);
		}

		/* slot is free, fill in descriptor */
		blocks[i] = b;
		j = (i << 1) | 1;
		q->desc[j].addr = PADDR(b->rp);
		q->desc[j].len = BLEN(b);
		coherence();
		q->avail->idx++;
		vqnotify(ctlr, Vtxq);
	}

	pexit("ether out queue closed", 1);
}

static void
rxproc(void *v)
{
	Vheader *header;
	Block **blocks;
	Ether *edev;
	Ctlr *ctlr;
	Vqueue *q;
	Vused *u;
	Block *b;
	int i, j;

	edev = v;
	ctlr = edev->ctlr;
	q = &ctlr->queue[Vrxq];

	header = smalloc(VheaderSize);
	blocks = smalloc(sizeof(Block*) * (q->qsize/2));

	for(i = 0; i < q->qsize/2; i++){
		j = i << 1;
		q->desc[j].addr = PADDR(header);
		q->desc[j].len = VheaderSize;
		q->desc[j].next = j | 1;
		q->desc[j].flags = Dwrite|Dnext;

		q->availent[i] = q->availent[i + q->qsize/2] = j;

		j |= 1;
		q->desc[j].next = 0;
		q->desc[j].flags = Dwrite;
	}

	q->avail->flags &= ~Rnointerrupt;

	while(waserror())
		;

	for(;;){
		/* replenish receive ring */
		do {
			i = q->avail->idx & (q->qmask >> 1);
			if(blocks[i] != nil)
				break;
			if((b = iallocb(ETHERMAXTU)) == nil)
				break;
			blocks[i] = b;
			j = (i << 1) | 1;
			q->desc[j].addr = PADDR(b->rp);
			q->desc[j].len = BALLOC(b);
			coherence();
			q->avail->idx++;
		} while(q->avail->idx != q->used->idx);
		vqnotify(ctlr, Vrxq);

		/* wait for any packets to complete */
		if(!vhasroom(q))
			sleep(q, vhasroom, q);

		/* retire completed packets */
		while((i = q->lastused) != q->used->idx) {
			u = &q->usedent[i & q->qmask];
			i = (u->id & q->qmask) >> 1;
			if((b = blocks[i]) == nil)
				break;

			blocks[i] = nil;
			b->wp = b->rp + u->len - VheaderSize;
			etheriq(edev, b);
			q->lastused++;
		}
	}
}

static int
vctlcmd(Ether *edev, uchar class, uchar cmd, uchar *data, int ndata)
{
	uchar hdr[2], ack[1];
	Ctlr *ctlr;
	Vqueue *q;
	Vdesc *d;
	int i;

	ctlr = edev->ctlr;
	q = &ctlr->queue[Vctlq];
	if(q->qsize < 3)
		return -1;

	qlock(&ctlr->ctllock);
	while(waserror())
		;

	ack[0] = 0x55;
	hdr[0] = class;
	hdr[1] = cmd;

	d = &q->desc[0];
	d->addr = PADDR(hdr);
	d->len = sizeof(hdr);
	d->next = 1;
	d->flags = Dnext;
	d++;
	d->addr = PADDR(data);
	d->len = ndata;
	d->next = 2;
	d->flags = Dnext;
	d++;
	d->addr = PADDR(ack);
	d->len = sizeof(ack);
	d->next = 0;
	d->flags = Dwrite;

	i = q->avail->idx & q->qmask;
	q->availent[i] = 0;
	coherence();

	q->avail->flags &= ~Rnointerrupt;
	q->avail->idx++;
	vqnotify(ctlr, Vctlq);
	while(!vhasroom(q))
		sleep(q, vhasroom, q);
	q->lastused = q->used->idx;
	q->avail->flags |= Rnointerrupt;

	qunlock(&ctlr->ctllock);
	poperror();

	if(ack[0] != 0)
		print("#l%d: vctlcmd: %ux.%ux -> %ux\n", edev->ctlrno, class, cmd, ack[0]);

	return ack[0];
}

static void
interrupt(Ureg*, void* arg)
{
	Ether *edev;
	Ctlr *ctlr;
	Vqueue *q;
	int i;

	edev = arg;
	ctlr = edev->ctlr;
	if(*ctlr->isr & 1){
		for(i = 0; i < ctlr->nqueue; i++){
			q = &ctlr->queue[i];
			if(vhasroom(q)){
				q->nintr++;
				wakeup(q);
			}
		}
	}
}

static void
attach(Ether* edev)
{
	char name[KNAMELEN];
	Ctlr* ctlr;
	int i;

	ctlr = edev->ctlr;
	ilock(ctlr);
	if(ctlr->attached){
		iunlock(ctlr);
		return;
	}
	ctlr->attached = 1;

	/* enable the queues */
	for(i = 0; i < ctlr->nqueue; i++){
		ctlr->cfg->queuesel = i;
		ctlr->cfg->queueenable = 1;
	}

	/* driver is ready */
	ctlr->cfg->status |= Sdriverok;

	iunlock(ctlr);

	/* start kprocs */
	snprint(name, sizeof name, "#l%drx", edev->ctlrno);
	kproc(name, rxproc, edev);
	snprint(name, sizeof name, "#l%dtx", edev->ctlrno);
	kproc(name, txproc, edev);
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

	l = snprint(p, READSTR, "devfeat %32.32lub %32.32lub\n", ctlr->feat[1], ctlr->feat[0]);
	l += snprint(p+l, READSTR-l, "devstatus %8.8ub\n", ctlr->cfg->status);

	for(i = 0; i < ctlr->nqueue; i++){
		q = &ctlr->queue[i];
		l += snprint(p+l, READSTR-l,
			"vq%d %#p size %d avail->idx %d used->idx %d lastused %hud nintr %ud nnote %ud\n",
			i, q, q->qsize, q->avail->idx, q->used->idx, q->lastused, q->nintr, q->nnote);
	}

	n = readstr(offset, a, n, p);
	free(p);

	return n;
}

static void
shutdown(Ether* edev)
{
	Ctlr *ctlr = edev->ctlr;

	coherence();
	ctlr->cfg->status = 0;
	coherence();

	pciclrbme(ctlr->pcidev);
}

static void
promiscuous(void *arg, int on)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	uchar b[1];

	if((ctlr->feat[0] & (Fctrlvq|Fctrlrx)) != (Fctrlvq|Fctrlrx))
		return;

	b[0] = on != 0;
	vctlcmd(edev, CtrlRx, CmdPromisc, b, sizeof(b));
}

static void
multicast(void *arg, uchar*, int)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	uchar b[1];

    if((ctlr->feat[0] & (Fctrlvq|Fctrlrx)) != (Fctrlvq|Fctrlrx))
		return;

	b[0] = edev->nmaddr > 0;
	vctlcmd(edev, CtrlRx, CmdAllmulti, b, sizeof(b));
}

static int
initqueue(Vqueue *q, int size)
{
	uchar *p;

	q->desc = mallocalign(VdescSize*size, 16, 0, 0);
	if(q->desc == nil)
		return -1;
	p = mallocalign(VringSize + 2*size + 2, 2, 0, 0);
	if(p == nil){
FreeDesc:
		free(q->desc);
		q->desc = nil;
		return -1;
	}
	q->avail = (void*)p;
	p += VringSize;
	q->availent = (void*)p;
	p += sizeof(u16int)*size;
	q->availevent = (void*)p;
	p = mallocalign(VringSize + VusedSize*size + 2, 4, 0, 0);
	if(p == nil){
		free(q->avail);
		q->avail = nil;
		goto FreeDesc;
	}
	q->used = (void*)p;
	p += VringSize;
	q->usedent = (void*)p;
	p += VusedSize*size;
	q->usedevent = (void*)p;

	q->qsize = size;
	q->qmask = q->qsize - 1;

	q->lastused = q->avail->idx = q->used->idx = 0;

	q->avail->flags |= Rnointerrupt;

	return 0;
}

static int
matchvirtiocfgcap(Pcidev *p, int cap, int off, int typ)
{
	int bar;

	if(cap != 9 || pcicfgr8(p, off+3) != typ)
		return 1;

	/* skip invalid or non memory bars */
	bar = pcicfgr8(p, off+4);
	if(bar < 0 || bar >= nelem(p->mem) 
	|| p->mem[bar].size == 0
	|| (p->mem[bar].bar & 3) != 0)
		return 1;

	return 0;
}

static int
virtiocap(Pcidev *p, int typ)
{
	return pcienumcaps(p, matchvirtiocfgcap, typ);
}

static void*
virtiomapregs(Pcidev *p, int cap, int size)
{
	int bar, len;
	uvlong addr;

	if(cap < 0)
		return nil;
	bar = pcicfgr8(p, cap+4) % nelem(p->mem);
	addr = pcicfgr32(p, cap+8);
	len = pcicfgr32(p, cap+12);
	if(size <= 0)
		size = len;
	else if(len < size)
		return nil;
	if(addr+len > p->mem[bar].size)
		return nil;
	addr += p->mem[bar].bar & ~0xFULL;
	return vmap(addr, size);
}

static Ctlr*
pciprobe(void)
{
	Ctlr *c, *h, *t;
	Pcidev *p;
	Vconfig *cfg;
	int bar, cap, n, i;

	h = t = nil;

	/* §4.1.2 PCI Device Discovery */
	for(p = nil; p = pcimatch(p, 0x1AF4, 0x1041);){
		/* non-transitional devices will have a revision > 0 */
		if(p->rid == 0)
			continue;
		if((cap = virtiocap(p, 1)) < 0)
			continue;
		bar = pcicfgr8(p, cap+4) % nelem(p->mem);
		cfg = virtiomapregs(p, cap, sizeof(Vconfig));
		if(cfg == nil)
			continue;
		if((c = mallocz(sizeof(Ctlr), 1)) == nil){
			print("ethervirtio: no memory for Ctlr\n");
			break;
		}
		c->cfg = cfg;
		c->pcidev = p;
		c->port = p->mem[bar].bar & ~0xFULL;

		pcienable(p);
		c->dev = virtiomapregs(p, virtiocap(p, 4), sizeof(Vnetcfg));
		if(c->dev == nil)
			goto Baddev;
		c->isr = virtiomapregs(p, virtiocap(p, 3), 0);
		if(c->isr == nil)
			goto Baddev;
		cap = virtiocap(p, 2);
		c->notify = virtiomapregs(p, cap, 0);
		if(c->notify == nil)
			goto Baddev;
		c->notifyoffmult = pcicfgr32(p, cap+16);

		/* device reset */
		coherence();
		cfg->status = 0;
		while(cfg->status != 0)
			delay(1);
		cfg->status = Sacknowledge|Sdriver;

		/* negotiate feature bits */
		cfg->devfeatsel = 1;
		c->feat[1] = cfg->devfeat;

		cfg->devfeatsel = 0;
		c->feat[0] = cfg->devfeat;

		cfg->drvfeatsel = 1;
		cfg->drvfeat = c->feat[1] & Fversion1;

		cfg->drvfeatsel = 0;
		cfg->drvfeat = c->feat[0] & (Fmac|Fctrlvq|Fctrlrx);

		cfg->status |= Sfeaturesok;

		for(i=0; i<nelem(c->queue); i++){
			cfg->queuesel = i;
			n = cfg->queuesize;
			if(n == 0 || (n & (n-1)) != 0){
				if(i < 2)
					print("ethervirtio: queue %d has invalid size %d\n", i, n);
				break;
			}
			if(initqueue(&c->queue[i], n) < 0)
				break;
			c->queue[i].notify = c->notify + c->notifyoffmult * cfg->queuenotifyoff;
			coherence();
			cfg->queuedesc = PADDR(c->queue[i].desc);
			cfg->queueavail = PADDR(c->queue[i].avail);
			cfg->queueused = PADDR(c->queue[i].used);
		}
		if(i < 2){
			print("ethervirtio: no queues\n");
Baddev:
			pcidisable(p);
			/* TODO, vunmap */
			free(c);
			continue;
		}
		c->nqueue = i;		

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
	static uchar zeros[Eaddrlen];
	Ctlr *ctlr;
	int i;

	if(ctlrhead == nil)
		ctlrhead = pciprobe();

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

	if((ctlr->feat[0] & Fmac) != 0 && memcmp(edev->ea, zeros, Eaddrlen) == 0){
		for(i = 0; i < Eaddrlen; i++)
			edev->ea[i] = ((uchar*)ctlr->dev)[i];
	} else {
		for(i = 0; i < Eaddrlen; i++)
			((uchar*)ctlr->dev)[i] = edev->ea[i];
	}

	edev->arg = edev;

	edev->attach = attach;
	edev->shutdown = shutdown;
	edev->ifstat = ifstat;
	edev->multicast = multicast;
	edev->promiscuous = promiscuous;


	pcisetbme(ctlr->pcidev);
	intrenable(edev->irq, interrupt, edev, edev->tbdf, edev->name);

	return 0;
}

void
ethervirtio10link(void)
{
	addethercard("virtio10", reset);
}
