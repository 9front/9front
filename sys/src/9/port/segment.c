#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

/*
 * Attachable segment types
 */
static Physseg physseg[10] = {
	{ SG_SHARED,	"shared",	0,	SEGMAXSIZE	},
	{ SG_BSS,	"memory",	0,	SEGMAXSIZE	},
	{ 0,		0,		0,	0		},
};

static Lock physseglock;

#define IHASHSIZE	64
#define ihash(s)	imagealloc.hash[s%IHASHSIZE]
static struct Imagealloc
{
	Lock;

	QLock	ireclaim;	/* mutex on reclaiming idle images */

	ulong	pgidle;		/* pages in idle list (reclaimable) */

	ulong	nidle;
	Image	*idle;
	Image	*hash[IHASHSIZE];

}imagealloc;

Segment* (*_globalsegattach)(char*);

void
initseg(void)
{
}

Segment *
newseg(int type, uintptr base, ulong size)
{
	Segment *s;
	int mapsize;

	assert((base & (BY2PG-1)) == 0);

	if(size > (SEGMAPSIZE*PTEPERTAB))
		error(Enovmem);

	s = malloc(sizeof(Segment));
	if(s == nil)
		error(Enomem);
	s->ref = 1;
	s->type = type;
	s->size = size;
	s->base = base;
	s->top = base+((uintptr)size*BY2PG);
	s->used = s->swapped = 0;
	s->sema.prev = &s->sema;
	s->sema.next = &s->sema;

	if((type & SG_TYPE) == SG_PHYSICAL){
		s->map = nil;
		s->mapsize = 0;
		return s;
	}

	mapsize = ROUND(size, PTEPERTAB)/PTEPERTAB;
	if(mapsize > nelem(s->ssegmap)){
		s->map = malloc(mapsize*sizeof(Pte*));
		if(s->map == nil){
			free(s);
			error(Enomem);
		}
		s->mapsize = mapsize;
	}
	else{
		s->map = s->ssegmap;
		s->mapsize = nelem(s->ssegmap);
	}

	return s;
}

void
putseg(Segment *s)
{
	Image *i;

	if(s == nil)
		return;

	i = s->image;
	if(i != nil) {
		/*
		 *  We must hold image lock here during
		 *  decref() to prevent someone from taking
		 *  a reference to our segment from the cache.
		 *  Just letting decref(s) drop to zero *before*
		 *  and then checking s->ref again under image
		 *  lock is not sufficient, as someone can grab
		 *  a reference and then call putseg() again;
		 *  freeing segment. By the time we hold image lock,
		 *  the segment would be freed from under us.
		 */
		lock(i);
		if(decref(s) != 0){
			unlock(i);
			return;
		}
		if(i->s == s)
			i->s = nil;
		putimage(i);
	} else if(decref(s) != 0)
		return;

	assert(s->sema.prev == &s->sema);
	assert(s->sema.next == &s->sema);

	if(s->mapsize > 0){
		int i;
		Pte *pte;
		Page *entry, *fh, *ft;
		ulong np;

		np = 0;
		fh = ft = nil;

		for(i = 0; i < s->mapsize; i++){
			if((pte = s->map[i]) == nil)
				continue;
			while(pte->first <= pte->last){
				entry = *(pte->first++);
				if(entry == nil)
					continue;
				if(onswap(entry)){
					putswap(entry);
					continue;
				}
				entry = deadpage(entry);
				if(entry == nil)
					continue;
				if(fh != nil)
					ft->next = entry;
				else
					fh = entry;
				ft = entry;
				np++;
			}
			free(pte);
		}

		freepages(fh, ft, np);

		if(s->map != s->ssegmap)
			free(s->map);
	}

	if(s->profile != nil)
		free(s->profile);

	free(s);
}

static Pte*
ptealloc(void)
{
	Pte *new;

	new = malloc(sizeof(Pte));
	if(new != nil){
		new->first = &new->pages[PTEPERTAB];
		new->last = new->pages;
	}
	return new;
}

