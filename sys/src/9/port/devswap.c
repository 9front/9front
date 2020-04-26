#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	<libsec.h>
#include	<pool.h>

static int	canflush(Proc*, Segment*);
static void	executeio(void);
static void	pageout(Proc*, Segment*);
static void	pagepte(int, Page**);
static void	pager(void*);

Image 	swapimage = {
	.notext = 1,
};

static Chan	*swapchan;
static uchar	*swapbuf;
static AESstate *swapkey;

static Page	**iolist;
static ulong	ioptr;

static ushort	ageclock;

static void
swapinit(void)
{
	while(conf.nswap && conf.nswppo){
		swapalloc.swmap = xalloc(conf.nswap);
		if(swapalloc.swmap == nil)
			break;
		iolist = xalloc(conf.nswppo*sizeof(Page*));
		if(iolist == nil){
			xfree(swapalloc.swmap);
			swapalloc.swmap = nil;
		}
		break;
	}

	if(swapalloc.swmap == nil || iolist == nil)
		conf.nswap = conf.nswppo = 0;

	swapalloc.top = &swapalloc.swmap[conf.nswap];
	swapalloc.alloc = swapalloc.swmap;
	swapalloc.last = swapalloc.swmap;
	swapalloc.free = conf.nswap;
	swapalloc.xref = 0;

	kproc("pager", pager, 0);
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
	wakeup(&swapalloc.r);
}

