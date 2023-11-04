/*
 * virtio 1.0 disk driver
 * http://docs.oasis-open.org/virtio/virtio/v1.0/virtio-v1.0.html
 *
 * In contrast to sdvirtio.c, this driver handles the non-legacy
 * interface for virtio disk which uses mmio for all register accesses
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
#include "ureg.h"
#include "../port/error.h"

#include "../port/sd.h"

typedef struct Vscsidev Vscsidev;
typedef struct Vblkdev Vblkdev;

typedef struct Vconfig Vconfig;
typedef struct Vring Vring;
typedef struct Vdesc Vdesc;
typedef struct Vused Vused;
typedef struct Vqueue Vqueue;
typedef struct Vdev Vdev;


/* device types */
enum {
	TypBlk	= 2,
	TypSCSI	= 8,
};

/* status flags */
enum {
	Acknowledge = 1,
	Driver = 2,
	FeaturesOk = 8,
	DriverOk = 4,
	Failed = 0x80,
};

/* descriptor flags */
enum {
	Next = 1,
	Write = 2,
	Indirect = 4,
};

/* struct sizes */
enum {
	VringSize = 4,
};

enum {
	CDBSIZE		= 32,
	SENSESIZE	= 96,
};

	
struct Vscsidev
{
	u32int	num_queues;
	u32int	seg_max;
	u32int	max_sectors;
	u32int	cmd_per_lun;
	u32int	event_info_size;
	u32int	sense_size;
	u32int	cdb_size;
	u16int	max_channel;
	u16int	max_target;
	u32int	max_lun;
};

struct Vblkdev
{
	u64int	capacity;
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

	Vdev	*dev;
	void	*notify;
	int	idx;

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

	uvlong	port;
	ulong	feat[2];

	int	nqueue;
	Vqueue	*queue[16];

	void	*dev;	/* device specific config (for scsi) */

	/* registers */
	Vconfig	*cfg;
	u8int	*isr;
	u8int	*notify;
	u32int	notifyoffmult;

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
			VringSize + 
			sizeof(u16int)*size + 
			sizeof(u16int)) +
		PGROUND(VringSize + 
			sizeof(Vused)*size + 
			sizeof(u16int)), 
		BY2PG, 0, 0);
	if(p == nil || q == nil){
		print("virtio: no memory for Vqueue\n");
		free(p);
		free(q);
		return nil;
	}

	q->desc = (void*)p;
	p += sizeof(Vdesc)*size;
	q->avail = (void*)p;
	p += VringSize;
	q->availent = (void*)p;
	p += sizeof(u16int)*size;
	q->availevent = (void*)p;
	p += sizeof(u16int);

	p = (uchar*)PGROUND((uintptr)p);
	q->used = (void*)p;
	p += VringSize;
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

static Vdev*
viopnpdevs(int typ)
{
	Vdev *vd, *h, *t;
	Vconfig *cfg;
	Vqueue *q;
	Pcidev *p;
	int cap, bar;
	int n, i;

	h = t = nil;
	for(p = nil; p = pcimatch(p, 0x1AF4, 0x1040+typ);){
		if(p->rid == 0)
			continue;
		if((cap = virtiocap(p, 1)) < 0)
			continue;
		bar = pcicfgr8(p, cap+4) % nelem(p->mem);
		cfg = virtiomapregs(p, cap, sizeof(Vconfig));
		if(cfg == nil)
			continue;
		if((vd = malloc(sizeof(*vd))) == nil){
			print("virtio: no memory for Vdev\n");
			break;
		}
		vd->port = p->mem[bar].bar & ~0xFULL;
		vd->typ = typ;
		vd->pci = p;
		vd->cfg = cfg;
		pcienable(p);

		vd->isr = virtiomapregs(p, virtiocap(p, 3), 0);
		if(vd->isr == nil){
Baddev:
			pcidisable(p);
			/* TODO: vunmap */
			free(vd);
			continue;
		}
		cap = virtiocap(p, 2);
		vd->notify = virtiomapregs(p, cap, 0);
		if(vd->notify == nil)
			goto Baddev;
		vd->notifyoffmult = pcicfgr32(p, cap+16);

		/* reset */
		cfg->status = 0;
		while(cfg->status != 0)
			delay(1);
		cfg->status = Acknowledge|Driver;

		/* negotiate feature bits */
		cfg->devfeatsel = 1;
		vd->feat[1] = cfg->devfeat;
		cfg->devfeatsel = 0;
		vd->feat[0] = cfg->devfeat;
		cfg->drvfeatsel = 1;
		cfg->drvfeat = vd->feat[1] & 1;
		cfg->drvfeatsel = 0;
		cfg->drvfeat = 0;
		cfg->status |= FeaturesOk;

		for(i=0; i<nelem(vd->queue); i++){
			cfg->queuesel = i;
			n = cfg->queuesize;
			if(n == 0 || (n & (n-1)) != 0)
				break;
			if((q = mkvqueue(n)) == nil)
				break;
			q->notify = vd->notify + vd->notifyoffmult * cfg->queuenotifyoff;
			q->dev = vd;
			q->idx = i;
			vd->queue[i] = q;
			coherence();
			cfg->queuedesc = PADDR(q->desc);
			cfg->queueavail = PADDR(q->avail);
			cfg->queueused = PADDR(q->used);
		}
		vd->nqueue = i;
	
		if(h == nil)
			h = vd;
		else
			t->next = vd;
		t = vd;
	}

	return h;
}