static Pte*
ptecpy(Pte *new, Pte *old)
{
	Page **src, **dst, *entry;

	dst = &new->pages[old->first-old->pages];
	new->first = dst;
	for(src = old->first; src <= old->last; src++, dst++){
		if((entry = *src) == nil)
			continue;
		if(onswap(entry))
			dupswap(entry);
		else
			incref(entry);
		new->last = dst;
		*dst = entry;
	}
	return new;
}

Segment*
dupseg(Segment **seg, int segno, int share)
{
	int i;
	Pte *pte;
	Segment *n, *s;

	s = seg[segno];
	qlock(s);
	if(waserror()){
		qunlock(s);
		nexterror();
	}
	switch(s->type&SG_TYPE) {
	case SG_TEXT:		/* New segment shares pte set */
	case SG_SHARED:
	case SG_PHYSICAL:
	case SG_FIXED:
	case SG_STICKY:
	default:
	sameseg:
		incref(s);
		qunlock(s);
		poperror();
		return s;

	case SG_STACK:
		n = newseg(s->type, s->base, s->size);
		break;

	case SG_BSS:		/* Just copy on write */
		if(share)
			goto sameseg;
		n = newseg(s->type, s->base, s->size);
		break;

	case SG_DATA:		/* Copy on write plus demand load info */
		if(segno == TSEG){
			n = data2txt(s);
			qunlock(s);
			poperror();
			return n;
		}
		if(share)
			goto sameseg;
		n = newseg(s->type, s->base, s->size);
		n->image = s->image;
		n->fstart = s->fstart;
		n->flen = s->flen;
		incref(s->image);
		break;
	}
	for(i = 0; i < s->mapsize; i++){
		if(s->map[i] != nil){
			pte = ptealloc();
			if(pte == nil){
				qunlock(s);
				poperror();
				putseg(n);
				error(Enomem);
			}
			n->map[i] = ptecpy(pte, s->map[i]);
		}
	}
	n->used = s->used;
	n->swapped = s->swapped;
	n->flushme = s->flushme;
	if(s->ref > 1)
		procflushseg(s);
	qunlock(s);
	poperror();
	return n;
}

/* Must be called with s locked */
Page*
segpeek(Segment *s, uintptr addr)
{
	Pte *pte;
	uintptr soff;

	assert(addr < s->top);
	assert(addr >= s->base);

	soff = addr - s->base;
	pte = s->map[soff/PTEMAPMEM];
	if(pte == nil)
		return nil;
	return pte->pages[(soff&(PTEMAPMEM-1))/BY2PG];
}

/* Must be called with s locked */
Page**
segmap(Segment *s, uintptr addr)
{
	Pte *pte;
	Page **pg;
	uintptr soff;

	assert(addr < s->top);
	assert(addr >= s->base);

	soff = addr - s->base;
	pte = s->map[soff/PTEMAPMEM];
	if(pte == nil) {
		if((pte = ptealloc()) == nil)
			return nil;
		s->map[soff/PTEMAPMEM] = pte;
	}

	pg = &pte->pages[(soff&(PTEMAPMEM-1))/BY2PG];
	if(pg < pte->first)
		pte->first = pg;
	if(pg > pte->last)
		pte->last = pg;

	return pg;
}

/*
 *  Insert new Page into Segmnet s.
 *  On error, calls putpage(new).
 */
void
segpage(Segment *s, Page *new)
{
	Page **pg;

	qlock(s);
	if((pg = segmap(s, new->va)) == nil) {
		qunlock(s);
		putpage(new);
		error(Enomem);
	}
	settxtflush(new, s->flushme);
	assert(*pg == nil);
	*pg = new;
	s->used++;
	qunlock(s);
}

void
relocateseg(Segment *s, uintptr base)
{
	int i;
	Pte *pte;
	Page **pg;
	uintptr offset;

	assert((base & (BY2PG-1)) == 0);

	qlock(s);
	offset = base - s->base;
	for(i = 0; i < s->mapsize; i++) {
		if((pte = s->map[i]) == nil)
			continue;
		for(pg = pte->first; pg <= pte->last; pg++) {
			if(!pagedout(*pg))
				(*pg)->va += offset;
		}
	}
	s->base += offset;
	s->top += offset;
	qunlock(s);
}

