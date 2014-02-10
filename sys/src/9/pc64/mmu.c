#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"

/*
 * Simple segment descriptors with no translation.
 */
#define	EXECSEGM(p) 	{ 0, SEGL|SEGP|SEGPL(p)|SEGEXEC }
#define	DATASEGM(p) 	{ 0, SEGB|SEGG|SEGP|SEGPL(p)|SEGDATA|SEGW }
#define	EXEC32SEGM(p) 	{ 0xFFFF, SEGG|SEGD|(0xF<<16)|SEGP|SEGPL(p)|SEGEXEC|SEGR }
#define	DATA32SEGM(p) 	{ 0xFFFF, SEGB|SEGG|(0xF<<16)|SEGP|SEGPL(p)|SEGDATA|SEGW }

Segdesc gdt[NGDT] =
{
[NULLSEG]	{ 0, 0},		/* null descriptor */
[KESEG]		EXECSEGM(0),		/* kernel code */
[KDSEG]		DATASEGM(0),		/* kernel data */
[UE32SEG]	EXEC32SEGM(3),		/* user code 32 bit*/
[UDSEG]		DATA32SEGM(3),		/* user data/stack */
[UESEG]		EXECSEGM(3),		/* user code */
};

static int didmmuinit = 0;

static struct {
	Lock;
	MMU	*free;

	int	nalloc;
	int	nfree;
} mmupool;

/* level */
enum {
	PML4E	= 2,
	PDPE	= 1,
	PDE	= 0,

	MAPBITS	= 8*sizeof(m->mmumap[0]),
};

static void
loadptr(u16int lim, uintptr off, void (*load)(void*))
{
	u64int b[2], *o;
	u16int *s;

	o = &b[1];
	s = ((u16int*)o)-1;

	*s = lim;
	*o = off;

	(*load)(s);
}

static void
taskswitch(uintptr stack)
{
	Tss *tss;

	tss = m->tss;
	tss->rsp0[0] = (u32int)stack;
	tss->rsp0[1] = stack >> 32;
	tss->rsp1[0] = (u32int)stack;
	tss->rsp1[1] = stack >> 32;
	tss->rsp2[0] = (u32int)stack;
	tss->rsp2[1] = stack >> 32;
	mmuflushtlb();
}

void
mmuinit(void)
{
	uintptr x;
	vlong v;
	int i;

	didmmuinit = 1;

	/* zap double map done by l.s */ 
	m->pml4[0] = 0;
	m->pml4[512] = 0;

	m->tss = mallocz(sizeof(Tss), 1);
	if(m->tss == nil)
		panic("mmuinit: no memory for Tss");
	m->tss->iomap = 0xDFFF;
	for(i=0; i<14; i+=2){
		x = (uintptr)m + MACHSIZE;
		m->tss->ist[i] = x;
		m->tss->ist[i+1] = x>>32;
	}

	/*
	 * We used to keep the GDT in the Mach structure, but it
	 * turns out that that slows down access to the rest of the
	 * page.  Since the Mach structure is accessed quite often,
	 * it pays off anywhere from a factor of 1.25 to 2 on real
	 * hardware to separate them (the AMDs are more sensitive
	 * than Intels in this regard).  Under VMware it pays off
	 * a factor of about 10 to 100.
	 */
	memmove(m->gdt, gdt, sizeof gdt);

	x = (uintptr)m->tss;
	m->gdt[TSSSEG+0].d0 = (x<<16)|(sizeof(Tss)-1);
	m->gdt[TSSSEG+0].d1 = (x&0xFF000000)|((x>>16)&0xFF)|SEGTSS|SEGPL(0)|SEGP;
	m->gdt[TSSSEG+1].d0 = x>>32;
	m->gdt[TSSSEG+1].d1 = 0;

	loadptr(sizeof(gdt)-1, (uintptr)m->gdt, lgdt);
	loadptr(sizeof(Segdesc)*512-1, (uintptr)IDTADDR, lidt);
	taskswitch((uintptr)m + MACHSIZE);
	ltr(TSSSEL);

	wrmsr(0xc0000100, 0ull);	/* 64 bit fsbase */
	wrmsr(0xc0000101, (uvlong)&machp[m->machno]);	/* 64 bit gsbase */
	wrmsr(0xc0000102, 0ull);	/* kernel gs base */

	/* enable syscall extension */
	rdmsr(0xc0000080, &v);
	v |= 1ull;
	wrmsr(0xc0000080, v);

	/* IA32_STAR */
	wrmsr(0xc0000081, ((uvlong)UE32SEL << 48) | ((uvlong)KESEL << 32));

	/* IA32_LSTAR */
	wrmsr(0xc0000082, (uvlong)syscallentry);

	/* SYSCALL flags mask */
	wrmsr(0xc0000084, 0x200);
}