struct Rock {
	int done;
	Rendez *sleep;
};

static void
vqinterrupt(Vqueue *q)
{
	int id, free, m;
	struct Rock *r;
	Rendez *z;

	m = q->size-1;

	ilock(q);
	while((q->lastused ^ q->used->idx) & m){
		id = q->usedent[q->lastused++ & m].id;
		if(r = q->rock[id]){
			q->rock[id] = nil;
			z = r->sleep;
			r->done = 1;	/* hands off */
			if(z != nil)
				wakeup(z);
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

static void
viointerrupt(Ureg *, void *arg)
{
	Vdev *vd = arg;

	if(vd->isr[0] & 1)
		vqinterrupt(vd->queue[vd->typ == TypSCSI ? 2 : 0]);
}

static int
viodone(void *arg)
{
	return ((struct Rock*)arg)->done;
}

static void
vqio(Vqueue *q, int head)
{
	struct Rock rock;

	rock.done = 0;
	rock.sleep = &up->sleep;
	q->rock[head] = &rock;
	q->availent[q->avail->idx & (q->size-1)] = head;
	coherence();
	q->avail->idx++;
	iunlock(q);
	if((q->used->flags & 1) == 0)
		*((u16int*)q->notify) = q->idx;
	while(!rock.done){
		while(waserror())
			;
		tsleep(rock.sleep, viodone, &rock, 1000);
		poperror();

		if(!rock.done)
			vqinterrupt(q);
	}
}

static int
vioblkreq(Vdev *vd, int typ, void *a, long count, long secsize, uvlong lba)
{
	int need, free, head;
	Vqueue *q;
	Vdesc *d;

	u8int status;
	struct Vioblkreqhdr {
		u32int	typ;
		u32int	prio;
		u64int	lba;
	} req;

	need = 2;
	if(a != nil)
		need = 3;

	status = -1;
	req.typ = typ;
	req.prio = 0;
	req.lba = lba;

	q = vd->queue[0];
	ilock(q);
	while(q->nfree < need){
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

	if(a != nil){
		d = &q->desc[free]; free = d->next;
		d->addr = PADDR(a);
		d->len = secsize*count;
		d->flags = typ ? Next : (Write|Next);
	}

	d = &q->desc[free]; free = d->next;
	d->addr = PADDR(&status);
	d->len = sizeof(status);
	d->flags = Write;

	q->free = free;
	q->nfree -= need;

	/* queue io, unlock and wait for completion */
	vqio(q, head);

	return status;
}

static int
vioscsireq(SDreq *r)
{
	u8int resp[4+4+2+2+SENSESIZE];
	u8int req[8+8+3+CDBSIZE];
	int free, head;
	u32int len;
	Vqueue *q;
	Vdesc *d;
	Vdev *vd;
	SDunit *u;
	Vscsidev *scsi;

	u = r->unit;
	vd = u->dev->ctlr;
	scsi = vd->dev;

	memset(resp, 0, sizeof(resp));
	memset(req, 0, sizeof(req));
	req[0] = 1;
	req[1] = u->subno;
	req[2] = r->lun>>8;
	req[3] = r->lun&0xFF;
	*(u64int*)(&req[8]) = (uintptr)r;

	memmove(&req[8+8+3], r->cmd, r->clen);

	q = vd->queue[2];
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
	d->addr = PADDR(req);
	d->len = 8+8+3+scsi->cdb_size;
	d->flags = Next;

	if(r->write && r->dlen > 0){
		d = &q->desc[free]; free = d->next;
		d->addr = PADDR(r->data);
		d->len = r->dlen;
		d->flags = Next;
	}

	d = &q->desc[free]; free = d->next;
	d->addr = PADDR(resp);
	d->len = 4+4+2+2+scsi->sense_size;
	d->flags = Write;

	if(!r->write && r->dlen > 0){
		d->flags |= Next;

		d = &q->desc[free]; free = d->next;
		d->addr = PADDR(r->data);
		d->len = r->dlen;
		d->flags = Write;
	}
	
	q->free = free;
	q->nfree -= 2 + (r->dlen > 0);

	/* queue io, unlock and wait for completion */
	vqio(q, head);

	/* response+status */
	r->status = resp[10];
	if(resp[11] != 0)
		r->status = SDcheck;

	/* sense_len */
	len = *((u32int*)&resp[0]);
	if(len > 0){
		if(len > sizeof(r->sense))
			len = sizeof(r->sense);
		memmove(r->sense, &resp[4+4+2+2], len);
		r->flags |= SDvalidsense;
	}

	/* data residue */
	len = *((u32int*)&resp[4]);
	if(len > r->dlen)
		r->rlen = 0;
	else
		r->rlen = r->dlen - len;

	return r->status;

}

static long
viobio(SDunit *u, int lun, int write, void *a, long count, uvlong lba)
{
	long ss, cc, max, ret;
	Vdev *vd;

	vd = u->dev->ctlr;
	if(vd->typ == TypSCSI)
		return scsibio(u, lun, write, a, count, lba);

	max = 32;
	ss = u->secsize;
	ret = 0;
	while(count > 0){
		if((cc = count) > max)
			cc = max;
		if(vioblkreq(vd, write != 0, (uchar*)a + ret, cc, ss, lba) != 0)
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
	Vdev *vd;

	u = r->unit;
	vd = u->dev->ctlr;
	if(vd->typ == TypSCSI)
		return vioscsireq(r);
	if(r->cmd[0] == 0x35 || r->cmd[0] == 0x91){
		if(vioblkreq(vd, 4, nil, 0, 0, 0) != 0)
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
	Vdev *vd;
	Vblkdev *blk;
	uvlong cap;

	vd = u->dev->ctlr;
	if(vd->typ == TypSCSI)
		return scsionline(u);

	blk = vd->dev;
	cap = blk->capacity;
	if(u->sectors != cap){
		u->sectors = cap;
		u->secsize = 512;
		return 2;
	}
	return 1;
}

static int
vioverify(SDunit *u)
{
	Vdev *vd;

	vd = u->dev->ctlr;
	if(vd->typ == TypSCSI)
		return scsiverify(u);

	return 1;
}

SDifc sdvirtio10ifc;

static int
vioenable(SDev *sd)
{
	char name[32];
	Vdev *vd;
	int i;

	vd = sd->ctlr;
	pcisetbme(vd->pci);
	snprint(name, sizeof(name), "%s (%s)", sd->name, sd->ifc->name);
	intrenable(vd->pci->intl, viointerrupt, vd, vd->pci->tbdf, name);
	coherence();

	for(i = 0; i < vd->nqueue; i++){
		vd->cfg->queuesel = i;
		vd->cfg->queueenable = 1;
	}
	vd->cfg->status |= DriverOk;

	return 1;
}

static int
viodisable(SDev *sd)
{
	char name[32];
	Vdev *vd;

	vd = sd->ctlr;
	snprint(name, sizeof(name), "%s (%s)", sd->name, sd->ifc->name);
	intrdisable(vd->pci->intl, viointerrupt, vd, vd->pci->tbdf, name);
	pciclrbme(vd->pci);
	return 1;
}

static SDev*
viopnp(void)
{
	SDev *s, *h, *t;
	Vdev *vd;
	int id;

	h = t = nil;

	id = 'F';
	for(vd =  viopnpdevs(TypBlk); vd; vd = vd->next){
		if(vd->nqueue == 0)
			continue;

		if((vd->dev = virtiomapregs(vd->pci, virtiocap(vd->pci, 4), sizeof(Vblkdev))) == nil)
			break;
		if((s = malloc(sizeof(*s))) == nil)
			break;
		s->ctlr = vd;
		s->idno = id++;
		s->ifc = &sdvirtio10ifc;
		s->nunit = 1;
		if(h)
			t->next = s;
		else
			h = s;
		t = s;
	}

	id = '0';
	for(vd = viopnpdevs(TypSCSI); vd; vd = vd->next){
		Vscsidev *scsi;

		if(vd->nqueue < 3)
			continue;

		if((scsi = virtiomapregs(vd->pci, virtiocap(vd->pci, 4), sizeof(Vscsidev))) == nil)
			break;
		if(scsi->max_target == 0){
			vunmap(scsi, sizeof(Vscsidev));
			continue;
		}
		if((scsi->cdb_size > CDBSIZE) || (scsi->sense_size > SENSESIZE)){
			print("sdvirtio: cdb %ud or sense size %ud too big\n",
				scsi->cdb_size, scsi->sense_size);
			vunmap(scsi, sizeof(Vscsidev));
			continue;
		}
		vd->dev = scsi;

		if((s = malloc(sizeof(*s))) == nil)
			break;
		s->ctlr = vd;
		s->idno = id++;
		s->ifc = &sdvirtio10ifc;
		s->nunit = scsi->max_target;

		if(h)
			t->next = s;
		else
			h = s;
		t = s;
	}
	return h;
}

SDifc sdvirtio10ifc = {
	"virtio10",			/* name */

	viopnp,				/* pnp */
	vioenable,			/* enable */
	viodisable,			/* disable */

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
