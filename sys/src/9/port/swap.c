#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

static int	canflush(Proc*, Segment*);
static void	executeio(void);
static void	pageout(Proc*, Segment*);
static void	pagepte(int, Page**);
static void	pager(void*);

Image 	swapimage;

static 	int	swopen;
static	Page	**iolist;
static	int	ioptr;

static	ushort	ageclock;

void
swapinit(void)
{
	swapalloc.swmap = xalloc(conf.nswap);
	swapalloc.top = &swapalloc.swmap[conf.nswap];
	swapalloc.alloc = swapalloc.swmap;
	swapalloc.last = swapalloc.swmap;
	swapalloc.free = conf.nswap;
	swapalloc.xref = 0;

	iolist = xalloc(conf.nswppo*sizeof(Page*));
	if(swapalloc.swmap == 0 || iolist == 0)
		panic("swapinit: not enough memory");

	swapimage.notext = 1;
}

static uintptr
newswap(void)
{
	uchar *look;

	lock(&swapalloc);
	if(swapalloc.free == 0) {
		unlock(&swapalloc);
		return ~0;
	}
	look = memchr(swapalloc.last, 0, swapalloc.top-swapalloc.last);
	if(look == nil)
		look = memchr(swapalloc.swmap, 0, swapalloc.last-swapalloc.swmap);
	*look = 2;	/* ref for pte + io transaction */
	swapalloc.last = look;
	swapalloc.free--;
	unlock(&swapalloc);
	return (look-swapalloc.swmap) * BY2PG;
}

void
putswap(Page *p)
{
	uchar *idx;

	lock(&swapalloc);
	idx = &swapalloc.swmap[((uintptr)p)/BY2PG];
	if(*idx == 0)
		panic("putswap %#p ref == 0", p);

	if(*idx == 255) {
		if(swapalloc.xref == 0)
			panic("putswap %#p xref == 0", p);

		if(--swapalloc.xref == 0) {
			for(idx = swapalloc.swmap; idx < swapalloc.top; idx++) {
				if(*idx == 255) {
					*idx = 0;
					swapalloc.free++;
				}
			}
		}
	} else {
		if(--(*idx) == 0)
			swapalloc.free++;
	}
	unlock(&swapalloc);
}

void
dupswap(Page *p)
{
	uchar *idx;

	lock(&swapalloc);
	idx = &swapalloc.swmap[((uintptr)p)/BY2PG];
	if(*idx == 255)
		swapalloc.xref++;
	else {
		if(++(*idx) == 255)
			swapalloc.xref += 255;
	}
	unlock(&swapalloc);
}

int
swapcount(uintptr daddr)
{
	return swapalloc.swmap[daddr/BY2PG];
}

void
kickpager(void)
{
	static Ref started;

	if(started.ref || incref(&started) != 1)
		wakeup(&swapalloc.r);
	else
		kproc("pager", pager, 0);
}

static int
reclaim(void)
{
	ulong np;

	for(;;){
		if((np = pagereclaim(&fscache, 1000)) > 0) {
			if(0) print("reclaim: %lud fscache\n", np);
		} else if((np = pagereclaim(&swapimage, 1000)) > 0) {
			if(0) print("reclaim: %lud swap\n", np);
		} else if((np = imagereclaim(1000)) > 0) {
			if(0) print("reclaim: %lud image\n", np);
		}
		if(!needpages(nil))
			return 1;	/* have pages, done */
		if(np == 0)
			return 0;	/* didnt reclaim, need to swap */
		sched();
	}
}

static void
pager(void*)
{
	int i;
	Segment *s;
	Proc *p, *ep;

	p = proctab(0);
	ep = &p[conf.nproc];

	while(waserror())
		;

	for(;;){
		up->psstate = "Reclaim";
		if(reclaim()){
			up->psstate = "Idle";
			wakeup(&palloc.pwait[0]);
			wakeup(&palloc.pwait[1]);
			sleep(&swapalloc.r, needpages, nil);
			continue;
		}

		if(swapimage.c == nil || swapalloc.free == 0){
		Killbig:
			if(!freebroken())
				killbig("out of memory");
			sched();
			continue;
		}

		i = ageclock;
		do {
			if(++p >= ep){
				if(++ageclock == i)
					goto Killbig;
				p = proctab(0);
			}
		} while(p->state == Dead || p->noswap || !canqlock(&p->seglock));
		up->psstate = "Pageout";
		for(i = 0; i < NSEG; i++) {
			if((s = p->seg[i]) != nil) {
				switch(s->type&SG_TYPE) {
				default:
					break;
				case SG_TEXT:
					pageout(p, s);
					break;
				case SG_DATA:
				case SG_BSS:
				case SG_STACK:
				case SG_SHARED:
					pageout(p, s);
					break;
				}
			}
		}
		qunlock(&p->seglock);

		if(ioptr > 0) {
			up->psstate = "I/O";
			executeio();
		}
	}
}

