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

static struct {
	Lock;
	MMU	*free;

	ulong	nalloc;
	ulong	nfree;
} mmupool;

enum {
	/* level */
	PML4E	= 2,
	PDPE	= 1,
	PDE	= 0,

	MAPBITS	= 8*sizeof(m->mmumap[0]),

	/* PAT entry used for write combining */
	PATWC	= 7,
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

static void kernelro(void);

void
mmuinit(void)
{
	uintptr x;
	vlong v;
	int i;

	/* zap double map done by l.s */ 
	m->pml4[512] = 0;
	m->pml4[0] = 0;

	if(m->machno == 0)
		kernelro();

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

	wrmsr(FSbase, 0ull);
	wrmsr(GSbase, (uvlong)&machp[m->machno]);
	wrmsr(KernelGSbase, 0ull);

	/* enable syscall extension */
	rdmsr(Efer, &v);
	v |= 1ull;
	wrmsr(Efer, v);

	wrmsr(Star, ((uvlong)UE32SEL << 48) | ((uvlong)KESEL << 32));
	wrmsr(Lstar, (uvlong)syscallentry);
	wrmsr(Sfmask, 0x200);

	/* IA32_PAT write combining */
	if((MACHP(0)->cpuiddx & Pat) != 0
	&& rdmsr(0x277, &v) != -1){
		v &= ~(255LL<<(PATWC*8));
		v |= 1LL<<(PATWC*8);	/* WC */
		wrmsr(0x277, v);
	}
}

/*
 * These could go back to being macros once the kernel is debugged,
 * but the extra checking is nice to have.
 */
void*
kaddr(uintptr pa)
{
	if(pa >= (uintptr)-KZERO)
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
		return va-VMAP;
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

static uintptr*
mmucreate(uintptr *table, uintptr va, int level, int index)
{
	uintptr *page, flags;
	MMU *p;
	
	flags = PTEWRITE|PTEVALID;
	if(va < VMAP){
		assert(up != nil);
		assert((va < USTKTOP) || (va >= KMAP && va < KMAP+KMAPSIZE));

		p = mmualloc();
		p->index = index;
		p->level = level;
		if(va < USTKTOP){
			flags |= PTEUSER;
			if(level == PML4E){
				if((p->next = up->mmuhead) == nil)
					up->mmutail = p;
				up->mmuhead = p;
				m->mmumap[index/MAPBITS] |= 1ull<<(index%MAPBITS);
			} else {
				up->mmutail->next = p;
				up->mmutail = p;
			}
			up->mmucount++;
		} else {
			if(level == PML4E){
				up->kmaptail = p;
				up->kmaphead = p;
			} else {
				up->kmaptail->next = p;
				up->kmaptail = p;
			}
			up->kmapcount++;
		}
		page = p->page;
	} else {
		page = rampage();
	}
	memset(page, 0, PTSZ);
	table[index] = PADDR(page) | flags;
	return page;
}

uintptr*
mmuwalk(uintptr* table, uintptr va, int level, int create)
{
	uintptr pte;
	int i, x;

	x = PTLX(va, 3);
	for(i = 2; i >= level; i--){
		pte = table[x];
		if(pte & PTEVALID){
			if(pte & PTESIZE)
				return 0;
			pte = PPN(pte);
			if(pte >= (uintptr)-KZERO)
				table = (void*)(pte + VMAP);
			else
				table = (void*)(pte + KZERO);
		} else {
			if(!create)
				return 0;
			table = mmucreate(table, va, i, x);
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

static void
ptesplit(uintptr* table, uintptr va)
{
	uintptr *pte, pa, off;

	pte = mmuwalk(table, va, 1, 0);
	if(pte == nil || (*pte & PTESIZE) == 0 || (va & PGLSZ(1)-1) == 0)
		return;
	table = rampage();
	va &= -PGLSZ(1);
	pa = *pte & ~PTESIZE;
	for(off = 0; off < PGLSZ(1); off += PGLSZ(0))
		table[PTLX(va + off, 0)] = pa + off;
	*pte = PADDR(table) | PTEVALID|PTEWRITE;
	invlpg(va);
}

/*
 * map kernel text segment readonly
 * and everything else no-execute.
 */
static void
kernelro(void)
{
	uintptr *pte, psz, va;

	ptesplit(m->pml4, APBOOTSTRAP);
	ptesplit(m->pml4, KTZERO);
	ptesplit(m->pml4, (uintptr)etext-1);

	for(va = KZERO; va != 0; va += psz){
		psz = PGLSZ(0);
		pte = mmuwalk(m->pml4, va, 0, 0);
		if(pte == nil){
			if(va & PGLSZ(1)-1)
				continue;
			pte = mmuwalk(m->pml4, va, 1, 0);
			if(pte == nil)
				continue;
			psz = PGLSZ(1);
		}
		if((*pte & PTEVALID) == 0)
			continue;
		if(va >= KTZERO && va < (uintptr)etext)
			*pte &= ~PTEWRITE;
		else if(va != (APBOOTSTRAP & -BY2PG))
			*pte |= PTENOEXEC;
		invlpg(va);
	}
}

void
pmap(uintptr pa, uintptr va, vlong size)
{
	uintptr *pte, *ptee, flags;
	int z, l;

	if(size <= 0 || va < VMAP)
		panic("pmap: pa=%#p va=%#p size=%lld", pa, va, size);
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
		pte = mmuwalk(m->pml4, va, l, 1);
		if(pte == nil){
			pte = mmuwalk(m->pml4, va, ++l, 0);
			if(pte && (*pte & PTESIZE)){
				flags |= PTESIZE;
				z = va & (PGLSZ(l)-1);
				va -= z;
				pa -= z;
				size += z;
				continue;
			}
			panic("pmap: pa=%#p va=%#p size=%lld", pa, va, size);
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

void
punmap(uintptr va, vlong size)
{
	uintptr *pte;
	int l;

	va = PPN(va);
	while(size > 0){
		if((va % PGLSZ(1)) != 0 || size < PGLSZ(1))
			ptesplit(m->pml4, va);
		l = 0;
		pte = mmuwalk(m->pml4, va, l, 0);
		if(pte == nil && (va % PGLSZ(1)) == 0 && size >= PGLSZ(1))
			pte = mmuwalk(m->pml4, va, ++l, 0);
		if(pte){
			*pte = 0;
			invlpg(va);
		}
		va += PGLSZ(l);
		size -= PGLSZ(l);
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
	pte[PTLX(USTKTOP-1, 3)] = 0;
	m->mmumap[PTLX(UTZERO, 3)/MAPBITS] &= ~(1ull<<(PTLX(UTZERO, 3)%MAPBITS));
	m->mmumap[PTLX(USTKTOP-1, 3)/MAPBITS] &= ~(1ull<<(PTLX(USTKTOP-1, 3)%MAPBITS));

	for(i = 0; i < nelem(m->mmumap); pte += MAPBITS, i++){
		if((w = m->mmumap[i]) == 0)
			continue;
		m->mmumap[i] = 0;
		for(x = 0; w != 0; w >>= 1, x++){
			if(w & 1)
				pte[x] = 0;
		}
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
		proc->mmuhead = proc->kmaphead;
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
	*pte = pa | PTEUSER;
	splx(x);
	if(old & PTEVALID)
		invlpg(va);
}

/*
 * Double-check the user MMU.
 * Error checking only.
 */
void
checkmmu(uintptr va, uintptr pa)
{
	uintptr *pte;

	pte = mmuwalk(m->pml4, va, 0, 0);
	if(pte != 0 && (*pte & PTEVALID) != 0 && PPN(*pte) != pa)
		print("%ld %s: va=%#p pa=%#p pte=%#p\n",
			up->pid, up->text, va, pa, *pte);
}

uintptr
cankaddr(uintptr pa)
{
	if(pa >= -KZERO)
		return 0;
	return -KZERO - pa;
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
	va = KMAP + (((uintptr)up->kmapindex++ << PGSHIFT) & (KMAPSIZE-1));
	pte = mmuwalk(m->pml4, va, 0, 1);
	if(pte == 0 || (*pte & PTEVALID) != 0)
		panic("kmap: pa=%#p va=%#p", pa, va);
	*pte = pa | PTEWRITE|PTENOEXEC|PTEVALID;
	splx(x);
	invlpg(va);
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
 * note that the VMAP and KZERO PDPs are shared
 * between processors (see mpstartap) so no
 * synchronization is being done.
 */
void*
vmap(uintptr pa, int size)
{
	uintptr va;
	int o;

	if(pa+size > VMAPSIZE)
		return 0;
	va = pa+VMAP;
	/*
	 * might be asking for less than a page.
	 */
	o = pa & (BY2PG-1);
	pa -= o;
	va -= o;
	size += o;
	pmap(pa | PTEUNCACHED|PTEWRITE|PTENOEXEC|PTEVALID, va, size);
	return (void*)(va+o);
}

void
vunmap(void *v, int)
{
	paddr(v);	/* will panic on error */
}

/*
 * mark pages as write combining (used for framebuffer)
 */
void
patwc(void *a, int n)
{
	uintptr *pte, mask, attr, va;
	int z, l;
	vlong v;

	/* check if pat is usable */
	if((MACHP(0)->cpuiddx & Pat) == 0
	|| rdmsr(0x277, &v) == -1
	|| ((v >> PATWC*8) & 7) != 1)
		return;

	/* set the bits for all pages in range */
	for(va = (uintptr)a; n > 0; n -= z, va += z){
		l = 0;
		pte = mmuwalk(m->pml4, va, l, 0);
		if(pte == 0)
			pte = mmuwalk(m->pml4, va, ++l, 0);
		if(pte == 0 || (*pte & PTEVALID) == 0)
			panic("patwc: va=%#p", va);
		z = PGLSZ(l);
		z -= va & (z-1);
		mask = l == 0 ? 3<<3 | 1<<7 : 3<<3 | 1<<12;
		attr = (((PATWC&3)<<3) | ((PATWC&4)<<5) | ((PATWC&4)<<10));
		*pte = (*pte & ~mask) | (attr & mask);
	}
}

/*
 * The palloc.pages array and mmupool can be a large chunk
 * out of the 2GB window above KZERO, so we allocate from
 * upages and map in the VMAP window before pageinit()
 */
void
preallocpages(void)
{
	Pallocmem *pm;
	uintptr va, base, top;
	vlong tsize, psize;
	ulong np, nt;
	int i;

	np = 0;
	for(i=0; i<nelem(palloc.mem); i++){
		pm = &palloc.mem[i];
		np += pm->npage;
	}
	nt = np / 50;	/* 2% for mmupool */
	np -= nt;

	nt = (uvlong)nt*BY2PG / (sizeof(MMU)+PTSZ);
	tsize = (uvlong)nt * (sizeof(MMU)+PTSZ);

	psize = (uvlong)np * BY2PG;
	psize += sizeof(Page) + BY2PG;
	psize = (psize / (sizeof(Page)+BY2PG)) * sizeof(Page);

	psize += tsize;
	psize = ROUND(psize, PGLSZ(1));

	for(i=0; i<nelem(palloc.mem); i++){
		pm = &palloc.mem[i];
		base = ROUND(pm->base, PGLSZ(1));
		top = pm->base + (uvlong)pm->npage * BY2PG;
		if((base + psize) <= VMAPSIZE && (vlong)(top - base) >= psize){
			pm->base = base + psize;
			pm->npage = (top - pm->base)/BY2PG;

			va = base + VMAP;
			pmap(base | PTEGLOBAL|PTEWRITE|PTENOEXEC|PTEVALID, va, psize);

			palloc.pages = (void*)(va + tsize);

			mmupool.nfree = mmupool.nalloc = nt;
			mmupool.free = (void*)(va + (uvlong)nt*PTSZ);
			for(i=0; i<nt; i++){
				mmupool.free[i].page = (uintptr*)va;
				mmupool.free[i].next = &mmupool.free[i+1];
				va += PTSZ;
			}
			mmupool.free[i-1].next = nil;

			break;
		}
	}
}
