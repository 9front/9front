#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

ulong *mpcore, *slcr;
uchar *ocm;

void
mmuinit(void)
{
	m->l1.pa = ttbget();
	m->l1.va = KADDR(m->l1.pa);
	memset((uchar*)TMAPL2(m->machno), 0, TMAPL2SZ);
	m->l1.va[L1X(TMAP)] = PADDR(TMAPL2(m->machno)) | L1PT;
	incref(&m->l1);
	if(mpcore != nil)
		return;
	mpcore = vmap(MPCORE_BASE, 0x2000);
	slcr = vmap(SLCR_BASE, 0x1000);
	ocm = vmap(OCM_BASE, -OCM_BASE);
}

void
l1switch(L1 *p, int flush)
{
	assert(!islo());

	ttbput(p->pa);
	if(flush){
		if(++m->asid == 0)
			flushtlb();
		setasid(m->asid);
	}
}

static L1 *
l1alloc(void)
{
	L1 *p;
	int s;

	s = splhi();
	p = m->l1free;
	if(p != nil){
		m->l1free = p->next;
		p->next = nil;
		m->nfree--;
		splx(s);
		return p;
	}
	splx(s);
	p = smalloc(sizeof(L1));
	for(;;){
		p->va = mallocalign(L1SZ, L1SZ, 0, 0);
		if(p->va != nil)
			break;
		if(!waserror()){
			resrcwait("no memory for L1 table");
			poperror();
		}
	}
	p->pa = PADDR(p->va);
	memmove(p->va, m->l1.va, L1SZ);
	return p;
}

static void
l1free(L1 *l1)
{
	if(islo())
		panic("l1free: islo");
	if(m->nfree >= 40){
		free(l1->va);
		free(l1);
	}else{
		l1->next = m->l1free;
		m->l1free = l1;
		m->nfree++;
	}
}

static void
upallocl1(void)
{
	L1 *p;
	int s;

	if(up->l1 != nil)
		return;
	p = l1alloc();
	s = splhi();
	if(up->l1 != nil)
		panic("upalloc1: up->l1 != nil");
	up->l1 = p;
	l1switch(p, 1);
	splx(s);
}

static void
l2free(Proc *proc)
{
	ulong *t;
	Page *p, **l;

	if(proc->l1 == nil || proc->mmuused == nil)
		return;
	l = &proc->mmuused;
	for(p = *l; p != nil; p = p->next){
		t = proc->l1->va + p->daddr;
		*t++ = 0;
		*t++ = 0;
		*t++ = 0;
		*t = 0;
		l = &p->next;
	}
	proc->l1->va[L1X(TMAP)] = 0;
	*l = proc->mmufree;
	proc->mmufree = proc->mmuused;
	proc->mmuused = 0;
}

void
mmuswitch(Proc *p)
{
	if(p->newtlb){
		p->newtlb = 0;
		l2free(p);
	}
	if(p->l1 != nil)
		l1switch(p->l1, 1);
	else
		l1switch(&m->l1, 1);
}

void
putmmu(uintptr va, uintptr pa, Page *pg)
{
	Page *p;
	ulong *e;
	ulong *l2;
	ulong old;
	uintptr l2p;
	int s;

	if(up->l1 == nil)
		upallocl1();
	if((pa & PTEUNCACHED) == 0)
		pa |= L2CACHED;
	e = &up->l1->va[L1RX(va)];
	if((*e & 3) == 0){
		p = up->mmufree;
		if(p != nil)
			up->mmufree = p->next;
		else
			p = newpage(0, 0, 0);
		p->daddr = L1RX(va);
		p->next = up->mmuused;
		up->mmuused = p;
		s = splhi();
		l2p = p->pa;
		l2 = tmpmap(l2p);
		memset(l2, 0, BY2PG);
		coherence();
		e[0] = p->pa | L1PT;
		e[1] = e[0] + L2SZ;
		e[2] = e[1] + L2SZ;
		e[3] = e[2] + L2SZ;
		coherence();
	}else{
		s = splhi();
		l2p = *e & ~(BY2PG - 1);
		l2 = tmpmap(l2p);
	}
	e = &l2[L2RX(va)];
	old = *e;
	*e = pa | L2VALID | L2USER | L2LOCAL;
	tmpunmap(l2);
	splx(s);
	if((old & L2VALID) != 0)
		flushpg((void *) va);
	if(pg->txtflush & (1<<m->machno)){
		cleandse((void *) va, (void *) (va + BY2PG));
		invalise((void *) va, (void *) (va + BY2PG));
		pg->txtflush &= ~(1<<m->machno);
	}
}

