#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

#include "../port/sd.h"

typedef struct Vioreqhdr Vioreqhdr;
typedef struct Vringhdr Vringhdr;
typedef struct Vdesc Vdesc;
typedef struct Vused Vused;
typedef struct Vqueue Vqueue;
typedef struct Vdev Vdev;

enum {
	Acknowledge	= 1,
	Driver		= 2,
	DriverOk	= 4,
	Failed		= 128,
};

enum {
	Devfeat = 0,
	Drvfeat = 4,
	Qaddr = 8,
	Qsize = 12,
	Qselect = 14,
	Qnotify = 16,
	Status = 18,
	Isr = 19,

	Devspec = 20,
};

enum {
	Next = 1,
	Write = 2,
	Indirect = 4,
};	

struct Vioreqhdr
{
	u32int	typ;
	u32int	prio;
	u64int	lba;
};

struct Vringhdr
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

struct Vqueue
{
	int		size;

	int		free;
	int		nfree;

	Vdesc		*desc;

	Vringhdr	*avail;
	u16int		*availent;
	u16int		*availevent;

	Vringhdr	*used;
	Vused		*usedent;
	u16int		*usedevent;

	u16int		lastused;

	Rendez;
	QLock;
	Lock;
};

struct Vdev
{
	int	typ;

	Pcidev	*pci;

	ulong	port;
	ulong	features;

	int	nqueue;
	Vqueue	*queue[16];

	Vdev	*next;
};

static Vqueue*
mkvqueue(int size)
{
	Vqueue *q;
	uchar *p;
	int i;

	q = malloc(sizeof(*q));
	p = mallocalign(
		PGROUND(sizeof(Vdesc)*size + 
			sizeof(Vringhdr) + 
			sizeof(u16int)*size + 
			sizeof(u16int)) +
		PGROUND(sizeof(Vringhdr) + 
			sizeof(Vused)*size + 
			sizeof(u16int)), 
		BY2PG, 0, 0);
	if(p == nil || q == nil){
		print("mkvqueue: no memory for Vqueue\n");
		free(p);
		free(q);
		return nil;
	}

	q->desc = (void*)p;
	p += sizeof(Vdesc)*size;
	q->avail = (void*)p;
	p += sizeof(Vringhdr);
	q->availent = (void*)p;
	p += sizeof(u16int)*size;
	q->availevent = (void*)p;
	p += sizeof(u16int);

	p = (uchar*)PGROUND((ulong)p);
	q->used = (void*)p;
	p += sizeof(Vringhdr);
	q->usedent = (void*)p;
	p += sizeof(Vused)*size;
	q->usedevent = (void*)p;

	q->free = -1;
	q->nfree = q->size = size;
	for(i=0; i<size; i++){
		q->desc[i].next = q->free;
		q->free = i;
	}

	return q;
}

static Vdev*
viopnpdevs(int typ)
{
	Vdev *vd, *head, *tail;
	Pcidev *p;
	u32int a;
	int n, i;

	head = tail = nil;
	for(p = nil; p = pcimatch(p, 0, 0);){
		if(p->vid != 0x1AF4)
			continue;
		if((p->did < 0x1000) || (p->did >= 0x1040))
			continue;
		if(p->rid != 0)
			continue;
		if(pcicfgr16(p, 0x2E) != typ)
			continue;
		if((vd = malloc(sizeof(*vd))) == nil){
			print("viopnpdevs: cannot allocate memory for Vdev\n");
			break;
		}
		vd->port = p->mem[0].bar & ~0x1;
		vd->typ = typ;
		vd->pci = p;
		vd->features = inl(vd->port+Devfeat);
		outb(vd->port+Status, inb(vd->port+Status)|Acknowledge|Driver);
		for(i=0; i<nelem(vd->queue); i++){
			outs(vd->port+Qselect, i);
			if((n = ins(vd->port+Qsize)) == 0)
				break;
			if((vd->queue[i] = mkvqueue(n)) == nil)
				break;
			coherence();
			a = PADDR(vd->queue[i]->desc)/BY2PG;
			outl(vd->port+Qaddr, a);
		}
		vd->nqueue = i;
	
		if(head == nil)
			head = vd;
		else
			tail->next = vd;
		tail = vd;
	}
	return head;
}

struct Rock {
	Vqueue *q;
	int id;
	int done;
};

static void
viointerrupt(Ureg *, void *arg)
{
	Vdev *vd;
	int i;

	vd = arg;
	if(inb(vd->port+Isr) & 1)
		for(i=0; i<vd->nqueue; i++)
			wakeup(vd->queue[i]);
}

