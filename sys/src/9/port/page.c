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
	if(palloc.freecount <= swapalloc.highwater){
		palloc.freecount += np;
		if(palloc.freecount > swapalloc.highwater)
			wakeup(&palloc.pwait[1]);
		wakeup(&palloc.pwait[0]);
	} else {
		palloc.freecount += np;
	}
	unlock(&palloc);
}

ulong
pagereclaim(Image *i)
{
	Page **h, **e, **l, **x, *p;
	Page *fh, *ft;
	ulong mp, np;

	if(i == nil)
		return 0;

	lock(i);
	mp = i->pgref;
	if(mp == 0){
		unlock(i);
		return 0;
	}
	np = 0;
	fh = ft = nil;
	e = &i->pghash[i->pghsize];
	for(h = i->pghash; h < e; h++){
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

		if(--i->pgref == 0){
			putimage(i);
			goto Done;
		}
		decref(i);
	}
	unlock(i);
Done:
	freepages(fh, ft, np);
	return np;
}

int
needpages(void*)
{
	return palloc.freecount < swapalloc.headroom;
}

static int
havepages(void*)
{
	return palloc.freecount > swapalloc.highwater || up->noswap && palloc.freecount > 0;
}

Page*
newpage(uintptr va, QLock *locked)
{
	Page *p, **l;
	int color;

	lock(&palloc);
	while(!havepages(nil)){
		unlock(&palloc);
		if(locked)
			qunlock(locked);

		if(!waserror()){
			Rendezq *q;

			q = &palloc.pwait[!up->noswap];
			eqlock(q);
			if(!waserror()){
				sleep(q, havepages, nil);
				poperror();
			}
			qunlock(q);
			poperror();
		}

		/*
		 * If called from fault and we lost the lock from
		 * underneath don't waste time allocating and freeing
		 * a page. Fault will call newpage again when it has
		 * reacquired the locks
		 */
		if(locked)
			return nil;

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
	if(--palloc.freecount <= swapalloc.highwater){
		unlock(&palloc);
		kickpager();
	} else {
		unlock(&palloc);
	}

	p->ref = 1;
	p->va = va;
	p->modref = 0;
	inittxtflush(p);

	return p;
}

/*
 *  deadpage() decrements the page refcount
 *  and returns the page when it becomes freeable.
 */
Page*
deadpage(Page *p)
{
	if(p->image != nil){
		decref(p);
		return nil;
	}
	if(decref(p) != 0)
		return nil;
	return p;
}

void
putpage(Page *p)
{
	p = deadpage(p);
	if(p != nil)
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

Page*
fillpage(Page *p, int c)
{
	KMap *k;

	if(p != nil){
		k = kmap(p);
		memset((void*)VA(k), c, BY2PG);
		kunmap(k);
	}
	return p;
}

void
cachepage(Page *p, Image *i)
{
	Page *x, **h;
	uintptr daddr;

	daddr = p->daddr;
	h = &PGHASH(i, daddr);
	lock(i);
	for(x = *h; x != nil; x = x->next)
		if(x->daddr == daddr)
			goto done;
	if(p->image != nil)
		goto done;
	p->image = i;
	p->next = *h;
	*h = p;
	incref(i);
	i->pgref++;
done:
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
	l = &PGHASH(i, p->daddr);
	lock(i);
	if(p->image != i)
		goto done;
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
done:
	unlock(i);
}

Page*
lookpage(Image *i, uintptr daddr)
{
	Page *p, **h, **l;

	l = h = &PGHASH(i, daddr);
	lock(i);
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

	if((p = lookpage(i, daddr)) != nil){
		uncachepage(p);
		putpage(p);
	}
}

void
zeroprivatepages(void)
{
	Page *p, *pe;

	/*
	 * in case of a panic, we might not have a process
	 * context to do the clearing of the private pages.
	 */
	if(up == nil){
		assert(panicking);
		return;
	}

	lock(&palloc);
	pe = palloc.pages + palloc.user;
	for(p = palloc.pages; p != pe; p++) {
		if(p->modref & PG_PRIV){
			incref(p);
			fillpage(p, 0);
			decref(p);
		}
	}
	unlock(&palloc);
}
