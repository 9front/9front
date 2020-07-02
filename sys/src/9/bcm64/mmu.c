#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "sysreg.h"

void
mmu0init(uintptr *l1)
{
	uintptr va, pa, pe, attr;

	/* KZERO */
	attr = PTEWRITE | PTEAF | PTEKERNEL | PTEUXN | PTESH(SHARE_INNER);
	pe = -KZERO;
	for(pa = PHYSDRAM, va = KZERO; pa < pe; pa += PGLSZ(1), va += PGLSZ(1)){
		l1[PTL1X(va, 1)] = pa | PTEVALID | PTEBLOCK | attr;
		l1[PTL1X(pa, 1)] = pa | PTEVALID | PTEBLOCK | attr;
	}

	/* VIRTIO */
	attr = PTEWRITE | PTEAF | PTEKERNEL | PTEUXN | PTEPXN | PTESH(SHARE_OUTER) | PTEDEVICE;
	pe = soc.physio + soc.iosize;
	for(pa = soc.physio, va = soc.virtio; pa < pe; pa += PGLSZ(1), va += PGLSZ(1)){
		if(((pa|va) & PGLSZ(1)-1) != 0){
			l1[PTL1X(va, 1)] = (uintptr)l1 | PTEVALID | PTETABLE;
			for(; pa < pe && ((va|pa) & PGLSZ(1)-1) != 0; pa += PGLSZ(0), va += PGLSZ(0)){
				assert(l1[PTLX(va, 0)] == 0);
				l1[PTLX(va, 0)] = pa | PTEVALID | PTEPAGE | attr;
			}
			break;
		}
		l1[PTL1X(va, 1)] = pa | PTEVALID | PTEBLOCK | attr;
	}

	/* ARMLOCAL */
	attr = PTEWRITE | PTEAF | PTEKERNEL | PTEUXN | PTEPXN | PTESH(SHARE_OUTER) | PTEDEVICE;
	pe = soc.armlocal + MB;
	for(pa = soc.armlocal, va = ARMLOCAL; pa < pe; pa += PGLSZ(1), va += PGLSZ(1)){
		if(((pa|va) & PGLSZ(1)-1) != 0){
			l1[PTL1X(va, 1)] = (uintptr)l1 | PTEVALID | PTETABLE;
			for(; pa < pe && ((va|pa) & PGLSZ(1)-1) != 0; pa += PGLSZ(0), va += PGLSZ(0)){
				assert(l1[PTLX(va, 0)] == 0);
				l1[PTLX(va, 0)] = pa | PTEVALID | PTEPAGE | attr;
			}
			break;
		}
		l1[PTL1X(va, 1)] = pa | PTEVALID | PTEBLOCK | attr;
	}

	if(PTLEVELS > 2)
	for(va = KSEG0; va != 0; va += PGLSZ(2))
		l1[PTL1X(va, 2)] = (uintptr)&l1[L1TABLEX(va, 1)] | PTEVALID | PTETABLE;
	if(PTLEVELS > 3)
	for(va = KSEG0; va != 0; va += PGLSZ(3))
		l1[PTL1X(va, 3)] = (uintptr)&l1[L1TABLEX(va, 2)] | PTEVALID | PTETABLE;
}

void
mmu0clear(uintptr *l1)
{
	uintptr va, pa, pe;

	pe = -KZERO;
	for(pa = PHYSDRAM, va = KZERO; pa < pe; pa += PGLSZ(1), va += PGLSZ(1))
		if(PTL1X(pa, 1) != PTL1X(va, 1))
			l1[PTL1X(pa, 1)] = 0;
	if(PTLEVELS > 2)
	for(pa = PHYSDRAM, va = KZERO; pa < pe; pa += PGLSZ(2), va += PGLSZ(2))
		if(PTL1X(pa, 2) != PTL1X(va, 2))
			l1[PTL1X(pa, 2)] = 0;
	if(PTLEVELS > 3)
	for(pa = PHYSDRAM, va = KZERO; pa < pe; pa += PGLSZ(3), va += PGLSZ(3))
		if(PTL1X(pa, 3) != PTL1X(va, 3))
			l1[PTL1X(pa, 3)] = 0;
}

