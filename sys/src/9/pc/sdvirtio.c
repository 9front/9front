#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

#include "../port/sd.h"

typedef struct Vring Vring;
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

struct Vqueue
{
	Lock;
	int	size;

	int	free;
	int	nfree;

	Vdesc	*desc;

	Vring	*avail;
	u16int	*availent;
	u16int	*availevent;

	Vring	*used;
	Vused	*usedent;
	u16int	*usedevent;
	u16int	lastused;

	void	*rock[];
};

struct Vdev
{
	int	typ;

	Pcidev	*pci;

	ulong	port;
	ulong	feat;

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

	q = malloc(sizeof(*q) + sizeof(void*)*size);
	p = mallocalign(
		PGROUND(sizeof(Vdesc)*size + 
			sizeof(Vring) + 
			sizeof(u16int)*size + 
			sizeof(u16int)) +
		PGROUND(sizeof(Vring) + 
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
	p += sizeof(Vring);
	q->availent = (void*)p;
	p += sizeof(u16int)*size;
	q->availevent = (void*)p;
	p += sizeof(u16int);

	p = (uchar*)PGROUND((ulong)p);
	q->used = (void*)p;
	p += sizeof(Vring);
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

		/* reset */
		outb(vd->port+Status, 0);

		vd->feat = inl(vd->port+Devfeat);
		outb(vd->port+Status, Acknowledge|Driver);
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
	int done;
	Rendez *sleep;
};

static void
viointerrupt(Ureg *, void *arg)
{
	int id, free, m;
	struct Rock *r;
	Vqueue *q;
	Vdev *vd;

	vd = arg;
	if(inb(vd->port+Isr) & 1){
		q = vd->queue[0];
		m = q->size-1;

		ilock(q);
		while((q->lastused ^ q->used->idx) & m){
			id = q->usedent[q->lastused++ & m].id;
			if(r = q->rock[id]){
				q->rock[id] = nil;
				r->done = 1;
				wakeup(r->sleep);
			}
			do {
				free = id;
				id = q->desc[free].next;
				q->desc[free].next = q->free;
				q->free = free;
				q->nfree++;
			} while(q->desc[free].flags & Next);
		}
		iunlock(q);
	}
}

static int
viodone(void *arg)
{
	return ((struct Rock*)arg)->done;
}

static int
vioreq(Vdev *vd, int typ, void *a, long count, long secsize, uvlong lba)
{
	struct Rock rock;
	int free, head;
	Vqueue *q;
	Vdesc *d;

	u8int status;
	struct Vioreqhdr {
		u32int	typ;
		u32int	prio;
		u64int	lba;
	} req;

	status = 0;
	req.typ = typ;
	req.prio = 0;
	req.lba = lba;

	rock.done = 0;
	rock.sleep = &up->sleep;

	q = vd->queue[0];
	ilock(q);
	while(q->nfree < 3){
		iunlock(q);

		if(!waserror())
			tsleep(&up->sleep, return0, 0, 500);
		poperror();

		ilock(q);
	}

	head = free = q->free;

	d = &q->desc[free]; free = d->next;
	d->addr = PADDR(&req);
	d->len = sizeof(req);
	d->flags = Next;

	d = &q->desc[free]; free = d->next;
	d->addr = PADDR(a);
	d->len = secsize*count;
	d->flags = typ ? Next : (Write|Next);

	d = &q->desc[free]; free = d->next;
	d->addr = PADDR(&status);
	d->len = sizeof(status);
	d->flags = Write;

	q->free = free;
	q->nfree -= 3;

	q->rock[head] = &rock;

	coherence();
	q->availent[q->avail->idx++ & (q->size-1)] = head;
	coherence();
	outs(vd->port+Qnotify, 0);
	iunlock(q);

	while(!rock.done){
		while(waserror())
			;
		tsleep(rock.sleep, viodone, &rock, 1000);
		poperror();

		if(!rock.done)
			viointerrupt(nil, vd);
	}

	return status;
}

static long
viobio(SDunit *u, int, int write, void *a, long count, uvlong lba)
{
	long ss, cc, max, ret;
	Vdev *vd;

	max = 32;
	ss = u->secsize;
	vd = u->dev->ctlr;

	ret = 0;
	while(count > 0){
		if((cc = count) > max)
			cc = max;
		if(vioreq(vd, write != 0, (uchar*)a + ret, cc, ss, lba) != 0)
			error(Eio);
		ret += cc*ss;
		count -= cc;
		lba += cc;
	}

	return ret;
}

static int
viorio(SDreq *r)
{
	int i, count, rw;
	uvlong lba;
	SDunit *u;

	u = r->unit;
	if(r->cmd[0] == 0x35 || r->cmd[0] == 0x91){
		if(vioreq(u->dev->ctlr, 4, nil, 0, 0, 0) != 0)
			return sdsetsense(r, SDcheck, 3, 0xc, 2);
		return sdsetsense(r, SDok, 0, 0, 0);
	}
	if((i = sdfakescsi(r)) != SDnostatus)
		return r->status = i;
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