/*
 * These could go back to being macros once the kernel is debugged,
 * but the extra checking is nice to have.
 */
void*
kaddr(uintptr pa)
{
	if(pa > (uintptr)-KZERO)
		panic("kaddr: pa=%#p pc=%#p", pa, getcallerpc(&pa));
	return (void*)(pa+KZERO);
}

uintptr
paddr(void *v)
{
	uintptr va;
	
	va = (uintptr)v;
	if(va >= KZERO)
		return va-KZERO;
	if(va >= VMAP)
		return va-(VMAP-(-KZERO));
	panic("paddr: va=%#p pc=%#p", va, getcallerpc(&v));
	return 0;
}

static MMU*
mmualloc(void)
{
	MMU *p;
	int i, n;

	p = m->mmufree;
	if(p != nil){
		m->mmufree = p->next;
		m->mmucount--;
	} else {
		lock(&mmupool);
		p = mmupool.free;
		if(p != nil){
			mmupool.free = p->next;
			mmupool.nfree--;
		} else {
			unlock(&mmupool);

			n = 256;
			p = malloc(n * sizeof(MMU));
			if(p == nil)
				panic("mmualloc: out of memory for MMU");
			p->page = mallocalign(n * PTSZ, BY2PG, 0, 0);
			if(p->page == nil)
				panic("mmualloc: out of memory for MMU pages");
			for(i=1; i<n; i++){
				p[i].page = p[i-1].page + (1<<PTSHIFT);
				p[i-1].next = &p[i];
			}

			lock(&mmupool);
			p[n-1].next = mmupool.free;
			mmupool.free = p->next;
			mmupool.nalloc += n;
			mmupool.nfree += n-1;
		}
		unlock(&mmupool);
	}
	p->next = nil;
	return p;
}

uintptr*
mmuwalk(uintptr* table, uintptr va, int level, int create)
{
	uintptr pte, *page;
	int i, x;
	MMU *p;

	x = PTLX(va, 3);
	for(i = 2; i >= level; i--){
		pte = table[x];
		if(pte & PTEVALID){
			if(pte & PTESIZE)
				return 0;
			table = KADDR(PPN(pte));
		} else {			
			if(!create)
				return 0;
			pte = PTEWRITE|PTEVALID;
			if(va < VMAP){
				if(va < TSTKTOP){
					pte |= PTEUSER;

					p = mmualloc();
					p->index = x;
					p->level = i;
					if(i == PML4E){
						if((p->next = up->mmuhead) == nil)
							up->mmutail = p;
						up->mmuhead = p;
						m->mmumap[p->index/MAPBITS] |= 1ull<<(p->index%MAPBITS);
					} else {
						up->mmutail->next = p;
						up->mmutail = p;
					}
					up->mmucount++;
				} else if(va >= KMAP && va < (KMAP+KMAPSIZE)) {
					p = mmualloc();
					p->index = x;
					p->level = i;
					if(i == PML4E){
						up->kmaptail = p;
						up->kmaphead = p;
					} else {
						up->kmaptail->next = p;
						up->kmaptail = p;
					}
					up->kmapcount++;
				} else
					return 0;
				page = p->page;
			} else if(didmmuinit) {
				page = mallocalign(PTSZ, BY2PG, 0, 0);
			} else
				page = rampage();
			memset(page, 0, PTSZ);
			table[x] = PADDR(page) | pte;
			table = page;
		}
		x = PTLX(va, i);
	}
	return &table[x];
}

