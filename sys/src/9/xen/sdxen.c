/*
 * Xen block storage device frontend
 *
 * The present implementation follows the principle of
 * "what's the simplest thing that could possibly work?".
 * We can think about performance later.
 * We can think about dynamically attaching and removing devices later.
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

#include "../port/sd.h"

#define LOG(a)

/*
 * conversions to machine page numbers, pages and addresses
 */
#define MFN(pa)		(patomfn[(pa)>>PGSHIFT])
#define MFNPG(pa)		(MFN(pa)<<PGSHIFT)
#define PA2MA(pa)		(MFNPG(pa) | PGOFF(pa))
#define VA2MA(va)		PA2MA(PADDR(va))
#define VA2MFN(va)		MFN(PADDR(va))

enum {
	Ndevs		= 4,
	MajorDevSD	= 0x800,
	MajorDevHDA	= 0x300,
	MajorDevHDC	= 0x1600,
	MajorDevXVD	= 0xCA00,
};

extern SDifc sdxenifc;

typedef struct Ctlr Ctlr;

struct Ctlr {
	int	online;
	ulong	secsize;
	ulong	sectors;
	int	backend;
	int	devid;
	int	evtchn;
	blkif_front_ring_t ring;
	int	ringref;
	Lock	ringlock;
	char	*frame;
	QLock	iolock;
	int	iodone;
	Rendez	wiodone;
};

static int
ringinit(Ctlr *ctlr, char *a)
{
	blkif_sring_t *sr;

	sr = (blkif_sring_t*)a;
	memset(sr, 0, BY2PG);
	SHARED_RING_INIT(sr);
	FRONT_RING_INIT(&ctlr->ring, sr, BY2PG);
	ctlr->ringref = shareframe(ctlr->backend, sr, 1);
	return BY2PG;
}

static int
vbdsend(Ctlr *ctlr, int write, int ref, int nb, uvlong bno)
{
	blkif_request_t *req;
	int i, notify;

	ilock(&ctlr->ringlock);		// XXX conservative
	i = ctlr->ring.req_prod_pvt;
	req = RING_GET_REQUEST(&ctlr->ring, i);	// XXX overflow?

	req->operation = write ? BLKIF_OP_WRITE : BLKIF_OP_READ;
	req->nr_segments = 1;
	req->handle = ctlr->devid;
	req->id = 1;
	req->sector_number = bno;
	req->seg[0].gref = ref;
	req->seg[0].first_sect = 0;
	req->seg[0].last_sect = nb-1;

	ctlr->ring.req_prod_pvt = i+1;
	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&ctlr->ring, notify);
	iunlock(&ctlr->ringlock);
	return notify;
}

static void
backendconnect(Ctlr *ctlr)
{
	char dir[64];
	char buf[64];

	sprint(dir, "device/vbd/%d/", ctlr->devid);
	xenstore_setd(dir, "ring-ref", ctlr->ringref);
	xenstore_setd(dir, "event-channel", ctlr->evtchn);
	xenstore_setd(dir, "state", XenbusStateInitialised);
	xenstore_gets(dir, "backend", buf, sizeof buf);
	sprint(dir, "%s/", buf);
	HYPERVISOR_yield();
	xenstore_gets(dir, "state", buf, sizeof buf);
	while (strtol(buf, 0, 0) != XenbusStateConnected) {
		print("sdxen: waiting for vbd %d to connect\n", ctlr->devid);
		tsleep(&up->sleep, return0, 0, 50);
		xenstore_gets(dir, "state", buf, sizeof buf);
	}
	xenstore_gets(dir, "sector-size", buf, sizeof buf);
	ctlr->secsize = strtol(buf, 0, 0);
	xenstore_gets(dir, "sectors", buf, sizeof buf);
	ctlr->sectors = strtol(buf, 0, 0);
	print("sdxen: backend %s secsize %ld sectors %ld\n", dir, ctlr->secsize, ctlr->sectors);
	if (ctlr->secsize > BY2PG)
		panic("sdxen: sector size bigger than mmu page size");
}

static void
backendactivate(Ctlr *ctlr)
{
	char dir[64];

	sprint(dir, "device/vbd/%d/", ctlr->devid);
	xenstore_setd(dir, "state", XenbusStateConnected);
}

static SDev*
xenpnp(void)
{
	SDev *sdev[Ndevs];
	static char idno[Ndevs] = { '0', 'C', 'D', 'E' };
	static char nunit[Ndevs] = { 8, 2, 2, 8 };
	int i;

	for (i = 0; i < Ndevs; i++) {
		sdev[i] = mallocz(sizeof(SDev), 1);
		sdev[i]->ifc = &sdxenifc;
		sdev[i]->idno = idno[i];
		sdev[i]->nunit = nunit[i];
		sdev[i]->ctlr = (Ctlr**)mallocz(sdev[i]->nunit*sizeof(Ctlr*), 1);
		if (i > 0)
			sdev[i]->next = sdev[i-1];
	}
	return sdev[Ndevs-1];
}

static int
linuxdev(int idno, int subno)
{
	switch (idno) {
	case '0':
		return MajorDevSD + 16*subno;
	case 'C':
		return MajorDevHDA + 64*subno;
	case 'D':
		return MajorDevHDC + 64*subno;
	case 'E':
		return MajorDevXVD + 16*subno;
	default:
		return 0;
	}
}