static void
pageout(Proc *p, Segment *s)
{
	int type, i, size;
	short age;
	Pte *l;
	Page **pg, *entry;

	if(!canqlock(s))	/* We cannot afford to wait, we will surely deadlock */
		return;

	if(!canflush(p, s)) {	/* Able to invalidate all tlbs with references */
		qunlock(s);
		putseg(s);
		return;
	}

	if(waserror()) {
		qunlock(s);
		putseg(s);
		return;
	}

	/* Pass through the pte tables looking for memory pages to swap out */
	type = s->type&SG_TYPE;
	size = s->mapsize;
	for(i = 0; i < size; i++) {
		l = s->map[i];
		if(l == nil)
			continue;
		for(pg = l->first; pg <= l->last; pg++) {
			entry = *pg;
			if(pagedout(entry))
				continue;
			if(entry->modref & PG_REF) {
				entry->modref &= ~PG_REF;
				entry->refage = ageclock;
				continue;
			}
			age = (short)(ageclock - entry->refage);
			if(age < 16)
				continue;
			pagepte(type, pg);
		}
	}
	poperror();
	qunlock(s);
	putseg(s);
}

static int
canflush(Proc *p, Segment *s)
{
	int i;
	Proc *ep;

	if(incref(s) == 2)		/* Easy if we are the only user */
		return canpage(p);

	/* Now we must do hardwork to ensure all processes which have tlb
	 * entries for this segment will be flushed if we succeed in paging it out
	 */
	p = proctab(0);
	ep = &p[conf.nproc];
	while(p < ep) {
		if(p->state != Dead) {
			for(i = 0; i < NSEG; i++)
				if(p->seg[i] == s)
					if(!canpage(p))
						return 0;
		}
		p++;
	}
	return 1;
}

static void
pagepte(int type, Page **pg)
{
	uintptr daddr;
	Page *outp;

	outp = *pg;
	switch(type) {
	case SG_TEXT:				/* Revert to demand load */
		putpage(outp);
		*pg = nil;
		break;

	case SG_DATA:
	case SG_BSS:
	case SG_STACK:
	case SG_SHARED:
		if(ioptr >= conf.nswppo)
			break;

		/*
		 *  get a new swap address with swapcount 2, one for the pte
		 *  and one extra ref for us while we write the page to disk
		 */
		daddr = newswap();
		if(daddr == ~0)
			break;

		/* clear any pages referring to it from the cache */
		cachedel(&swapimage, daddr);

		/* forget anything that it used to cache */
		uncachepage(outp);

		/*
		 *  enter it into the cache so that a fault happening
		 *  during the write will grab the page from the cache
		 *  rather than one partially written to the disk
		 */
		outp->daddr = daddr;
		cachepage(outp, &swapimage);
		*pg = (Page*)(daddr|PG_ONSWAP);

		/* Add page to IO transaction list */
		iolist[ioptr++] = outp;
		break;
	}
}

void
pagersummary(void)
{
	print("%lud/%lud memory %lud/%lud swap %d iolist\n",
		palloc.user-palloc.freecount,
		palloc.user, conf.nswap-swapalloc.free, conf.nswap,
		ioptr);
}

static void
executeio(void)
{
	Page *outp;
	int i, n;
	Chan *c;
	char *kaddr;
	KMap *k;

	c = swapimage.c;
	for(i = 0; i < ioptr; i++) {
		if(ioptr > conf.nswppo)
			panic("executeio: ioptr %d > %d", ioptr, conf.nswppo);
		outp = iolist[i];

		assert(outp->ref > 0);
		assert(outp->image == &swapimage);
		assert(outp->daddr != ~0);

		/* only write when swap address still in use */
		if(swapcount(outp->daddr) > 1){
			k = kmap(outp);
			kaddr = (char*)VA(k);

			if(waserror())
				panic("executeio: page outp I/O error");

			n = devtab[c->type]->write(c, kaddr, BY2PG, outp->daddr);
			if(n != BY2PG)
				nexterror();

			kunmap(k);
			poperror();
		}

		/* drop our extra swap reference */
		putswap((Page*)outp->daddr);

		/* Free up the page after I/O */
		putpage(outp);
	}
	ioptr = 0;
}

int
needpages(void*)
{
	return palloc.freecount < swapalloc.headroom;
}

void
setswapchan(Chan *c)
{
	uchar dirbuf[sizeof(Dir)+100];
	Dir d;
	int n;

	if(waserror()){
		cclose(c);
		nexterror();
	}
	if(swapimage.c != nil) {
		if(swapalloc.free != conf.nswap)
			error(Einuse);
		cclose(swapimage.c);
		swapimage.c = nil;
	}

	/*
	 *  if this isn't a file, set the swap space
	 *  to be at most the size of the partition
	 */
	if(devtab[c->type]->dc != L'M'){
		n = devtab[c->type]->stat(c, dirbuf, sizeof dirbuf);
		if(n <= 0 || convM2D(dirbuf, n, &d, nil) == 0)
			error("stat failed in setswapchan");
		if(d.length < conf.nswppo*BY2PG)
			error("swap device too small");
		if(d.length < conf.nswap*BY2PG){
			conf.nswap = d.length/BY2PG;
			swapalloc.top = &swapalloc.swmap[conf.nswap];
			swapalloc.free = conf.nswap;
		}
	}
	c->flag &= ~CCACHE;
	cclunk(c);
	swapimage.c = c;
	poperror();
}