Image*
newimage(ulong pages)
{
	ulong pghsize;
	Image *i;

	/* make power of two */
	pghsize = pages-1;
	pghsize |= pghsize >> 16;
	pghsize |= pghsize >> 8;
	pghsize |= pghsize >> 4;
	pghsize |= pghsize >> 2;
	pghsize |= pghsize >> 1;
	pghsize++;

	if(pghsize > 1024)
		pghsize >>= 4;

	i = malloc(sizeof(Image) + pghsize * sizeof(Page*));
	if(i == nil)
		return nil;

	i->ref = 1;
	i->pghsize = pghsize;

	return i;
}

/* remove from idle list */
static void
busyimage(Image *i)
{
	/* not on idle list? */
	if(i->link == nil)
		return;

	if((*i->link = i->next) != nil)
		i->next->link  = i->link;
	i->link = nil;
	i->next = nil;
	imagealloc.pgidle -= i->pgref;
	imagealloc.nidle--;
}

Image*
attachimage(Chan *c, ulong pages)
{
	Image *i, **l;

retry:
	lock(&imagealloc);

	/*
	 * Search the image cache for remains of the text from a previous
	 * or currently running incarnation
	 */
	for(i = ihash(c->qid.path); i != nil; i = i->hash){
		if(eqchantdqid(c, i->type, i->dev, i->qid, 0)){
			incref(i);
			busyimage(i);
			goto found;
		}
	}
	if(imagealloc.nidle > conf.nimage
	|| (i = newimage(pages)) == nil) {
		unlock(&imagealloc);
		if(imagealloc.nidle == 0)
			error(Enomem);
		if(imagereclaim(0) == 0)
			freebroken();		/* can use the memory */
		goto retry;
	}
	i->type = c->type;
	i->dev = c->dev;
	i->qid = c->qid;
	l = &ihash(c->qid.path);
	i->hash = *l;
	*l = i;
found:
	i->nattach++;
	unlock(&imagealloc);
	lock(i);
	if(i->c == nil){
		i->c = c;
		incref(c);
	}
	return i;
}

/* insert into idle list */
static void
idleimage(Image *i)
{
	Image **l, *j;

	/* already on idle list? */
	if(i->link != nil)
		return;

	l = &imagealloc.idle;
	j = imagealloc.idle;
	/* sort by least frequenty and most pages used first */
	for(; j != nil; l = &j->next, j = j->next){
		long c = j->nattach - i->nattach;
		if(c < 0)
			continue;
		if(c > 0)
			break;
		if(j->pgref < i->pgref)
			break;
	}
	if((i->next = j) != nil)
		j->link = &i->next;
	*(i->link = l) = i;
	imagealloc.pgidle += i->pgref;
	imagealloc.nidle++;
}

/* putimage(): called with image locked and unlocks */
void
putimage(Image *i)
{
	Chan *c;
	long r;

	r = decref(i);
	if(i->notext){
		unlock(i);
		return;
	}
	c = nil;
	if(r == 0){
		assert(i->pgref == 0);
		assert(i->s == nil);
		lock(&imagealloc);
		r = i->ref;
		if(r == 0){
			Image *f, **l;

			c = i->c;
			i->c = nil;
			busyimage(i);

			l = &ihash(i->qid.path);
			for(f = *l; f != nil; f = f->hash) {
				if(f == i) {
					*l = i->hash;
					break;
				}
				l = &f->hash;
			}
		}
		unlock(&imagealloc);
	} else if(r == i->pgref) {
		assert(i->pgref > 0);
		assert(i->s == nil);
		if(i->link == nil){
			c = i->c;
			i->c = nil;

			lock(&imagealloc);
			idleimage(i);
			unlock(&imagealloc);
		}
	}
	unlock(i);

	if(r == 0)
		free(i);

	if(c != nil)
		ccloseq(c);	/* does not block */
}

ulong
imagecached(void)
{
	return imagealloc.pgidle;
}

ulong
imagereclaim(ulong pages)
{
	ulong np;
	Image *i;

	eqlock(&imagealloc.ireclaim);
	
	lock(&imagealloc);
	np = 0;
	while(np < pages || imagealloc.nidle > conf.nimage) {
		i = imagealloc.idle;
		if(i == nil)
			break;
		incref(i);
		busyimage(i);
		unlock(&imagealloc);

		np += pagereclaim(i);

		lock(i);
		putimage(i);

		lock(&imagealloc);
	}
	unlock(&imagealloc);

	qunlock(&imagealloc.ireclaim);

	return np;
}