static int
ptecount(uintptr va, int level)
{
	return (1<<PTSHIFT) - (va & PGLSZ(level+1)-1) / PGLSZ(level);
}

void
pmap(uintptr *pml4, uintptr pa, uintptr va, int size)
{
	uintptr *pte, *ptee, flags;
	int z, l;

	if((size <= 0) || va < VMAP)
		panic("pmap: pa=%#p va=%#p size=%d", pa, va, size);
	flags = pa;
	pa = PPN(pa);
	flags -= pa;
	if(va >= KZERO)
		flags |= PTEGLOBAL;
	while(size > 0){
		if(size >= PGLSZ(1) && (va % PGLSZ(1)) == 0)
			flags |= PTESIZE;
		l = (flags & PTESIZE) != 0;
		z = PGLSZ(l);
		pte = mmuwalk(pml4, va, l, 1);
		if(pte == 0){
			pte = mmuwalk(pml4, va, ++l, 0);
			if(pte && (*pte & PTESIZE)){
				flags |= PTESIZE;
				z = va & (PGLSZ(l)-1);
				va -= z;
				pa -= z;
				size += z;
				continue;
			}
			panic("pmap: pa=%#p va=%#p size=%d", pa, va, size);
		}
		ptee = pte + ptecount(va, l);
		while(size > 0 && pte < ptee){
			*pte++ = pa | flags;
			pa += z;
			va += z;
			size -= z;
		}
	}
}

static void
mmuzap(void)
{
	uintptr *pte;
	u64int w;
	int i, x;

	pte = m->pml4;
	pte[PTLX(KMAP, 3)] = 0;

	/* common case */
	pte[PTLX(UTZERO, 3)] = 0;
	pte[PTLX(TSTKTOP, 3)] = 0;
	m->mmumap[PTLX(UTZERO, 3)/MAPBITS] &= ~(1ull<<(PTLX(UTZERO, 3)%MAPBITS));
	m->mmumap[PTLX(TSTKTOP, 3)/MAPBITS] &= ~(1ull<<(PTLX(TSTKTOP, 3)%MAPBITS));

	for(i = 0; i < nelem(m->mmumap); pte += MAPBITS, i++){
		w = m->mmumap[i];
		if(w == 0)
			continue;
		x = 0;
		do {
			if(w & 1)
				pte[x] = 0;
			x++;
			x >>= 1;
		} while(w);
		m->mmumap[i] = 0;
	}
}

