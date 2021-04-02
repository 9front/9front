#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

Palloc palloc;

ulong
nkpages(Confmem *cm)
{
	return ((cm->klimit - cm->kbase) + BY2PG-1) / BY2PG;
}

void
pageinit(void)
{
	int color, i, j;
	Page *p, **t;
	Confmem *cm;
	vlong m, v, u;

	if(palloc.pages == nil){
		ulong np;

		np = 0;
		for(i=0; i<nelem(conf.mem); i++){
			cm = &conf.mem[i];
			np += cm->npage - nkpages(cm);
		}
		palloc.pages = xalloc(np*sizeof(Page));
		if(palloc.pages == nil)
			panic("pageinit");
	}

	color = 0;
	palloc.freecount = 0;
	palloc.head = nil;

	t = &palloc.head;
	p = palloc.pages;

	for(i=0; i<nelem(conf.mem); i++){
		cm = &conf.mem[i];
		for(j=nkpages(cm); j<cm->npage; j++){
			memset(p, 0, sizeof *p);
			p->pa = cm->base+j*BY2PG;
			if(cankaddr(p->pa) && (KADDR(p->pa) == nil || KADDR(p->pa) == (void*)-BY2PG))
				continue;
			p->color = color;
			color = (color+1)%NCOLOR;
			*t = p, t = &p->next;
			palloc.freecount++;
			p++;
		}
	}

	palloc.user = p - palloc.pages;
	u = palloc.user*BY2PG;
	v = u + conf.nswap*BY2PG;

	/* Paging numbers */
	swapalloc.highwater = (palloc.user*5)/100;
	swapalloc.headroom = swapalloc.highwater + (swapalloc.highwater/4);

	m = 0;
	for(i=0; i<nelem(conf.mem); i++)
		if(conf.mem[i].npage)
			m += conf.mem[i].npage*BY2PG;
	m += PGROUND(end - (char*)KTZERO);

	print("%lldM memory: ", (m+1024*1024-1)/(1024*1024));
	print("%lldM kernel data, ", (m-u+1024*1024-1)/(1024*1024));
	print("%lldM user, ", u/(1024*1024));
	print("%lldM swap\n", v/(1024*1024));
}

static void
pagechaindone(void)
{
	if(palloc.pwait[0].p != nil && wakeup(&palloc.pwait[0]) != nil)
		return;
	if(palloc.pwait[1].p != nil)
		wakeup(&palloc.pwait[1]);
}

void
freepages(Page *head, Page *tail, ulong np)
{
	if(head == nil)
		return;
	if(tail == nil){
		tail = head;
		for(np = 1;; np++){
			tail->ref = 0;
			if(tail->next == nil)
				break;
			tail = tail->next;
		}
	}
	lock(&palloc);
	tail->next = palloc.head;
	palloc.head = head;
	palloc.freecount += np;
	pagechaindone();
	unlock(&palloc);
}

ulong
pagereclaim(Image *i)
{
	Page **h, **l, **x, *p;
	Page *fh, *ft;
	ulong np;

	lock(i);
	if(i->pgref == 0){
		unlock(i);
		return 0;
	}
	incref(i);

	np = 0;
	fh = ft = nil;
	for(h = i->pghash; h < &i->pghash[PGHSIZE]; h++){
		l = h;
		x = nil;
		for(p = *l; p != nil; p = p->next){
			if(p->ref == 0)
				x = l;
			l = &p->next;
		}
		if(x == nil)
			continue;

		p = *x;
		*x = p->next;
		p->next = nil;
		p->image = nil;
		p->daddr = ~0;

		if(fh == nil)
			fh = p;
		else
			ft->next = p;
		ft = p;
		np++;

		decref(i);
		if(--i->pgref == 0)
			break;
	}
	putimage(i);

	if(np > 0)
		freepages(fh, ft, np);

	return np;
}

static int
ispages(void*)
{
	return palloc.freecount > swapalloc.highwater || up->noswap && palloc.freecount > 0;
}