/*
 *  Must be called with s locked.
 *
 *  This relies on s->ref > 1 indicating that
 *  the segment is shared with other processes
 *  different from the calling one.
 *
 *  The calling process (up) is responsible for
 *  flushing its own TBL by calling flushmmu()
 *  afterwards when returning a non-zero value
 */
static int
segfreemap(Segment *s, uintptr from, uintptr to)
{
	uintptr soff;
	int poff, i;
	Pte *pte;
	Page **pg, *entry, *fh, *ft;
	ulong np;
	int flush;

	to &= ~(BY2PG-1);
	from = PGROUND(from);
	if(from >= to)
		return 0;

	fh = ft = nil;
	np = 0;
	flush = 0;

	soff = from - s->base;
	poff = (soff & (PTEMAPMEM-1))/BY2PG;
	for(i = soff/PTEMAPMEM; i < s->mapsize; i++, poff = 0) {
		if((pte = s->map[i]) == nil) {
			from = (from | (PTEMAPMEM-1))+1;
			if(from >= to)
				goto done;
			continue;
		}

		for(pg = &pte->pages[poff]; pg < &pte->pages[PTEPERTAB]; pg++) {
			if(pg == pte->first)
				pte->first++;
			if((entry = *pg) != nil) {
				*pg = nil;
				if(onswap(entry)) {
					putswap(entry);
					s->swapped--;
				} else {
					if((entry = deadpage(entry)) != nil) {
						if(fh != nil)
							ft->next = entry;
						else
							fh = entry;
						ft = entry;
						np++;
					}
					flush = 1;
				}
				s->used--;
			}
			from += BY2PG;
			if(from >= to)
				goto done;
		}

		if(poff == 0) {
			s->map[i] = nil;
			free(pte);
		}
	}
done:
	/* skip TLB flush when no pages have been changed */
	if(flush == 0)
		return 0;

	/*
	 * we have to make sure other processors flush the
	 * entries from their TLBs before any pages are freed.
	 */
	if(s->ref > 1)
		procflushseg(s);

	freepages(fh, ft, np);

	/* tell caller it should flush its TLB */
	return 1;
}

uintptr
ibrk(uintptr addr, int seg)
{
	Segment *s, *ns;
	uintptr newtop;
	ulong newsize;
	int i, mapsize;
	Pte **map;

	s = up->seg[seg];
	if(s == nil)
		error(Ebadarg);

	if(addr == 0)
		return s->base;

	qlock(s);

	/* We may start with the bss overlapping the data */
	if(addr < s->base) {
		if(seg != BSEG || up->seg[DSEG] == nil || addr < up->seg[DSEG]->base) {
			qunlock(s);
			error(Enovmem);
		}
		addr = s->base;
	}

	newtop = PGROUND(addr);
	newsize = (newtop-s->base)/BY2PG;
	if(newtop < s->top) {
		/*
		 * do not shrink a segment shared with other procs, as the
		 * to-be-freed address space may have been passed to the kernel
		 * already by another proc and is past the validaddr stage.
		 */
		if(s->ref > 1){
			qunlock(s);
			error(Einuse);
		}
		if(!segfreemap(s, newtop, s->top))
			goto done;	/* skip flushmmu() */
		s->top = newtop;
		s->size = newsize;
		qunlock(s);
		flushmmu();
		return 0;
	}

	for(i = 0; i < NSEG; i++) {
		ns = up->seg[i];
		if(ns == nil || ns == s)
			continue;
		if(newtop > ns->base && s->base < ns->top) {
			qunlock(s);
			error(Esoverlap);
		}
	}

	if(newsize > (SEGMAPSIZE*PTEPERTAB)) {
		qunlock(s);
		error(Enovmem);
	}
	mapsize = ROUND(newsize, PTEPERTAB)/PTEPERTAB;
	if(mapsize > s->mapsize) {
		map = malloc(mapsize*sizeof(Pte*));
		if(map == nil){
			qunlock(s);
			error(Enomem);
		}
		memmove(map, s->map, s->mapsize*sizeof(Pte*));
		if(s->map != s->ssegmap)
			free(s->map);
		s->map = map;
		s->mapsize = mapsize;
	}
done:
	s->top = newtop;
	s->size = newsize;
	qunlock(s);
	return 0;
}

