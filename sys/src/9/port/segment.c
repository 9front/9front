#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

int imagereclaim(int);

/*
 * Attachable segment types
 */
static Physseg physseg[10] = {
	{ SG_SHARED,	"shared",	0,	SEGMAXSIZE,	0, 	0 },
	{ SG_BSS,	"memory",	0,	SEGMAXSIZE,	0,	0 },
	{ 0,		0,		0,	0,		0,	0 },
};

static Lock physseglock;

#define IHASHSIZE	64
#define ihash(s)	imagealloc.hash[s%IHASHSIZE]
static struct Imagealloc
{
	Lock;
	Image	*list;
	Image	*free;
	Image	*hash[IHASHSIZE];
	QLock	ireclaim;	/* mutex on reclaiming free images */
}imagealloc;

Segment* (*_globalsegattach)(Proc*, char*);

void
initseg(void)
{
	Image *i, *ie;

	imagealloc.list = xalloc(conf.nimage*sizeof(Image));
	if(imagealloc.list == nil)
		panic("initseg: no memory for Image");
	ie = &imagealloc.list[conf.nimage-1];
	for(i = imagealloc.list; i < ie; i++)
		i->next = i+1;
	i->next = nil;
	imagealloc.free = imagealloc.list;
}

Segment *
newseg(int type, uintptr base, ulong size)
{
	Segment *s;
	int mapsize;

	if(size > (SEGMAPSIZE*PTEPERTAB))
		error(Enovmem);

	s = malloc(sizeof(Segment));
	if(s == nil)
		error(Enomem);
	s->ref = 1;
	s->type = type;
	s->base = base;
	s->top = base+(size*BY2PG);
	s->size = size;
	s->sema.prev = &s->sema;
	s->sema.next = &s->sema;

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
	Pte **pp, **emap;
	Image *i;

	if(s == nil)
		return;

	i = s->image;
	if(i != nil) {
		lock(i);
		if(decref(s) != 0){
			unlock(i);
			return;
		}
		if(i->s == s)
			i->s = nil;
		unlock(i);
		putimage(i);
	} else if(decref(s) != 0)
		return;

	emap = &s->map[s->mapsize];
	for(pp = s->map; pp < emap; pp++)
		if(*pp != nil)
			freepte(s, *pp);

	if(s->map != s->ssegmap)
		free(s->map);
	if(s->profile != nil)
		free(s->profile);

	free(s);
}

void
relocateseg(Segment *s, uintptr offset)
{
	Page **pg, *x;
	Pte *pte, **p, **endpte;

	endpte = &s->map[s->mapsize];
	for(p = s->map; p < endpte; p++) {
		if((pte = *p) == nil)
			continue;
		for(pg = pte->first; pg <= pte->last; pg++) {
			if((x = *pg) != nil)
				x->va += offset;
		}
	}
}

Segment*
dupseg(Segment **seg, int segno, int share)
{
	int i, size;
	Pte *pte;
	Segment *n, *s;

	SET(n);
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
		goto sameseg;

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
			poperror();
			qunlock(s);
			return n;
		}

		if(share)
			goto sameseg;
		n = newseg(s->type, s->base, s->size);

		incref(s->image);
		n->image = s->image;
		n->fstart = s->fstart;
		n->flen = s->flen;
		break;
	}
	size = s->mapsize;
	for(i = 0; i < size; i++)
		if((pte = s->map[i]) != nil)
			n->map[i] = ptecpy(pte);

	n->flushme = s->flushme;
	if(s->ref > 1)
		procflushseg(s);
	poperror();
	qunlock(s);
	return n;

sameseg:
	incref(s);
	poperror();
	qunlock(s);
	return s;
}

void
segpage(Segment *s, Page *p)
{
	Pte **pte;
	uintptr off;
	Page **pg;

	if(p->va < s->base || p->va >= s->top)
		panic("segpage");

	off = p->va - s->base;
	pte = &s->map[off/PTEMAPMEM];
	if(*pte == nil)
		*pte = ptealloc();

	pg = &(*pte)->pages[(off&(PTEMAPMEM-1))/BY2PG];
	*pg = p;
	if(pg < (*pte)->first)
		(*pte)->first = pg;
	if(pg > (*pte)->last)
		(*pte)->last = pg;
}