static int
xenverify(SDunit *unit)
{
	Ctlr *ctlr;
	char dir[64];
	char buf[64];
	int devid;
	int npage;
	char *p;

	if (unit->subno > unit->dev->nunit)
		return 0;
	devid = linuxdev(unit->dev->idno, unit->subno);
	sprint(dir, "device/vbd/%d/", devid);
	if (xenstore_gets(dir, "backend-id", buf, sizeof buf) <= 0)
		return 0;

	ctlr = mallocz(sizeof(Ctlr), 1);
	((Ctlr**)unit->dev->ctlr)[unit->subno] = ctlr;
	ctlr->devid = devid;
	ctlr->backend = strtol(buf, 0, 0);

	npage = 2;
	p = xspanalloc(npage<<PGSHIFT, BY2PG, 0);
	p += ringinit(ctlr, p);
	ctlr->frame = p;
	ctlr->evtchn = xenchanalloc(ctlr->backend);
	backendconnect(ctlr);

	unit->inquiry[0] = 0;		// XXX how do we know if it's a CD?
	unit->inquiry[2] = 2;
	unit->inquiry[3] = 2;
	unit->inquiry[4] = sizeof(unit->inquiry)-4;
	strcpy((char*)&unit->inquiry[8], "Xen block device");

	return 1;
}

static int
wiodone(void *a)
{
	return ((Ctlr*)a)->iodone != 0;
}

static void
sdxenintr(Ureg *, void *a)
{
	Ctlr *ctlr = a;
	blkif_response_t *rsp;
	int i, avail;

	ilock(&ctlr->ringlock);	// XXX conservative
	for (;;) {
		RING_FINAL_CHECK_FOR_RESPONSES(&ctlr->ring, avail);
		if (!avail)
			break;
		i = ctlr->ring.rsp_cons;
		rsp = RING_GET_RESPONSE(&ctlr->ring, i);
		LOG(dprint("sdxen rsp %llud %d %d\n", rsp->id, rsp->operation, rsp->status);)
		if (rsp->status == BLKIF_RSP_OKAY)
			ctlr->iodone = 1;
		else
			ctlr->iodone = -1;
		ctlr->ring.rsp_cons = ++i;
	}
	iunlock(&ctlr->ringlock);
	if (ctlr->iodone)
		wakeup(&ctlr->wiodone);
}

static Ctlr *kickctlr;

static void
kickme(void)
{
	Ctlr *ctlr = kickctlr;
	shared_info_t *s;

	if (ctlr) {
		s = HYPERVISOR_shared_info;
		dprint("tick %d %d prod %d cons %d pending %x mask %x\n",
			 m->ticks, ctlr->iodone, ctlr->ring.sring->rsp_prod, ctlr->ring.rsp_cons,
			s->evtchn_pending[0], s->evtchn_mask[0]);
		sdxenintr(0, ctlr);
	}
}

static int
xenonline(SDunit *unit)
{
	Ctlr *ctlr;

	ctlr = ((Ctlr**)unit->dev->ctlr)[unit->subno];
	unit->sectors = ctlr->sectors;
	unit->secsize = ctlr->secsize;
	if (ctlr->online == 0) {
		intrenable(ctlr->evtchn, sdxenintr, ctlr, BUSUNKNOWN, "vbd");
		//kickctlr = ctlr;
		//addclock0link(kickme, 10000);
		backendactivate(ctlr);
		ctlr->online = 1;
	}

	return 1;
}

static int
xenrio(SDreq*)
{
	return -1;
}

static long
xenbio(SDunit* unit, int lun, int write, void* data, long nb, uvlong bno)
{
	Ctlr *ctlr;
	char *buf;
	long bcount, len;
	int ref;
	int n;

	USED(lun);	// XXX meaningless
	ctlr = ((Ctlr**)unit->dev->ctlr)[unit->subno];
	LOG(("xenbio %c %lux %ld %lld\n", write? 'w' : 'r', (ulong)data, nb, bno);)
	buf = data;
	// XXX extra copying & fragmentation could be avoided by
	// redefining sdmalloc() to get page-aligned buffers
	if ((ulong)data&(BY2PG-1))
		buf = ctlr->frame;
	bcount = BY2PG/unit->secsize;
	qlock(&ctlr->iolock);
	for (n = nb; n > 0; n -= bcount) {
		ref = shareframe(ctlr->backend, buf, !write);
		if (bcount > n)
			bcount = n;
		len = bcount*unit->secsize;
		if (write && buf == ctlr->frame)
			memmove(buf, data, len);
		ctlr->iodone = 0;
		if (vbdsend(ctlr, write, ref, bcount, bno))
			xenchannotify(ctlr->evtchn);
		LOG(dprint("sleeping %d prod %d cons %d pending %x mask %x \n", ctlr->iodone, ctlr->ring.sring->rsp_prod, ctlr->ring.rsp_cons,
						HYPERVISOR_shared_info->evtchn_pending[0], HYPERVISOR_shared_info->evtchn_mask[0]);)
		sleep(&ctlr->wiodone, wiodone, ctlr);
		xengrantend(ref);
		if (ctlr->iodone < 0) {
			qunlock(&ctlr->iolock);
			return -1;
		}
		if (buf == ctlr->frame) {
			if (!write)
				memmove(data, buf, len);
			data = (char*)data + len;
		} else
			buf += len;
		bno += bcount;
	}
	qunlock(&ctlr->iolock);
	return (nb-n)*unit->secsize;
}

static void
xenclear(SDev *)
{
}

SDifc sdxenifc = {
	"xen",				/* name */

	xenpnp,				/* pnp */
	0,			/* enable */
	0,			/* disable */

	xenverify,			/* verify */
	xenonline,			/* online */
	xenrio,				/* rio */
	0,			/* rctl */
	0,			/* wctl */

	xenbio,				/* bio */
	0,			/* probe */
	xenclear,			/* clear */
	0,			/* stat */
};