Segment*
isoverlap(uintptr va, uintptr len)
{
	int i;
	Segment *ns;
	uintptr newtop;

	newtop = va+len;
	for(i = 0; i < NSEG; i++) {
		ns = up->seg[i];
		if(ns == nil)
			continue;
		if(newtop > ns->base && va < ns->top)
			return ns;
	}
	return nil;
}

Physseg*
addphysseg(Physseg* new)
{
	Physseg *ps;

	/*
	 * Check not already entered and there is room
	 * for a new entry and the terminating null entry.
	 */
	lock(&physseglock);
	for(ps = physseg; ps->name; ps++){
		if(strcmp(ps->name, new->name) == 0){
			unlock(&physseglock);
			return nil;
		}
	}
	if(ps-physseg >= nelem(physseg)-2){
		unlock(&physseglock);
		return nil;
	}
	*ps = *new;
	unlock(&physseglock);

	return ps;
}

Physseg*
findphysseg(char *name)
{
	Physseg *ps;

	for(ps = physseg; ps->name; ps++)
		if(strcmp(ps->name, name) == 0)
			return ps;

	return nil;
}

uintptr
segattach(int attr, char *name, uintptr va, uintptr len)
{
	int sno;
	Segment *s, *os;
	Physseg *ps;

	if(va != 0 && va >= USTKTOP)
		error(Ebadarg);

	qlock(&up->seglock);
	if(waserror()){
		qunlock(&up->seglock);
		nexterror();
	}
		
	for(sno = 0; sno < NSEG; sno++)
		if(up->seg[sno] == nil && sno != ESEG)
			break;

	if(sno == NSEG)
		error(Enovmem);

	/*
	 *  first look for a global segment with the
	 *  same name
	 */
	if(_globalsegattach != nil){
		s = (*_globalsegattach)(name);
		if(s != nil){
			va = s->base;
			len = s->top - va;
			if(isoverlap(va, len) != nil){
				putseg(s);
				error(Esoverlap);
			}
			up->seg[sno] = s;
			goto done;
		}
	}

	/* round up va+len */
	len += va & (BY2PG-1);
	len = PGROUND(len);

	if(len == 0)
		error(Ebadarg);

	/*
	 * Find a hole in the address space.
	 * Starting at the lowest possible stack address - len,
	 * check for an overlapping segment, and repeat at the
	 * base of that segment - len until either a hole is found
	 * or the address space is exhausted.  Ensure that we don't
	 * map the zero page.
	 */
	if(va == 0) {
		for (os = up->seg[SSEG]; os != nil; os = isoverlap(va, len)) {
			va = os->base;
			if(len >= va)
				error(Enovmem);
			va -= len;
		}
	}

	va &= ~(BY2PG-1);
	if(va == 0 || (va+len) > USTKTOP || (va+len) < va)
		error(Ebadarg);

	if(isoverlap(va, len) != nil)
		error(Esoverlap);

	ps = findphysseg(name);
	if(ps == nil)
		error(Ebadarg);

	if(len > ps->size)
		error(Enovmem);

	/* Turn off what is not allowed */
	attr &= ~(SG_TYPE | SG_CACHED | SG_DEVICE);

	/* Copy in defaults */
	attr |= ps->attr;

	s = newseg(attr, va, len/BY2PG);
	s->pseg = ps;
	up->seg[sno] = s;
done:
	qunlock(&up->seglock);
	poperror();

	return va;
}

static int
segflush1(Segment *s, uintptr from, uintptr to)
{
	if((s->type & SG_NOEXEC) != 0)
		return 0;

	if((s->type & SG_RONLY) != 0 && s->flushme)
		return 0;

	s->flushme = 1;
	if(s->ref > 1)
		procflushseg(s);

	if(s->mapsize == 0)
		return 1;

	from &= ~(BY2PG);
	to = PGROUND(to);
	while(from < to){
		Page *p = segpeek(s, from);
		if(!pagedout(p)){
			settxtflush(p, 1);
		}
		from += BY2PG;
	}

	return 1;
}

