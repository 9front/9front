#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

Palloc palloc;

void
pageinit(void)
{
	int color, i, j;
	Page *p;
	Pallocmem *pm;
	vlong m, v, u;

	if(palloc.pages == nil){
		ulong np;

		np = 0;
		for(i=0; i<nelem(palloc.mem); i++){
			pm = &palloc.mem[i];
			np += pm->npage;
		}
		palloc.pages = xalloc(np*sizeof(Page));
		if(palloc.pages == nil)
			panic("pageinit");
	}

	color = 0;
	palloc.head = nil;
	p = palloc.pages;
	for(i=0; i<nelem(palloc.mem); i++){
		pm = &palloc.mem[i];
		for(j=0; j<pm->npage; j++){
			memset(p, 0, sizeof *p);
			p->pa = pm->base+j*BY2PG;
			p->color = color;
			color = (color+1)%NCOLOR;
			pagechainhead(p);
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
pagechainhead(Page *p)
{
	p->next = palloc.head;
	palloc.head = p;
	palloc.freecount++;
}

static void
freepages(Page *head, Page *tail, int n)
{
	lock(&palloc);
	tail->next = palloc.head;
	palloc.head = head;
	palloc.freecount += n;
	if(palloc.r.p != nil)
		wakeup(&palloc.r);
	unlock(&palloc);
}

int
pagereclaim(Image *i, int min)
{
	Page **h, **l, *p;
	Page *fh, *ft;
	int n;

	lock(i);
	if(i->pgref == 0){
		unlock(i);
		return 0;
	}
	incref(i);

	n = 0;
	fh = ft = nil;
	for(h = i->pghash; h < &i->pghash[PGHSIZE]; h++){
		if((p = *h) == nil)
			continue;
		for(l = h; p != nil; p = p->next){
			if(p->ref == 0)
				break;
			l = &p->next;
		}
		if(p == nil)
			continue;

		*l = p->next;
		p->next = nil;
		p->image = nil;
		p->daddr = ~0;
		i->pgref--;
		decref(i);

		if(fh == nil)
			fh = p;
		else
			ft->next = p;
		ft = p;
		if(++n >= min)
			break;
	}
	unlock(i);
	putimage(i);

	if(n > 0)
		freepages(fh, ft, n);

	return n;
}

int
ispages(void*)
{
	return palloc.freecount >= swapalloc.highwater;
}

Page*
newpage(int clear, Segment **s, uintptr va)
{
	Page *p, **l;
	KMap *k;
	uchar ct;
	int i, color;

	color = getpgcolor(va);
	lock(&palloc);
	for(;;) {
		if(palloc.freecount > swapalloc.highwater)
			break;
		if(up->kp && palloc.freecount > 0)
			break;
		unlock(&palloc);
		if(s != nil)
			qunlock(*s);

		if(!waserror()){
			eqlock(&palloc.pwait);	/* Hold memory requesters here */

			if(!waserror()){
				kickpager();
				tsleep(&palloc.r, ispages, 0, 1000);
				poperror();
			}

			qunlock(&palloc.pwait);

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
	l = &palloc.head;
	for(p = *l; p != nil; p = p->next){
		if(p->color == color)
			break;
		l = &p->next;
	}

	ct = PG_NOFLUSH;
	if(p == nil) {
		l = &palloc.head;
		p = *l;
		p->color = color;
		ct = PG_NEWCOL;
	}

	*l = p->next;
	p->next = nil;
	palloc.freecount--;
	unlock(&palloc);

	p->ref = 1;
	p->va = va;
	p->modref = 0;
	for(i = 0; i < MAXMACH; i++)
		p->cachectl[i] = ct;

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

Page*
auxpage(void)
{
	Page *p;

	lock(&palloc);
	p = palloc.head;
	if(p == nil || palloc.freecount < swapalloc.highwater) {
		unlock(&palloc);
		return nil;
	}
	palloc.head = p->next;
	p->next = nil;
	palloc.freecount--;
	unlock(&palloc);
	p->ref = 1;

	return p;
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
			unlock(i);
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
	Page *p;

	lock(i);
	for(p = PGHASH(i, daddr); p != nil; p = p->next) {
		if(p->daddr == daddr) {
			incref(p);
			unlock(i);
			return p;
		}
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
freepte(Segment *s, Pte *p)
{
	void (*fn)(Page*);
	Page **pg, **ptop;

	switch(s->type&SG_TYPE) {
	case SG_PHYSICAL:
		fn = s->pseg->pgfree;
		ptop = &p->pages[PTEPERTAB];
		if(fn != nil) {
			for(pg = p->pages; pg < ptop; pg++) {
				if(*pg == nil)
					continue;
				(*fn)(*pg);
				*pg = nil;
			}
			break;
		}
		for(pg = p->pages; pg < ptop; pg++) {
			if(*pg != nil) {
				if(decref(*pg) == 0)
					free(*pg);
				*pg = nil;
			}
		}
		break;
	default:
		for(pg = p->first; pg <= p->last; pg++)
			if(*pg != nil) {
				putpage(*pg);
				*pg = nil;
			}
	}
	free(p);
}

ulong
pagenumber(Page *p)
{
	return p-palloc.pages;
}

void
checkpagerefs(void)
{
	int s;
	ulong i, np, nwrong;
	ulong *ref;
	
	np = palloc.user;
	ref = malloc(np*sizeof ref[0]);
	if(ref == nil){
		print("checkpagerefs: out of memory\n");
		return;
	}
	
	/*
	 * This may not be exact if there are other processes
	 * holding refs to pages on their stacks.  The hope is
	 * that if you run it on a quiescent system it will still
	 * be useful.
	 */
	s = splhi();
	lock(&palloc);
	countpagerefs(ref, 0);
	portcountpagerefs(ref, 0);
	nwrong = 0;
	for(i=0; i<np; i++){
		if(palloc.pages[i].ref != ref[i]){
			iprint("page %#p ref %ld actual %lud\n", 
				palloc.pages[i].pa, palloc.pages[i].ref, ref[i]);
			ref[i] = 1;
			nwrong++;
		}else
			ref[i] = 0;
	}
	countpagerefs(ref, 1);
	portcountpagerefs(ref, 1);
	iprint("%lud mistakes found\n", nwrong);
	unlock(&palloc);
	splx(s);
}

void
portcountpagerefs(ulong *ref, int print)
{
	ulong i, j, k, ns, n;
	Page **pg, *entry;
	Proc *p;
	Pte *pte;
	Segment *s;

	/*
	 * Pages in segments.  s->mark avoids double-counting.
	 */
	n = 0;
	ns = 0;
	for(i=0; i<conf.nproc; i++){
		p = proctab(i);
		for(j=0; j<NSEG; j++){
			s = p->seg[j];
			if(s)
				s->mark = 0;
		}
	}
	for(i=0; i<conf.nproc; i++){
		p = proctab(i);
		for(j=0; j<NSEG; j++){
			s = p->seg[j];
			if(s == nil || s->mark++)
				continue;
			ns++;
			for(k=0; k<s->mapsize; k++){
				pte = s->map[k];
				if(pte == nil)
					continue;
				for(pg = pte->first; pg <= pte->last; pg++){
					entry = *pg;
					if(pagedout(entry))
						continue;
					if(print){
						if(ref[pagenumber(entry)])
							iprint("page %#p in segment %#p\n", entry->pa, s);
						continue;
					}
					if(ref[pagenumber(entry)]++ == 0)
						n++;
				}
			}
		}
	}
	if(!print){
		iprint("%lud pages in %lud segments\n", n, ns);
		for(i=0; i<conf.nproc; i++){
			p = proctab(i);
			for(j=0; j<NSEG; j++){
				s = p->seg[j];
				if(s == nil)
					continue;
				if(s->ref != s->mark){
					iprint("segment %#p (used by proc %lud pid %lud) has bad ref count %lud actual %lud\n",
						s, i, p->pid, s->ref, s->mark);
				}
			}
		}
	}
}