void
checkmmu(uintptr, uintptr)
{
}

void
flushmmu(void)
{
	int s;

	s = splhi();
	up->newtlb = 1;
	mmuswitch(up);
	splx(s);
}

void
mmurelease(Proc *proc)
{
	Page *p, *n;

	if(islo())
		panic("mmurelease: islo");
	
	l1switch(&m->l1, 0);
	if(proc->kmaptable != nil){
		if(proc->l1 == nil)
			panic("mmurelease: no l1");
		if(decref(proc->kmaptable) != 0)
			panic("mmurelease: kmap ref %ld", proc->kmaptable->ref);
		if(proc->nkmap)
			panic("mmurelease: nkmap %d", proc->nkmap);
		if(PPN(proc->l1->va[L1X(KMAP)]) != proc->kmaptable->pa)
			panic("mmurelease: bad kmap l2 %#.8lux kmap %#.8lux", proc->l1->va[L1X(KMAP)], proc->kmaptable->pa);
		proc->l1->va[L1X(KMAP)] = 0;
		pagechainhead(proc->kmaptable);
		proc->kmaptable = nil;
	}
	if(proc->l1 != nil){
		l2free(proc);
		l1free(proc->l1);
		proc->l1 = nil;
	}
	for(p = proc->mmufree; p != nil; p = n){
		n = p->next;
		if(decref(p) != 0)
			panic("mmurelease: p->ref %ld", p->ref);
		pagechainhead(p);
	}
	if(proc->mmufree != nil)
		pagechaindone();
	proc->mmufree = nil;
}

void
countpagerefs(ulong *, int)
{
	print("countpagerefs\n");
}

uintptr
paddr(void *v)
{
	if((uintptr)v >= KZERO)
		return (uintptr)v-KZERO;
	if((uintptr)v >= VMAP)
		return ((uintptr)v & (BY2PG-1)) | PPN(((ulong*)VMAPL2)[(uintptr)v-VMAP >> PGSHIFT]);
	panic("paddr: va=%#p pc=%#p", v, getcallerpc(&v));
	return 0;
}

void *
kaddr(uintptr u)
{
	if(u < (uintptr)-KZERO)
		return (void *)(u + KZERO);
	if(u >= OCM_BASE)
		return (void *)(ocm + (u - OCM_BASE));
	panic("kaddr: pa=%#p pc=%#p", u, getcallerpc(&u));
	return nil;
}

uintptr
cankaddr(uintptr u)
{
	if(u < (uintptr)-KZERO)
		return -KZERO - u;
	if(u >= OCM_BASE)
		return -u;
	return 0;
}

KMap *
kmap(Page *page)
{
	ulong *e, *v;
	int i, s;

	if(cankaddr(page->pa))
		return (KMap*)KADDR(page->pa);
	if(up == nil)
		panic("kmap: up=0 pc=%#.8lux", getcallerpc(&page));
	if(up->l1 == nil)
		upallocl1();
	if(up->nkmap < 0)
		panic("kmap %lud %s: nkmap=%d", up->pid, up->text, up->nkmap);
	up->nkmap++;
	e = &up->l1->va[L1X(KMAP)];
	if((*e & 3) == 0){
		if(up->kmaptable != nil)
			panic("kmaptable != nil");
		up->kmaptable = newpage(0, 0, 0);
		s = splhi();
		v = tmpmap(up->kmaptable->pa);
		memset(v, 0, BY2PG);
		v[0] = page->pa | L2KERRW | L2VALID | L2CACHED | L2LOCAL;
		v[NKMAP] = up->kmaptable->pa | L2KERRW | L2VALID | L2CACHED | L2LOCAL;
		tmpunmap(v);
		splx(s);
		*e = up->kmaptable->pa | L1PT;
		coherence();
		return (KMap *) KMAP;
	}
	if(up->kmaptable == nil)
		panic("kmaptable == nil");
	e = (ulong *) (KMAP + NKMAP * BY2PG);
	for(i = 0; i < NKMAP; i++)
		if((e[i] & 3) == 0){
			e[i] = page->pa | L2KERRW | L2VALID | L2CACHED | L2LOCAL;
			coherence();
			return (KMap *) (KMAP + i * BY2PG);
		}
	panic("out of kmap");
	return nil;
}