static int
segfree1(Segment *s, uintptr from, uintptr to)
{
	uintptr afrom, ato;
	int fill;
	Page *p;
	KMap *k;

	if((s->type & SG_RONLY) != 0 || s->mapsize == 0)
		return 0;

	fill = 0;
	switch(s->type&SG_TYPE){
	case SG_FIXED:
	case SG_STICKY:
		qunlock(s);
		if(!waserror()){
			memset((uchar*)from, fill, to-from);
			poperror();
		}
		qlock(s);
		/* wet floor */
	case SG_PHYSICAL:
	case SG_TEXT:
		return 0;

	case SG_STACK:
		fill = 0xfe;
		/* wet floor */
	default:
	case SG_BSS:
	case SG_SHARED:
		ato = to & ~(BY2PG-1);
		afrom = PGROUND(from);

		/* fill partial covered pages with pattern */
		if(afrom > to)
			afrom = ato = to;
		if(from < afrom && segpeek(s, from) != nil){
			qunlock(s);
			if(!waserror()){
				memset((uchar*)from, fill, afrom-from);
				poperror();
			}
			qlock(s);
		}
		if(ato < to && ato >= from && segpeek(s, ato) != nil){
			qunlock(s);
			if(!waserror()){
				memset((uchar*)ato, fill, to-ato);
				poperror();
			}
			qlock(s);
		}
		return segfreemap(s, afrom, ato);

	case SG_DATA:
		ato = to & ~(BY2PG-1);
		afrom = PGROUND(from);

		/* reset partially covered pages to original image data. */
		if(afrom > to)
			afrom = ato = to;
		if(from < afrom && segpeek(s, from) != nil
		&& (p = lookpage(s->image, s->fstart + (from & ~(BY2PG-1)) - s->base)) != nil){
			qunlock(s);
			k = kmap(p);
			if(!waserror()){
				memmove((uchar*)from, (uchar*)VA(k) + (from & (BY2PG-1)), afrom-from);
				poperror();
			}
			kunmap(k);
			putpage(p);
			qlock(s);
		}
		if(ato < to && ato >= from && segpeek(s, ato) != nil
		&& (p = lookpage(s->image, s->fstart + ato - s->base)) != nil){
			qunlock(s);
			k = kmap(p);
			if(!waserror()){
				memmove((uchar*)ato, (uchar*)VA(k), to-ato);
				poperror();
			}
			kunmap(k);
			putpage(p);
			qlock(s);
		}
		return segfreemap(s, afrom, ato);
	}
}

static int
withsegrange(void *va, uintptr len, int (*fun)(Segment*, uintptr, uintptr))
{
	uintptr from, to, top;
	Segment *s;
	int flush;

	from = (uintptr)va;
	to = from + len;
	if(to < from || to > USTKTOP)
		error(Ebadarg);

	flush = 0;
	while(from < to){
		s = seg(up, from, 1);
		if(s == nil){
			if(flush)
				flushmmu();
			error(Ebadarg);
		}
		top = s->top;
		if(to <= top){
			flush |= (*fun)(s, from, to);
			qunlock(s);
			break;
		}
		flush |= (*fun)(s, from, top);
		qunlock(s);
		from = top;
	}

	if(flush)
		flushmmu();

	return 0;
}

int
segflush(void *va, uintptr len)
{
	return withsegrange(va, len, segflush1);
}

int
segfree(void *va, uintptr len)
{
	return withsegrange(va, len, segfree1);
}

void
segclock(uintptr pc)
{
	Segment *s;

	s = up->seg[TSEG];
	if(s == nil || s->profile == nil)
		return;
	s->profile[0] += TK2MS(1);
	if(pc >= s->base && pc < s->top) {
		pc -= s->base;
		s->profile[pc>>LRESPROF] += TK2MS(1);
	}
}

Segment*
txt2data(Segment *s)
{
	Segment *ps;

	ps = newseg(SG_DATA, s->base, s->size);
	ps->image = s->image;
	ps->fstart = s->fstart;
	ps->flen = s->flen;
	ps->flushme = 1;
	incref(s->image);
	return ps;
}