static void
mmufree(Proc *proc)
{
	MMU *p;

	p = proc->mmutail;
	if(p == nil)
		return;
	if(m->mmucount+proc->mmucount < 256){
		p->next = m->mmufree;
		m->mmufree = proc->mmuhead;
		m->mmucount += proc->mmucount;
	} else {
		lock(&mmupool);
		p->next = mmupool.free;
		mmupool.free = proc->mmuhead;
		mmupool.nfree += proc->mmucount;
		unlock(&mmupool);
	}
	proc->mmuhead = proc->mmutail = nil;
	proc->mmucount = 0;
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
mmuswitch(Proc *proc)
{
	MMU *p;

	mmuzap();
	if(proc->newtlb){
		mmufree(proc);
		proc->newtlb = 0;
	}
	if((p = proc->kmaphead) != nil)
		m->pml4[PTLX(KMAP, 3)] = PADDR(p->page) | PTEWRITE|PTEVALID;
	for(p = proc->mmuhead; p != nil && p->level == PML4E; p = p->next){
		m->mmumap[p->index/MAPBITS] |= 1ull<<(p->index%MAPBITS);
		m->pml4[p->index] = PADDR(p->page) | PTEUSER|PTEWRITE|PTEVALID;
	}
	taskswitch((uintptr)proc->kstack+KSTACK);
}

void
mmurelease(Proc *proc)
{
	MMU *p;

	mmuzap();
	if((p = proc->kmaptail) != nil){
		if((p->next = proc->mmuhead) == nil)
			proc->mmutail = p;
		proc->mmuhead = p;
		proc->mmucount += proc->kmapcount;

		proc->kmaphead = proc->kmaptail = nil;
		proc->kmapcount = proc->kmapindex = 0;
	}
	mmufree(proc);
	taskswitch((uintptr)m+MACHSIZE);
}

void
putmmu(uintptr va, uintptr pa, Page *)
{
	uintptr *pte, old;
	int x;

	x = splhi();
	pte = mmuwalk(m->pml4, va, 0, 1);
	if(pte == 0)
		panic("putmmu: bug: va=%#p pa=%#p", va, pa);
	old = *pte;
	*pte = pa | PTEVALID|PTEUSER;
	splx(x);
	if(old & PTEVALID)
		invlpg(va);
}

void
checkmmu(uintptr va, uintptr pa)
{
	USED(va, pa);
}

uintptr
cankaddr(uintptr pa)
{
	if(pa >= -KZERO)
		return 0;
	return -KZERO - pa;
}

void
countpagerefs(ulong *ref, int print)
{
	USED(ref, print);
}

KMap*
kmap(Page *page)
{
	uintptr *pte, pa, va;
	int x;

	pa = page->pa;
	if(cankaddr(pa) != 0)
		return (KMap*)KADDR(pa);

	x = splhi();
	va = KMAP + ((uintptr)up->kmapindex << PGSHIFT);
	pte = mmuwalk(m->pml4, va, 0, 1);
	if(pte == 0 || *pte & PTEVALID)
		panic("kmap: pa=%#p va=%#p", pa, va);
	*pte = pa | PTEWRITE|PTEVALID;
 	up->kmapindex = (up->kmapindex + 1) % (1<<PTSHIFT);
	if(up->kmapindex == 0)
		mmuflushtlb();
	splx(x);
	return (KMap*)va;
}

void
kunmap(KMap *k)
{
	uintptr *pte, va;
	int x;

	va = (uintptr)k;
	if(va >= KZERO)
		return;

	x = splhi();
	pte = mmuwalk(m->pml4, va, 0, 0);
	if(pte == 0 || (*pte & PTEVALID) == 0)
		panic("kunmap: va=%#p", va);
	*pte = 0;
	splx(x);
}

/*
 * Add a device mapping to the vmap range.
 */
void*
vmap(uintptr pa, int size)
{
	uintptr va;
	int o;

	if(size <= 0 || pa >= -VMAP)
		panic("vmap: pa=%#p size=%d pc=%#p", pa, size, getcallerpc(&pa));
	if(cankaddr(pa) >= size)
		va = pa+KZERO;
	else
		va = pa+(VMAP-(-KZERO));
	/*
	 * might be asking for less than a page.
	 */
	o = pa & (BY2PG-1);
	pa -= o;
	va -= o;
	size += o;
	pmap(m->pml4, pa | PTEUNCACHED|PTEWRITE|PTEVALID, va, size);
	return (void*)(va+o);
}

void
vunmap(void *v, int)
{
	paddr(v);	/* will panic on error */
}

/*
 * vmapsync() is currently unused as the VMAP and KZERO PDPs
 * are shared between processors. (see mpstartap)
 */
int
vmapsync(uintptr va)
{
	uintptr *pte1, *pte2;
	int level;

	if(va < VMAP || m->machno == 0)
		return 0;

	for(level=0; level<2; level++){
		pte1 = mmuwalk(MACHP(0)->pml4, va, level, 0);
		if(pte1 && *pte1 & PTEVALID){
			pte2 = mmuwalk(m->pml4, va, level, 1);
			if(pte2 == 0)
				break;
			if(pte1 != pte2)
				*pte2 = *pte1;
			return 1;
		}
	}
	return 0;
}