void
kunmap(KMap *arg)
{
	uintptr va;
	ulong *e;
	
	va = (uintptr) arg;
	if(va >= KZERO)
		return;
	if(up->l1 == nil || (up->l1->va[L1X(KMAP)] & 3) == 0)
		panic("kunmap: no kmaps");
	if(va < KMAP || va >= KMAP + NKMAP * BY2PG)
		panic("kunmap: bad address %#.8lux pc=%#p", va, getcallerpc(&arg));
	e = (ulong *) (KMAP + NKMAP * BY2PG) + L2X(va);
	if((*e & 3) == 0)
		panic("kunmap: not mapped %#.8lux pc=%#p", va, getcallerpc(&arg));
	up->nkmap--;
	if(up->nkmap < 0)
		panic("kunmap %lud %s: nkmap=%d", up->pid, up->text, up->nkmap);
	*e = 0;
	coherence();
	flushpg((void *) va);
}

void *
tmpmap(ulong pa)
{
	ulong *u, *ub, *ue;

	if(islo())
		panic("tmpmap: islow %#p", getcallerpc(&pa));
	if(cankaddr(pa))
		return KADDR(pa);
	ub = (ulong *) TMAPL2(m->machno);
	ue = ub + NL2;
	for(u = ub; u < ue; u++)
		if((*u & 3) == 0){
			*u = pa | L2VALID | L2CACHED | L2KERRW;

			assert(m->l1.va[L1X(TMAP)] != 0);
			if(up != nil && up->l1 != nil)
				up->l1->va[L1X(TMAP)] = m->l1.va[L1X(TMAP)];

			coherence();
			return (void *) ((u - ub) * BY2PG + TMAP);
		}
	panic("tmpmap: full (pa=%#.8lux)", pa);
	return nil;
}

void
tmpunmap(void *v)
{
	ulong *u;

	if(v >= (void*) KZERO)
		return;
	if(v < (void*)TMAP || v >= (void*)(TMAP + TMAPSZ))
		panic("tmpunmap: invalid address (va=%#.8lux)", (uintptr) v);
	u = (ulong *) TMAPL2(m->machno) + L2X(v);
	if((*u & 3) == 0)
		panic("tmpunmap: double unmap (va=%#.8lux)", (uintptr) v);
	*u = 0;
	coherence();
	flushpg(v);
}

void *
vmap(uintptr pa, ulong sz)
{
	ulong np;
	void *vr, *ve;
	static ulong *vp = (ulong *) VMAPL2 + 1; /* first page is uart */
	
	if((pa & BY2PG - 1) != 0)
		panic("vmap: misaligned pa=%#.8lux", pa);
	np = (sz + BY2PG - 1) >> PGSHIFT;
	vr = (char*) VMAP + (vp - (ulong *)VMAPL2 << PGSHIFT);
	ve = (ulong *) (VMAPL2 + VMAPL2SZ);
	while(np-- != 0){
		if(vp == ve)
			panic("vmap: out of vmap space (pa=%#.8lux)", pa);
		*vp++ = pa | L2VALID | L2DEVICE | L2KERRW;
		pa += BY2PG;
	}
	coherence();
	return vr;
}

/* nasty things happen when there are cache entries for uncached memory
   so must make sure memory is not mapped ANYWHERE cached */
void*
ucalloc(ulong len)
{
	static uchar *free = nil;
	static Lock l;
	uchar *va;

	if(len == 0)
		panic("ualloc: len == 0");

	ilock(&l);
	if(free == nil)
		free = ocm + -OCM_BASE - BY2PG;	/* last page is cpu1 bootstrap */
	len = PGROUND(len);
	free -= len;
	if(free < ocm)
		panic("ualloc: out of uncached memory");
	va = free;
	iunlock(&l);

	invaldse(va, va + len);
	return (void*)va;
}