Segment*
data2txt(Segment *s)
{
	Segment *ps;
	Image *i;

	i = s->image;
	lock(i);
	if((ps = i->s) != nil && ps->flen == s->flen){
		assert(ps->image == i);
		incref(ps);	
		unlock(i);
		return ps;
	}
	if(waserror()){
		unlock(i);
		nexterror();
	}
	ps = newseg(SG_TEXT | SG_RONLY, s->base, s->size);
	ps->image = i;
	ps->fstart = s->fstart;
	ps->flen = s->flen;
	ps->flushme = 1;
	if(i->s == nil)
		i->s = ps;
	incref(i);
	unlock(i);
	poperror();
	return ps;
}

enum {
	/* commands to segmentioproc */
	Cnone=0,
	Cread,
	Cwrite,
	Cdie,
};

static int
cmddone(void *arg)
{
	Segio *sio = arg;

	return sio->cmd == Cnone;
}

static void
docmd(Segio *sio, int cmd)
{
	sio->err = nil;
	sio->cmd = cmd;
	while(waserror())
		;
	wakeup(&sio->cmdwait);
	sleep(&sio->replywait, cmddone, sio);
	poperror();
	if(sio->err != nil)
		error(sio->err);
}

static int
cmdready(void *arg)
{
	Segio *sio = arg;

	return sio->cmd != Cnone;
}

static void
segmentioproc(void *arg)
{
	Segio *sio = arg;
	int done;
	int sno;

	qlock(&up->seglock);
	for(sno = 0; sno < NSEG; sno++)
		if(up->seg[sno] == nil && sno != ESEG)
			break;
	if(sno == NSEG)
		panic("segmentkproc");
	sio->p = up;
	incref(sio->s);
	up->seg[sno] = sio->s;
	qunlock(&up->seglock);

	while(waserror())
		;
	for(done = 0; !done;){
		sleep(&sio->cmdwait, cmdready, sio);
		if(waserror())
			sio->err = up->errstr;
		else {
			if(sio->s != nil && up->seg[sno] != sio->s){
				Segment *tmp;
				qlock(&up->seglock);
				incref(sio->s);
				tmp = up->seg[sno];
				up->seg[sno] = sio->s;
				putseg(tmp);
				qunlock(&up->seglock);
				flushmmu();
			}
			switch(sio->cmd){
			case Cread:
				memmove(sio->data, sio->addr, sio->dlen);
				break;
			case Cwrite:
				memmove(sio->addr, sio->data, sio->dlen);
				if(sio->s->flushme)
					segflush(sio->addr, sio->dlen);
				break;
			case Cdie:
				done = 1;
				break;
			}
			poperror();
		}
		sio->cmd = Cnone;
		wakeup(&sio->replywait);
	}

	pexit("done", 1);
}

long
segio(Segio *sio, Segment *s, void *a, long n, vlong off, int read)
{
	uintptr m;
	void *b;

	b = a;
	if(s != nil){
		m = s->top - s->base;
		if(off < 0 || off >= m){
			if(!read)
				error(Ebadarg);
			return 0;
		}
		if(off+n > m){
			if(!read)
				error(Ebadarg);	
			n = m - off;
		}

		if((uintptr)a < KZERO) {
			b = smalloc(n);
			if(waserror()){
				free(b);
				nexterror();
			}
			if(!read)
				memmove(b, a, n);
		}
	}

	eqlock(sio);
	if(waserror()){
		qunlock(sio);
		nexterror();
	}
	sio->s = s;
	if(s == nil){
		if(sio->p != nil){
			docmd(sio, Cdie);
			sio->p = nil;
		}
		qunlock(sio);
		poperror();
		return 0;
	}
	if(sio->p == nil){
		sio->cmd = Cnone;
		kproc("segmentio", segmentioproc, sio);
	}
	sio->addr = (char*)s->base + off;
	sio->data = b;
	sio->dlen = n;
	docmd(sio, read ? Cread : Cwrite);
	qunlock(sio);
	poperror();

	if(a != b){
		if(read)
			memmove(a, b, n);
		free(b);
		poperror();
	}
	return n;
}