void
mmuidmap(uintptr *l1)
{
	uintptr va, pa, pe;

	pe = PHYSDRAM + soc.dramsize;
	if(pe > (uintptr)-KZERO)
		pe = (uintptr)-KZERO;
	for(pa = PHYSDRAM, va = KZERO; pa < pe; pa += PGLSZ(1), va += PGLSZ(1))
		l1[PTL1X(pa, 1)] = l1[PTL1X(va, 1)];
	if(PTLEVELS > 2)
	for(pa = PHYSDRAM, va = KZERO; pa < pe; pa += PGLSZ(2), va += PGLSZ(2))
		l1[PTL1X(pa, 2)] = l1[PTL1X(va, 2)];
	if(PTLEVELS > 3)
	for(pa = PHYSDRAM, va = KZERO; pa < pe; pa += PGLSZ(3), va += PGLSZ(3))
		l1[PTL1X(pa, 3)] = l1[PTL1X(va, 3)];
	setttbr(PADDR(&l1[L1TABLEX(0, PTLEVELS-1)]));
	flushtlb();
}

void
mmu1init(void)
{
	m->mmutop = mallocalign(L1TOPSIZE, BY2PG, 0, 0);
	if(m->mmutop == nil)
		panic("mmu1init: no memory for mmutop");
	memset(m->mmutop, 0, L1TOPSIZE);
	mmuswitch(nil);
}

/* KZERO maps the first 1GB of ram */
uintptr
paddr(void *va)
{
	if((uintptr)va >= KZERO)
		return (uintptr)va-KZERO;
	panic("paddr: va=%#p pc=%#p", va, getcallerpc(&va));
	return 0;
}

uintptr
cankaddr(uintptr pa)
{
	if(pa < (uintptr)-KZERO)
		return -KZERO - pa;
	return 0;
}

void*
kaddr(uintptr pa)
{
	if(pa < (uintptr)-KZERO)
		return (void*)(pa + KZERO);
	panic("kaddr: pa=%#p pc=%#p", pa, getcallerpc(&pa));
	return nil;
}

static void*
kmapaddr(uintptr pa)
{
	if(pa < (uintptr)-KZERO)
		return (void*)(pa + KZERO);
	if(pa >= KMAPEND-KMAP)
		panic("kmapaddr: pa=%#p pc=%#p", pa, getcallerpc(&pa));
	return (void*)(pa + KMAP);
}

KMap*
kmap(Page *p)
{
	return kmapaddr(p->pa);
}

void
kunmap(KMap*)
{
}

void
kmapinval(void)
{
}

#define INITMAP	(ROUND((uintptr)end + BY2PG, PGLSZ(1))-KZERO)

static void*
rampage(void)
{
	uintptr pa;

	if(conf.npage)
		return mallocalign(BY2PG, BY2PG, 0, 0);

	pa = conf.mem[0].base;
	assert((pa % BY2PG) == 0);
	assert(pa < INITMAP);
	conf.mem[0].base += BY2PG;
	return KADDR(pa);
}

static void
l1map(uintptr va, uintptr pa, uintptr pe, uintptr attr)
{
	uintptr *l1, *l0;

	assert(pa < pe);

	va &= -BY2PG;
	pa &= -BY2PG;
	pe = PGROUND(pe);

	attr |= PTEKERNEL | PTEAF;

	l1 = (uintptr*)L1;

	while(pa < pe){
		if(l1[PTL1X(va, 1)] == 0 && (pe-pa) >= PGLSZ(1) && ((va|pa) & PGLSZ(1)-1) == 0){
			l1[PTL1X(va, 1)] = PTEVALID | PTEBLOCK | pa | attr;
			va += PGLSZ(1);
			pa += PGLSZ(1);
			continue;
		}
		if(l1[PTL1X(va, 1)] & PTEVALID) {
			assert((l1[PTL1X(va, 1)] & PTETABLE) == PTETABLE);
			l0 = KADDR(l1[PTL1X(va, 1)] & -PGLSZ(0));
		} else {
			l0 = rampage();
			memset(l0, 0, BY2PG);
			l1[PTL1X(va, 1)] = PTEVALID | PTETABLE | PADDR(l0);
		}
		assert(l0[PTLX(va, 0)] == 0);
		l0[PTLX(va, 0)] = PTEVALID | PTEPAGE | pa | attr;
		va += BY2PG;
		pa += BY2PG;
	}
}

static void
kmapram(uintptr base, uintptr limit)
{
	if(base < (uintptr)-KZERO && limit > (uintptr)-KZERO){
		kmapram(base, (uintptr)-KZERO);
		kmapram((uintptr)-KZERO, limit);
		return;
	}
	if(base < INITMAP)
		base = INITMAP;
	if(base >= limit || limit <= INITMAP)
		return;

	l1map((uintptr)kmapaddr(base), base, limit,
		PTEWRITE | PTEPXN | PTEUXN | PTESH(SHARE_INNER));
}