Image*
attachimage(int type, Chan *c, uintptr base, ulong len)
{
	Image *i, **l;

	lock(&imagealloc);

	/*
	 * Search the image cache for remains of the text from a previous
	 * or currently running incarnation
	 */
	for(i = ihash(c->qid.path); i; i = i->hash) {
		if(c->qid.path == i->qid.path) {
			lock(i);
			if(eqchantdqid(c, i->type, i->dev, i->qid, 0) && c->qid.type == i->qid.type)
				goto found;
			unlock(i);
		}
	}

	/* dump pages of inactive images to free image structures */
	while((i = imagealloc.free) == nil) {
		unlock(&imagealloc);
		imagereclaim(1000);
		if(imagealloc.free == nil){
			freebroken();		/* can use the memory */
			resrcwait("no image after reclaim");
		}
		lock(&imagealloc);
	}

	imagealloc.free = i->next;

	lock(i);
	i->type = c->type;
	i->dev = c->dev;
	i->qid = c->qid;

	l = &ihash(c->qid.path);
	i->hash = *l;
	*l = i;

found:
	unlock(&imagealloc);
	if(i->c == nil){
		i->c = c;
		c->flag &= ~CCACHE;
		incref(c);
	}

	if(i->s == nil) {
		incref(i);
		if(waserror()) {
			unlock(i);
			putimage(i);
			nexterror();
		}
		i->s = newseg(type, base, len);
		i->s->image = i;
		poperror();
	}
	else
		incref(i->s);

	return i;
}

extern int pagereclaim(Image*, int);	/* page.c */

int
imagereclaim(int min)
{
	static Image *i, *ie;
	int j, n;

	eqlock(&imagealloc.ireclaim);
	if(i == nil){
		i = imagealloc.list;
		ie = &imagealloc.list[conf.nimage];
	}
	n = 0;
	for(j = 0; j < conf.nimage; j++, i++){
		if(i >= ie)
			i = imagealloc.list;
		if(i->ref == 0)
			continue;
		/*
		 * if there are no free image structures, only
		 * reclaim pages from inactive images.
		 */
		if(imagealloc.free != nil || i->ref == i->pgref){
			n += pagereclaim(i, min - n);
			if(n >= min)
				break;
		}
	}
	qunlock(&imagealloc.ireclaim);

	return n;
}

void
putimage(Image *i)
{
	Image *f, **l;
	Chan *c;
	int r;

	if(i->notext){
		decref(i);
		return;
	}

	c = nil;
	lock(i);
	r = decref(i);
	if(r == i->pgref){
		/*
		 * all remaining references to this image are from the
		 * page cache, so close the chan.
		 */
		c = i->c;
		i->c = nil;
	}
	if(r == 0){
		l = &ihash(i->qid.path);
		mkqid(&i->qid, ~0, ~0, QTFILE);
		unlock(i);

		lock(&imagealloc);
		for(f = *l; f != nil; f = f->hash) {
			if(f == i) {
				*l = i->hash;
				break;
			}
			l = &f->hash;
		}
		i->next = imagealloc.free;
		imagealloc.free = i;
		unlock(&imagealloc);
	} else
		unlock(i);
	if(c != nil)
		ccloseq(c);	/* does not block */
}

long
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
		mfreeseg(s, newtop, (s->top-newtop)/BY2PG);
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
		if(newtop >= ns->base && newtop < ns->top) {
			qunlock(s);
			error(Esoverlap);
		}
	}

	if(newsize > (SEGMAPSIZE*PTEPERTAB)) {
		qunlock(s);
		error(Enovmem);
	}
	mapsize = ROUND(newsize, PTEPERTAB)/PTEPERTAB;
	if(mapsize > s->mapsize){
		map = smalloc(mapsize*sizeof(Pte*));
		memmove(map, s->map, s->mapsize*sizeof(Pte*));
		if(s->map != s->ssegmap)
			free(s->map);
		s->map = map;
		s->mapsize = mapsize;
	}

	s->top = newtop;
	s->size = newsize;
	qunlock(s);
	return 0;
}

/*
 *  called with s locked
 */
int
mcountseg(Segment *s)
{
	int i, j, pages;
	Page *pg;

	pages = 0;
	for(i = 0; i < s->mapsize; i++){
		if(s->map[i] == nil)
			continue;
		for(j = 0; j < PTEPERTAB; j++){
			pg = s->map[i]->pages[j];
			if(!pagedout(pg))
				pages++;
		}
	}
	return pages;
}

/*
 *  called with s locked
 */