Page*
newpage(int clear, Segment **s, uintptr va)
{
	Page *p, **l;
	KMap *k;
	int color;

	lock(&palloc);
	while(!ispages(nil)){
		unlock(&palloc);
		if(s != nil)
			qunlock(*s);

		if(!waserror()){
			Rendezq *q;

			q = &palloc.pwait[!up->noswap];
			eqlock(q);	
			if(!waserror()){
				kickpager();
				sleep(q, ispages, nil);
				poperror();
			}
			qunlock(q);
			poperror();
		}

		/*
		 * If called from fault and we lost the segment from
		 * underneath don't waste time allocating and freeing
		 * a page. Fault will call newpage again when it has
		 * reacquired the segment locks
		 */
		if(s != nil){
			*s = nil;
			return nil;
		}
		lock(&palloc);
	}

	/* First try for our colour */
	color = getpgcolor(va);
	l = &palloc.head;
	for(p = *l; p != nil; p = p->next){
		if(p->color == color)
			break;
		l = &p->next;
	}

	if(p == nil) {
		l = &palloc.head;
		p = *l;
	}

	*l = p->next;
	p->next = nil;
	palloc.freecount--;
	unlock(&palloc);

	p->ref = 1;
	p->va = va;
	p->modref = 0;
	p->txtflush = 0;

	if(clear) {
		k = kmap(p);
		memset((void*)VA(k), 0, BY2PG);
		kunmap(k);
	}

	return p;
}

void
putpage(Page *p)
{
	if(onswap(p)) {
		putswap(p);
		return;
	}
	if(p->image != nil) {
		decref(p);
		return;
	}
	if(decref(p) == 0)
		freepages(p, p, 1);
}

void
copypage(Page *f, Page *t)
{
	KMap *ks, *kd;

	ks = kmap(f);
	kd = kmap(t);
	memmove((void*)VA(kd), (void*)VA(ks), BY2PG);
	kunmap(ks);
	kunmap(kd);
}

void
cachepage(Page *p, Image *i)
{
	Page **h;

	lock(i);
	p->image = i;
	h = &PGHASH(i, p->daddr);
	p->next = *h;
	*h = p;
	incref(i);
	i->pgref++;
	unlock(i);
}

void
uncachepage(Page *p)
{
	Page **l, *x;
	Image *i;

	i = p->image;
	if(i == nil)
		return;

	lock(i);
	if(p->image != i){
		unlock(i);
		return;
	}
	l = &PGHASH(i, p->daddr);
	for(x = *l; x != nil; x = x->next) {
		if(x == p){
			*l = p->next;
			p->next = nil;
			p->image = nil;
			p->daddr = ~0;
			i->pgref--;
			putimage(i);
			return;
		}
		l = &x->next;
	}
	unlock(i);
}

Page*
lookpage(Image *i, uintptr daddr)
{
	Page *p, **h, **l;

	lock(i);
	l = h = &PGHASH(i, daddr);
	for(p = *l; p != nil; p = p->next){
		if(p->daddr == daddr){
			*l = p->next;
			p->next = *h;
			*h = p;
			incref(p);
			unlock(i);
			return p;
		}
		l = &p->next;
	}
	unlock(i);

	return nil;
}

void
cachedel(Image *i, uintptr daddr)
{
	Page *p;

	while((p = lookpage(i, daddr)) != nil){
		uncachepage(p);
		putpage(p);
	}
}


Pte*
ptecpy(Pte *old)
{
	Pte *new;
	Page **src, **dst;

	new = ptealloc();
	dst = &new->pages[old->first-old->pages];
	new->first = dst;
	for(src = old->first; src <= old->last; src++, dst++)
		if(*src != nil) {
			if(onswap(*src))
				dupswap(*src);
			else
				incref(*src);
			new->last = dst;
			*dst = *src;
		}

	return new;
}

Pte*
ptealloc(void)
{
	Pte *new;

	new = smalloc(sizeof(Pte));
	new->first = &new->pages[PTEPERTAB];
	new->last = new->pages;
	return new;
}

void
freepte(Segment*, Pte *p)
{
	Page **pg, **pe;

	pg = p->first;
	pe = p->last;
	while(pg <= pe){
		if(*pg != nil)
			putpage(*pg);
		pg++;
	}
	free(p);
}