void
meminit(void)
{
	uvlong memsize = 0;
	uintptr pa, va;
	char *p, *e;
	int i;

	if(p = getconf("*maxmem")){
		memsize = strtoull(p, &e, 0) - PHYSDRAM;
		for(i = 1; i < nelem(conf.mem); i++){
			if(e <= p || *e != ' ')
				break;
			p = ++e;
			conf.mem[i].base = strtoull(p, &e, 0);
			if(e <= p || *e != ' ')
				break;
			p = ++e;
			conf.mem[i].limit = strtoull(p, &e, 0);
		}
	}

	if (memsize < INITMAP)		/* sanity */
		memsize = INITMAP;

	getramsize(&conf.mem[0]);
	if(conf.mem[0].limit == 0){
		conf.mem[0].base = PHYSDRAM;
		conf.mem[0].limit = PHYSDRAM + memsize;
	}else if(p != nil)
		conf.mem[0].limit = conf.mem[0].base + memsize;

	/*
	 * now we know the real memory regions, unmap
	 * everything above INITMAP and map again with
	 * the proper sizes.
	 */
	coherence();
	for(va = INITMAP+KZERO; va != 0; va += PGLSZ(1)){
		pa = va-KZERO;
		((uintptr*)L1)[PTL1X(pa, 1)] = 0;
		((uintptr*)L1)[PTL1X(va, 1)] = 0;
	}
	flushtlb();

	pa = PGROUND((uintptr)end)-KZERO;
	for(i=0; i<nelem(conf.mem); i++){
		if(conf.mem[i].limit >= KMAPEND-KMAP)
			conf.mem[i].limit = KMAPEND-KMAP;

		if(conf.mem[i].limit <= conf.mem[i].base){
			conf.mem[i].limit = conf.mem[i].base = 0;
			continue;
		}

		if(conf.mem[i].base < PHYSDRAM + soc.dramsize
		&& conf.mem[i].limit > PHYSDRAM + soc.dramsize)
			conf.mem[i].limit = PHYSDRAM + soc.dramsize;

		/* take kernel out of allocatable space */
		if(pa > conf.mem[i].base && pa < conf.mem[i].limit)
			conf.mem[i].base = pa;

		kmapram(conf.mem[i].base, conf.mem[i].limit);
	}
	flushtlb();

	/* rampage() is now done, count up the pages for each bank */
	for(i=0; i<nelem(conf.mem); i++)
		conf.mem[i].npage = (conf.mem[i].limit - conf.mem[i].base)/BY2PG;
}

uintptr
mmukmap(uintptr va, uintptr pa, usize size)
{
	uintptr attr, off;

	if(va == 0)
		return 0;

	off = pa & BY2PG-1;

	attr = va & PTEMA(7);
	attr |= PTEWRITE | PTEUXN | PTEPXN | PTESH(SHARE_OUTER);

	va &= -BY2PG;
	pa &= -BY2PG;

	l1map(va, pa, pa + off + size, attr);
	flushtlb();

	return va + off;
}

void*
vmap(uvlong pa, int size)
{
	static uintptr base = VMAP;
	uvlong pe = pa + size;
	uintptr va;

	va = base;
	base += PGROUND(pe) - (pa & -BY2PG);
	
	return (void*)mmukmap(va | PTEDEVICE, pa, size);
}

void
vunmap(void *, int)
{
}

static uintptr*
mmuwalk(uintptr va, int level)
{
	uintptr *table, pte;
	Page *pg;
	int i, x;

	x = PTLX(va, PTLEVELS-1);
	table = m->mmutop;
	for(i = PTLEVELS-2; i >= level; i--){
		pte = table[x];
		if(pte & PTEVALID) {
			if(pte & (0xFFFFULL<<48))
				iprint("strange pte %#p va %#p\n", pte, va);
			pte &= ~(0xFFFFULL<<48 | BY2PG-1);
		} else {
			pg = up->mmufree;
			if(pg == nil)
				return nil;
			up->mmufree = pg->next;
			pg->va = va & -PGLSZ(i+1);
			if((pg->next = up->mmuhead[i+1]) == nil)
				up->mmutail[i+1] = pg;
			up->mmuhead[i+1] = pg;
			pte = pg->pa;
			memset(kmapaddr(pte), 0, BY2PG);
			coherence();
			table[x] = pte | PTEVALID | PTETABLE;
		}
		table = kmapaddr(pte);
		x = PTLX(va, (uintptr)i);
	}
	return &table[x];
}