static int
viodone(void *arg)
{
	struct Rock *r;
	Vqueue *q;
	u16int i;

	r = arg;
	q = r->q;
	for(i = q->lastused; i != q->used->idx; i++)
		if(q->usedent[i % q->size].id == r->id){
			if(i == q->lastused)
				q->lastused++;
			r->done = 1;
			break;
		}
	return r->done;
}

static void
viowait(Vqueue *q, int id)
{
	struct Rock r;

	r.q = q;
	r.id = id;
	r.done = 0;
	do {
		qlock(q);
		while(waserror())
			;
		sleep(q, viodone, &r);
		poperror();
		qunlock(q);
	} while(!r.done);
}

static long
viobio(SDunit *u, int, int write, void *a, long count, uvlong lba)
{
	int i, free, head;
	u8int status;
	Vioreqhdr h;
	Vqueue *q;
	Vdev *vd;
	Vdesc *d;
	uchar *p;

	vd = u->dev->ctlr;
	q = vd->queue[0];

	lock(q);
	if(q->nfree < (2+count)){
		unlock(q);
		error("out of virtio descriptors");
	}
	head = free = q->free;

	status = 0;
	h.typ = write != 0;
	h.lba = lba;
	h.prio = 0;

	d = &q->desc[free]; free = d->next;
	d->addr = PADDR(&h);
	d->len = sizeof(h);
	d->flags = Next;

	p = a;
	for(i = 0; i<count; i++){
		d = &q->desc[free]; free = d->next;
		d->addr = PADDR(p);
		d->len = u->secsize;
		d->flags = write ? Next : (Write|Next);
		p += d->len;
	}

	d = &q->desc[free]; free = d->next;
	d->addr = PADDR(&status);
	d->len = sizeof(status);
	d->flags = Write;
	d->next = -1;

	q->free = free;
	q->nfree -= 2+count;

	coherence();
	q->availent[q->avail->idx++ % q->size] = head;
	unlock(q);

	coherence();
	outs(vd->port+Qnotify, 0);

	viowait(q, head);

	lock(q);
	d->next = q->free;
	q->free = head;
	q->nfree += 2+count;
	unlock(q);

	if(status != 0)
		error(Eio);

	return count*u->secsize;
}

static int
viorio(SDreq *r)
{
	int i, count, rw;
	uvlong lba;
	SDunit *u;

	u = r->unit;
	if(r->cmd[0] == 0x35 || r->cmd[0] == 0x91){
		/* flush */
		// return sdsetsense(r, SDok, 0, 0, 0);
		return sdsetsense(r, SDcheck, 3, 0xc, 2);
	}
	if((i = sdfakescsi(r)) != SDnostatus){
		r->status = i;
		return i;
	}
	if((i = sdfakescsirw(r, &lba, &count, &rw)) != SDnostatus)
		return i;
	r->rlen = viobio(u, r->lun, rw == SDwrite, r->data, count, lba);
	return r->status = SDok;
}

static int
vioonline(SDunit *u)
{
	uvlong cap;
	Vdev *vd;

	vd = u->dev->ctlr;
	cap = inl(vd->port+Devspec+4);
	cap <<= 32;
	cap |= inl(vd->port+Devspec);
	if(u->sectors != cap){
		u->sectors = cap;
		u->secsize = 512;
		return 2;
	}
	return 1;
}

static int
vioverify(SDunit *)
{
	return 1;
}

SDifc sdvirtioifc;

static SDev*
viopnp(void)
{
	SDev *s, *h, *t;
	Vdev *vd;
	int id;
	
	id = 'F';
	h = t = nil;
	for(vd =  viopnpdevs(2); vd; vd = vd->next){
		if(vd->nqueue != 1)
			continue;

		intrenable(vd->pci->intl, viointerrupt, vd, vd->pci->tbdf, "sdvirtio");
		outb(vd->port+Status, inb(vd->port+Status) | DriverOk);

		s = malloc(sizeof(*s));
		if(s == nil)
			break;
		s->ctlr = vd;
		s->idno = id++;
		s->ifc = &sdvirtioifc;
		s->nunit = 1;
		if(h)
			t->next = s;
		else
			h = s;
		t = s;
	}

	return h;
}

SDifc sdvirtioifc = {
	"virtio",			/* name */

	viopnp,				/* pnp */
	nil,				/* legacy */
	nil,				/* enable */
	nil,				/* disable */

	vioverify,			/* verify */
	vioonline,			/* online */
	viorio,				/* rio */
	nil,				/* rctl */
	nil,				/* wctl */

	viobio,				/* bio */
	nil,				/* probe */
	nil,				/* clear */
	nil,				/* rtopctl */
	nil,				/* wtopctl */
};
