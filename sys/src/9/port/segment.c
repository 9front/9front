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
	Image	*list;
	Image	*free;
	Image	*hash[IHASHSIZE];
	QLock	ireclaim;	/* mutex on reclaiming free images */
}imagealloc;

Segment* (*_globalsegattach)(char*);

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

	if((type & SG_TYPE) == SG_PHYSICAL)
		return s;

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
	Pte **pte, **emap;
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

	if(s->mapsize > 0){
		emap = &s->map[s->mapsize];
		for(pte = s->map; pte < emap; pte++)
			if(*pte != nil)
				freepte(s, *pte);

		if(s->map != s->ssegmap)
			free(s->map);
	}

	if(s->profile != nil)
		free(s->profile);

	free(s);
}

void
relocateseg(Segment *s, uintptr offset)
{
	Pte **pte, **emap;
	Page **pg, **pe;

	emap = &s->map[s->mapsize];
	for(pte = s->map; pte < emap; pte++) {
		if(*pte == nil)
			continue;
		pe = (*pte)->last;
		for(pg = (*pte)->first; pg <= pe; pg++) {
			if(!pagedout(*pg))
				(*pg)->va += offset;
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
	case SG_FIXED:
	case SG_STICKY:
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
	Pte **pte, *etp;
	uintptr soff;
	Page **pg;

	if(p->va < s->base || p->va >= s->top || s->mapsize == 0)
		panic("segpage");

	soff = p->va - s->base;
	pte = &s->map[soff/PTEMAPMEM];
	if((etp = *pte) == nil)
		*pte = etp = ptealloc();

	pg = &etp->pages[(soff&(PTEMAPMEM-1))/BY2PG];
	*pg = p;
	if(pg < etp->first)
		etp->first = pg;
	if(pg > etp->last)
		etp->last = pg;
}

Image*
attachimage(int type, Chan *c, uintptr base, ulong len)
{
	Image *i, **l;

	c->flag &= ~CCACHE;
	cclunk(c);

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
		if(imagereclaim(1000) == 0 && imagealloc.free == nil){
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

ulong
imagecached(void)
{
	Image *i, *ie;
	ulong np;

	np = 0;
	ie = &imagealloc.list[conf.nimage];
	for(i = imagealloc.list; i < ie; i++)
		np += i->pgref;
	return np;
}

ulong
imagereclaim(ulong pages)
{
	static Image *i, *ie;
	ulong np;
	int j;

	if(pages == 0)
		return 0;

	eqlock(&imagealloc.ireclaim);
	if(i == nil){
		i = imagealloc.list;
		ie = &imagealloc.list[conf.nimage];
	}
	np = 0;
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
			np += pagereclaim(i, pages - np);
			if(np >= pages)
				break;
		}
	}
	qunlock(&imagealloc.ireclaim);

	return np;
}

void
putimage(Image *i)
{
	Image *f, **l;
	Chan *c;
	long r;

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
	if(mapsize > s->mapsize){
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

	s->top = newtop;
	s->size = newsize;
	qunlock(s);
	return 0;
}

/*
 *  called with s locked
 */
ulong
mcountseg(Segment *s)
{
	Pte **pte, **emap;
	Page **pg, **pe;
	ulong pages;

	if((s->type&SG_TYPE) == SG_PHYSICAL)
		return 0;

	pages = 0;
	emap = &s->map[s->mapsize];
	for(pte = s->map; pte < emap; pte++){
		if(*pte == nil)
			continue;
		pe = (*pte)->last;
		for(pg = (*pte)->first; pg <= pe; pg++)
			if(!pagedout(*pg))
				pages++;
	}
	return pages;
}

/*
 *  called with s locked
 */
void
mfreeseg(Segment *s, uintptr start, ulong pages)
{
	uintptr off;
	Pte **pte, **emap;
	Page **pg, **pe;

	if(pages == 0)
		return;

	switch(s->type&SG_TYPE){
	case SG_PHYSICAL:
	case SG_FIXED:
	case SG_STICKY:
		return;
	}

	/*
	 * we have to make sure other processors flush the
	 * entry from their TLBs before the page is freed.
	 */
	if(s->ref > 1)
		procflushseg(s);

	off = start-s->base;
	pte = &s->map[off/PTEMAPMEM];
	off = (off&(PTEMAPMEM-1))/BY2PG;
	for(emap = &s->map[s->mapsize]; pte < emap; pte++, off = 0) {
		if(*pte == nil) {
			off = PTEPERTAB - off;
			if(off >= pages)
				return;
			pages -= off;
			continue;
		}
		pg = &(*pte)->pages[off];
		for(pe = &(*pte)->pages[PTEPERTAB]; pg < pe; pg++) {
			if(*pg != nil){
				putpage(*pg);
				*pg = nil;
			}
			if(--pages == 0)
				return;
		}
	}
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
			if(isoverlap(s->base, s->top - s->base) != nil){
				putseg(s);
				error(Esoverlap);
			}
			up->seg[sno] = s;
			return s->base;
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

	attr &= ~SG_TYPE;		/* Turn off what is not allowed */
	attr |= ps->attr;		/* Copy in defaults */

	s = newseg(attr, va, len/BY2PG);
	s->pseg = ps;
	up->seg[sno] = s;

	return va;
}

static void
segflush(void *va, uintptr len)
{
	uintptr from, to, off;
	Segment *s;
	Pte *pte;
	Page **pg, **pe;

	from = (uintptr)va;
	to = from + len;
	to = PGROUND(to);
	from &= ~(BY2PG-1);
	if(to < from)
		error(Ebadarg);

	while(from < to) {
		s = seg(up, from, 1);
		if(s == nil)
			error(Ebadarg);

		s->flushme = 1;
	more:
		len = (s->top < to ? s->top : to) - from;
		off = from-s->base;
		pte = s->map[off/PTEMAPMEM];
		off &= PTEMAPMEM-1;
		if(off+len > PTEMAPMEM)
			len = PTEMAPMEM-off;

		if(pte != nil) {
			pg = &pte->pages[off/BY2PG];
			pe = pg + len/BY2PG;
			while(pg < pe) {
				if(!pagedout(*pg))
					(*pg)->txtflush = ~0;
				pg++;
			}
		}

		from += len;
		if(from < to && from < s->top)
			goto more;

		qunlock(s);
	}
}

uintptr
syssegflush(va_list list)
{
	void *va;
	ulong len;

	va = va_arg(list, void*);
	len = va_arg(list, ulong);
	segflush(va, len);
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

Segment*
txt2data(Segment *s)
{
	Segment *ps;

	ps = newseg(SG_DATA, s->base, s->size);
	ps->image = s->image;
	incref(ps->image);
	ps->fstart = s->fstart;
	ps->flen = s->flen;
	ps->flushme = 1;
	qunlock(s);
	putseg(s);
	qlock(ps);
	return ps;
}

Segment*
data2txt(Segment *s)
{
	Segment *ps;

	ps = newseg(SG_TEXT, s->base, s->size);
	ps->image = s->image;
	incref(ps->image);
	ps->fstart = s->fstart;
	ps->flen = s->flen;
	ps->flushme = 1;
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

	for(sno = 0; sno < NSEG; sno++)
		if(up->seg[sno] == nil && sno != ESEG)
			break;
	if(sno == NSEG)
		panic("segmentkproc");

	sio->p = up;
	incref(sio->s);
	up->seg[sno] = sio->s;

	while(waserror())
		;
	for(done = 0; !done;){
		sleep(&sio->cmdwait, cmdready, sio);
		if(waserror())
			sio->err = up->errstr;
		else {
			if(sio->s != nil && up->seg[sno] != sio->s){
				putseg(up->seg[sno]);
				incref(sio->s);
				up->seg[sno] = sio->s;
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