static int
reclaim(void)
{
	ulong np;

	for(;;){
		if((np = pagereclaim(&fscache) + imagereclaim(0)) > 0){
			if(0) print("reclaim: %lud fscache + inactive image\n", np);
		} else if((np = pagereclaim(&swapimage)) > 0) {
			if(0) print("reclaim: %lud swap\n", np);
		} else if((np = imagereclaim(1)) > 0) {
			if(0) print("reclaim: %lud active image\n", np);
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
	Proc *p;
	Segment *s;
	int x, i;

	while(waserror())
		;

	x = -1;
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
			if(++x >= conf.nproc){
				if(++ageclock == i)
					goto Killbig;
				x = 0;
			}
			p = proctab(x);
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

		if(ioptr) {
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

	if(!canflush(p, s)	/* Able to invalidate all tlbs with references */
	|| waserror()) {
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
	int x, i;

	if(incref(s) == 2)		/* Easy if we are the only user */
		return canpage(p);

	/*
	 * Now we must do hardwork to ensure all processes which have tlb
	 * entries for this segment will be flushed if we succeed in paging it out
	 */
	for(x = 0; x < conf.nproc; x++){
		p = proctab(x);
		if(p->state == Dead)
			continue;
		for(i = 0; i < NSEG; i++){
			if(p->seg[i] == s)
				if(!canpage(p))
					return 0;
		}
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

static void
executeio(void)
{
	Page *outp;
	ulong i, j;

	for(i = j = 0; i < ioptr; i++) {
		outp = iolist[i];

		assert(outp->ref > 0);
		assert(outp->image == &swapimage);
		assert(outp->daddr != ~0);

		/* only write when swap address still in use */
		if(swapcount(outp->daddr) > 1){
			Chan *c = swapimage.c;
			KMap *k = kmap(outp);
			if(waserror()){
				kunmap(k);
				iolist[j++] = outp;
				continue;
			}
			if(devtab[c->type]->write(c, (char*)VA(k), BY2PG, outp->daddr) != BY2PG)
				error(Eshort);
			kunmap(k);
			poperror();
		}

		/* drop our extra swap reference */
		putswap((Page*)outp->daddr);

		/* Free up the page after I/O */
		putpage(outp);
	}
	ioptr = j;
	if(j) print("executeio (%lud/%lud): %s\n", j, i, up->errstr);
}

int
needpages(void*)
{
	return palloc.freecount < swapalloc.headroom;
}

static void
setswapchan(Chan *c)
{
	uchar buf[sizeof(Dir)+100];
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
		n = devtab[c->type]->stat(c, buf, sizeof buf);
		if(n <= 0 || convM2D(buf, n, &d, nil) == 0)
			error("stat failed in setswapchan");
		if(d.length < (vlong)conf.nswppo*BY2PG)
			error("swap device too small");
		if(d.length < (vlong)conf.nswap*BY2PG){
			conf.nswap = d.length/BY2PG;
			swapalloc.top = &swapalloc.swmap[conf.nswap];
			swapalloc.free = conf.nswap;
		}
	}
	c->flag &= ~CCACHE;
	cclunk(c);
	poperror();

	swapchan = c;
	swapimage.c = namec("#¶/swapfile", Aopen, ORDWR, 0);
}

enum {
	Qdir,
	Qswap,
	Qswapfile,
};

static Dirtab swapdir[]={
	".",		{Qdir, 0, QTDIR},	0,		DMDIR|0555,
	"swap",		{Qswap},		0,		0664,
	"swapfile",	{Qswapfile},		0,		0600,
};

static Chan*
swapattach(char *spec)
{
	return devattach(L'¶', spec);
}

static Walkqid*
swapwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, swapdir, nelem(swapdir), devgen);
}

static int
swapstat(Chan *c, uchar *dp, int n)
{
	return devstat(c, dp, n, swapdir, nelem(swapdir), devgen);
}

static Chan*
swapopen(Chan *c, int omode)
{
	uchar key[128/8];

	switch((ulong)c->qid.path){
	case Qswapfile:
		if(!iseve() || omode != ORDWR)
			error(Eperm);
		if(swapimage.c != nil)
			error(Einuse);
		if(swapchan == nil)
			error(Egreg);

		c->mode = openmode(omode);
		c->flag |= COPEN;
		c->offset = 0;

		swapbuf = mallocalign(BY2PG, BY2PG, 0, 0);
		swapkey = secalloc(sizeof(AESstate)*2);
		if(swapbuf == nil || swapkey == nil)
			error(Enomem);

		genrandom(key, sizeof(key));
		setupAESstate(&swapkey[0], key, sizeof(key), nil);
		genrandom(key, sizeof(key));
		setupAESstate(&swapkey[1], key, sizeof(key), nil);
		memset(key, 0, sizeof(key));

		return c;
	}
	return devopen(c, omode, swapdir, nelem(swapdir), devgen);
}

static void
swapclose(Chan *c)
{
	if((c->flag & COPEN) == 0)
		return;
	switch((ulong)c->qid.path){
	case Qswapfile:
		cclose(swapchan);
		swapchan = nil;
		secfree(swapkey);
		swapkey = nil;
		free(swapbuf);
		swapbuf = nil;
		break;
	}
}

static long
swapread(Chan *c, void *va, long n, vlong off)
{
	char tmp[256];		/* must be >= 18*NUMSIZE (Qswap) */
	ulong reclaim;

	switch((ulong)c->qid.path){
	case Qdir:
		return devdirread(c, va, n, swapdir, nelem(swapdir), devgen);
	case Qswap:
		reclaim = imagecached() + fscache.pgref + swapimage.pgref;
		snprint(tmp, sizeof tmp,
			"%llud memory\n"
			"%llud pagesize\n"
			"%lud kernel\n"
			"%lud/%lud user\n"
			"%lud/%lud swap\n"
			"%lud/%lud reclaim\n"
			"%llud/%llud/%llud kernel malloc\n"
			"%llud/%llud/%llud kernel draw\n"
			"%llud/%llud/%llud kernel secret\n",
			(uvlong)conf.npage*BY2PG,
			(uvlong)BY2PG,
			conf.npage-conf.upages,
			palloc.user-palloc.freecount-reclaim, palloc.user,
			conf.nswap-swapalloc.free, conf.nswap,
			reclaim, palloc.user,
			(uvlong)mainmem->curalloc,
			(uvlong)mainmem->cursize,
			(uvlong)mainmem->maxsize,
			(uvlong)imagmem->curalloc,
			(uvlong)imagmem->cursize,
			(uvlong)imagmem->maxsize,
			(uvlong)secrmem->curalloc,
			(uvlong)secrmem->cursize,
			(uvlong)secrmem->maxsize);
		return readstr((ulong)off, va, n, tmp);
	case Qswapfile:
		if(n != BY2PG)
			error(Ebadarg);
		if(devtab[swapchan->type]->read(swapchan, va, n, off) != n)
			error(Eio);
		aes_xts_decrypt(&swapkey[0], &swapkey[1], off, va, va, n);
		return n;
	}
	error(Egreg);
	return 0;
}

static long
swapwrite(Chan *c, void *va, long n, vlong off)
{
	char buf[256];
	
	switch((ulong)c->qid.path){
	case Qswap:
		if(!iseve())
			error(Eperm);
		if(n >= sizeof buf)
			error(Egreg);
		memmove(buf, va, n);	/* so we can NUL-terminate */
		buf[n] = 0;
		/* start a pager if not already started */
		if(strncmp(buf, "start", 5) == 0)
			kickpager();
		else if(buf[0]>='0' && buf[0]<='9')
			setswapchan(fdtochan(strtoul(buf, nil, 0), ORDWR, 1, 1));
		else
			error(Ebadctl);
		return n;
	case Qswapfile:
		if(n != BY2PG)
			error(Ebadarg);
		aes_xts_encrypt(&swapkey[0], &swapkey[1], off, va, swapbuf, n);
		if(devtab[swapchan->type]->write(swapchan, swapbuf, n, off) != n)
			error(Eio);
		return n;
	}
	error(Egreg);
	return 0;
}

Dev swapdevtab = {
	L'¶',
	"swap",
	devreset,
	swapinit,
	devshutdown,
	swapattach,
	swapwalk,
	swapstat,
	swapopen,
	devcreate,
	swapclose,
	swapread,
	devbread,
	swapwrite,
	devbwrite,
	devremove,
	devwstat,
};