static Proc *asidlist[256];

static int
allocasid(Proc *p)
{
	static Lock lk;
	Proc *x;
	int a;

	lock(&lk);
	a = p->asid;
	if(a < 0)
		a = -a;
	if(a == 0)
		a = p->pid;
	for(;; a++){
		a %= nelem(asidlist);
		if(a == 0)
			continue;	// reserved
		x = asidlist[a];
		if(x == p || x == nil || (x->asid < 0 && x->mach == nil))
			break;
	}
	p->asid = a;
	asidlist[a] = p;
	unlock(&lk);

	return x != p;
}

static void
freeasid(Proc *p)
{
	int a;

	a = p->asid;
	if(a < 0)
		a = -a;
	if(a > 0 && asidlist[a] == p)
		asidlist[a] = nil;
	p->asid = 0;
}

void
putasid(Proc *p)
{
	/*
	 * Prevent the following scenario:
	 *	pX sleeps on cpuA, leaving its page tables in mmutop
	 *	pX wakes up on cpuB, and exits, freeing its page tables
	 *  pY on cpuB allocates a freed page table page and overwrites with data
	 *  cpuA takes an interrupt, and is now running with bad page tables
	 * In theory this shouldn't hurt because only user address space tables
	 * are affected, and mmuswitch will clear mmutop before a user process is
	 * dispatched.  But empirically it correlates with weird problems, eg
	 * resetting of the core clock at 0x4000001C which confuses local timers.
	 */
	if(conf.nmach > 1)
		mmuswitch(nil);

	if(p->asid > 0)
		p->asid = -p->asid;
}

void
putmmu(uintptr va, uintptr pa, Page *pg)
{
	uintptr *pte, old;
	int s;

	s = splhi();
	while((pte = mmuwalk(va, 0)) == nil){
		spllo();
		up->mmufree = newpage(0, nil, 0);
		splhi();
	}
	old = *pte;
	*pte = 0;
	if((old & PTEVALID) != 0)
		flushasidvall((uvlong)up->asid<<48 | va>>12);
	else
		flushasidva((uvlong)up->asid<<48 | va>>12);
	*pte = pa | PTEPAGE | PTEUSER | PTEPXN | PTENG | PTEAF |
		(((pa & PTEMA(7)) == PTECACHED)? PTESH(SHARE_INNER): PTESH(SHARE_OUTER));
	if(pg->txtflush & (1UL<<m->machno)){
		/* pio() sets PG_TXTFLUSH whenever a text pg has been written */
		cachedwbinvse(kmap(pg), BY2PG);
		cacheiinvse((void*)va, BY2PG);
		pg->txtflush &= ~(1UL<<m->machno);
	}
	splx(s);
}

static void
mmufree(Proc *p)
{
	int i;

	freeasid(p);

	for(i=1; i<PTLEVELS; i++){
		if(p->mmuhead[i] == nil)
			break;
		p->mmutail[i]->next = p->mmufree;
		p->mmufree = p->mmuhead[i];
		p->mmuhead[i] = p->mmutail[i] = nil;
	}
}

void
mmuswitch(Proc *p)
{
	uintptr va;
	Page *t;

	for(va = UZERO; va < USTKTOP; va += PGLSZ(PTLEVELS-1))
		m->mmutop[PTLX(va, PTLEVELS-1)] = 0;

	if(p == nil){
		setttbr(PADDR(m->mmutop));
		return;
	}

	if(p->newtlb){
		mmufree(p);
		p->newtlb = 0;
	}

	if(allocasid(p))
		flushasid((uvlong)p->asid<<48);

	setttbr((uvlong)p->asid<<48 | PADDR(m->mmutop));

	for(t = p->mmuhead[PTLEVELS-1]; t != nil; t = t->next){
		va = t->va;
		m->mmutop[PTLX(va, PTLEVELS-1)] = t->pa | PTEVALID | PTETABLE;
	}
}

void
mmurelease(Proc *p)
{
	Page *t;

	mmuswitch(nil);
	mmufree(p);

	if((t = p->mmufree) != nil){
		do {
			p->mmufree = t->next;
			if(--t->ref != 0)
				panic("mmurelease: bad page ref");
			pagechainhead(t);
		} while((t = p->mmufree) != nil);
		pagechaindone();
	}
}

void
flushmmu(void)
{
	int x;

	x = splhi();
	up->newtlb = 1;
	mmuswitch(up);
	splx(x);
}

void
checkmmu(uintptr, uintptr)
{
}