void
mfreeseg(Segment *s, uintptr start, int pages)
{
	int i, j, size;
	uintptr soff;
	Page *pg;

	/*
	 * We want to zero s->map[i]->page[j] and putpage(pg),
	 * but we have to make sure other processors flush the
	 * entry from their TLBs before the page is freed.
	 */
	if(s->ref > 1)
		procflushseg(s);

	soff = start-s->base;
	j = (soff&(PTEMAPMEM-1))/BY2PG;

	size = s->mapsize;
	for(i = soff/PTEMAPMEM; i < size; i++) {
		if(pages <= 0)
			return;
		if(s->map[i] == nil) {
			pages -= PTEPERTAB-j;
			j = 0;
			continue;
		}
		while(j < PTEPERTAB) {
			pg = s->map[i]->pages[j];
			if(pg != nil){
				s->map[i]->pages[j] = nil;
				putpage(pg);
			}
			if(--pages == 0)
				return;
			j++;
		}
		j = 0;
	}
}

Segment*
isoverlap(Proc *p, uintptr va, uintptr len)
{
	int i;
	Segment *ns;
	uintptr newtop;

	newtop = va+len;
	for(i = 0; i < NSEG; i++) {
		ns = p->seg[i];
		if(ns == nil)
			continue;
		if((newtop > ns->base && newtop <= ns->top) ||
		   (va >= ns->base && va < ns->top))
			return ns;
	}
	return nil;
}

int
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
			return -1;
		}
	}
	if(ps-physseg >= nelem(physseg)-2){
		unlock(&physseglock);
		return -1;
	}
	*ps = *new;
	unlock(&physseglock);

	return 0;
}

int
isphysseg(char *name)
{
	Physseg *ps;
	int rv = 0;

	lock(&physseglock);
	for(ps = physseg; ps->name; ps++){
		if(strcmp(ps->name, name) == 0){
			rv = 1;
			break;
		}
	}
	unlock(&physseglock);
	return rv;
}

uintptr
segattach(Proc *p, ulong attr, char *name, uintptr va, uintptr len)
{
	int sno;
	Segment *s, *os;
	Physseg *ps;

	if(va != 0 && va >= USTKTOP)
		error(Ebadarg);

	validaddr((uintptr)name, 1, 0);
	vmemchr(name, 0, ~0);

	for(sno = 0; sno < NSEG; sno++)
		if(p->seg[sno] == nil && sno != ESEG)
			break;

	if(sno == NSEG)
		error(Enovmem);

	/*
	 *  first look for a global segment with the
	 *  same name
	 */
	if(_globalsegattach != nil){
		s = (*_globalsegattach)(p, name);
		if(s != nil){
			p->seg[sno] = s;
			return s->base;
		}
	}

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
		for (os = p->seg[SSEG]; os != nil; os = isoverlap(p, va, len)) {
			va = os->base;
			if(len >= va)
				error(Enovmem);
			va -= len;
		}
	}

	va &= ~(BY2PG-1);
	if(va == 0 || (va+len) > USTKTOP || (va+len) < va)
		error(Ebadarg);

	if(isoverlap(p, va, len) != nil)
		error(Esoverlap);

	for(ps = physseg; ps->name; ps++)
		if(strcmp(name, ps->name) == 0)
			goto found;

	error(Ebadarg);
found:
	if(len > ps->size)
		error(Enovmem);

	attr &= ~SG_TYPE;		/* Turn off what is not allowed */
	attr |= ps->attr;		/* Copy in defaults */

	s = newseg(attr, va, len/BY2PG);
	s->pseg = ps;
	p->seg[sno] = s;

	return va;
}

void
pteflush(Pte *pte, int s, int e)
{
	Page *pg;
	int i;

	for(i = s; i < e; i++) {
		pg = pte->pages[i];
		if(!pagedout(pg))
			memset(pg->cachectl, PG_TXTFLUSH, sizeof(pg->cachectl));
	}
}

uintptr
syssegflush(va_list list)
{
	Segment *s;
	ulong len, chunk, l;
	Pte *pte;
	uintptr ps, pe, addr;

	addr = va_arg(list, uintptr);
	len = va_arg(list, ulong);

	while(len > 0) {
		s = seg(up, addr, 1);
		if(s == 0)
			error(Ebadarg);

		s->flushme = 1;
	more:
		l = len;
		if(addr+l > s->top)
			l = s->top - addr;

		ps = addr-s->base;
		pte = s->map[ps/PTEMAPMEM];
		ps &= PTEMAPMEM-1;
		pe = PTEMAPMEM;
		if(pe-ps > l){
			pe = ps + l;
			pe = PGROUND(pe);
		}
		if(pe == ps) {
			qunlock(s);
			error(Ebadarg);
		}

		if(pte)
			pteflush(pte, ps/BY2PG, pe/BY2PG);

		chunk = pe-ps;
		len -= chunk;
		addr += chunk;

		if(len > 0 && addr < s->top)
			goto more;

		qunlock(s);
	}
	flushmmu();
	return 0;
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

