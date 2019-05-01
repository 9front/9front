#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"

int paemode;
uvlong *xenpdpt;	/* this needs to go in Mach for multiprocessor guest */

#define LOG(a)  
#define PUTMMULOG(a)
#define MFN(pa)		(patomfn[(pa)>>PGSHIFT])
#define	MAPPN(x)	(paemode? matopfn[*(uvlong*)(&x)>>PGSHIFT]<<PGSHIFT : matopfn[(x)>>PGSHIFT]<<PGSHIFT)

/* note: pdb must already be pinned */
static void
taskswitch(Page *pdb, ulong stack)
{
	HYPERVISOR_stack_switch(KDSEL, stack);
	mmuflushtlb(pdb);
}

void
mmuflushtlb(Page *pdb)
{
	int s, i;

	if(!paemode){
		if(pdb)
			xenptswitch(pdb->pa);
		else
			xenptswitch(PADDR(m->pdb));
	}else{
		if(pdb){
			s = splhi();
			for(i = 0; i < 3; i++){
				xenupdate((ulong*)&xenpdpt[i], pdb->pa | PTEVALID);
				pdb = pdb->next;
			}
			splx(s);
		}else{
			s = splhi();
			for(i = 0; i < 3; i++)
				xenupdatema((ulong*)&xenpdpt[i], ((uvlong*)m->pdb)[i]);
			splx(s);
		}
		xentlbflush();
	}
}

/* 
 * On processors that support it, we set the PTEGLOBAL bit in
 * page table and page directory entries that map kernel memory.
 * Doing this tells the processor not to bother flushing them
 * from the TLB when doing the TLB flush associated with a 
 * context switch (write to CR3).  Since kernel memory mappings
 * are never removed, this is safe.  (If we ever remove kernel memory
 * mappings, we can do a full flush by turning off the PGE bit in CR4,
 * writing to CR3, and then turning the PGE bit back on.) 
 *
 * See also mmukmap below.
 * 
 * Processor support for the PTEGLOBAL bit is enabled in devarch.c.
 */
static void
memglobal(void)
{
	int i, j;
	ulong *pde, *pte;

	/* only need to do this once, on bootstrap processor */
	if(m->machno != 0)
		return;

	if(!m->havepge)
		return;

	pde = m->pdb;
	for(i=512; i<1024; i++){	/* 512: start at entry for virtual 0x80000000 */
		if(pde[i] & PTEVALID){
			pde[i] |= PTEGLOBAL;
			if(!(pde[i] & PTESIZE)){
				pte = KADDR(pde[i]&~(BY2PG-1));
				for(j=0; j<1024; j++)
					if(pte[j] & PTEVALID)
						pte[j] |= PTEGLOBAL;
			}
		}
	}			
}

ulong
mmumapframe(ulong va, ulong mfn)
{
	ulong *pte, pdbx;
	uvlong ma;

	/* 
	 * map machine frame number to a virtual address.
	 * When called the pagedir and page table exist, we just
	 * need to fill in a page table entry.
	 */
	ma = ((uvlong)mfn<<PGSHIFT) | PTEVALID|PTEWRITE;
	pdbx = PDX(va);
	pte = KADDR(MAPPN(PDB(m->pdb,va)[pdbx]));
	xenupdatema(&pte[PTX(va)], ma);
	return va;
}

void
mmumapcpu0(void)
{
	ulong *pdb, *pte, va, pa, pdbx;

	if(strstr(xenstart->magic, "x86_32p"))
		paemode = 1;
	hypervisor_virt_start = paemode ? 0xF5800000 : 0xFC000000;
	patomfn = (ulong*)xenstart->mfn_list;
	matopfn = (ulong*)hypervisor_virt_start;
	/* Xen bug ? can't touch top entry in PDPT */
	if(paemode)
		hypervisor_virt_start = 0xC0000000;

	/* 
	 * map CPU0MACH at MACHADDR.
	 * When called the pagedir and page table exist, we just
	 * need to fill in a page table entry.
	 */
	pdb = (ulong*)xenstart->pt_base;
	va = MACHADDR;
	pa = PADDR(CPU0MACH) | PTEVALID|PTEWRITE;
	pdbx = PDX(va);
	pdb = PDB(pdb, va);
	pte = KADDR(MAPPN(pdb[pdbx]));
	xenupdate(&pte[PTX(va)], pa);
}

void
mmuinit(void)
{
	ulong *pte, npgs, pa;

	if(paemode){
		int i;
		xenpdpt = (uvlong*)m->pdb;
		m->pdb = xspanalloc(32, 32, 0);
		/* clear "reserved" bits in initial page directory pointers -- Xen bug? */
		for(i = 0; i < 4; i++)
			((uvlong*)m->pdb)[i] = xenpdpt[i] & ~0x1E6LL;
	}

	/* 
	 * So far only memory up to xentop is mapped, map the rest.
	 * We cant use large pages because our contiguous PA space
	 * is not necessarily contiguous in MA.
	 */
	npgs = conf.mem[0].npage;
	for(pa=conf.mem[0].base; npgs; npgs--, pa+=BY2PG) {
		pte = mmuwalk(m->pdb, (ulong)KADDR(pa), 2, 1);
		if(!pte)
			panic("mmuinit");
		xenupdate(pte, pa|PTEVALID|PTEWRITE);
	}

	memglobal();

#ifdef we_may_eventually_want_this
	/* make kernel text unwritable */
	for(x = KTZERO; x < (ulong)etext; x += BY2PG){
		p = mmuwalk(m->pdb, x, 2, 0);
		if(p == nil)
			panic("mmuinit");
		*p &= ~PTEWRITE;
	}
#endif

	taskswitch(0,  (ulong)m + BY2PG);
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

static ulong*
mmupdb(Page *pg, ulong va)
{
	int i;

	for(i = PAX(va); i > 0; i -= 2)
		pg = pg->next;
	return (ulong*)pg->va;
}
	
/* this can be called with an active pdb, so use Xen calls to zero it out.
  */
static void
mmuptefree(Proc* proc)
{
	ulong *pdb, va;
	Page **last, *page;

	if(proc->mmupdb && proc->mmuused){
		last = &proc->mmuused;
		for(page = *last; page; page = page->next){
			/* this is no longer a pte page so make it readwrite */
			va = page->daddr;
			pdb = mmupdb(proc->mmupdb, va);
			xenupdatema(&pdb[PDX(va)], 0);
			xenptunpin(page->va);
			last = &page->next;
		}
		*last = proc->mmufree;
		proc->mmufree = proc->mmuused;
		proc->mmuused = 0;
	}
}

void
mmuswitch(Proc* proc)
{
	//ulong *pdb;

	if(proc->newtlb){
		mmuptefree(proc);
		proc->newtlb = 0;
	}

	if(proc->mmupdb){
		//XXX doesn't work for some reason, but it's not needed for uniprocessor
		//pdb = (ulong*)proc->mmupdb->va;
		//xenupdate(&pdb[PDX(MACHADDR)], m->pdb[PDX(MACHADDR)]);
		taskswitch(proc->mmupdb, (ulong)(proc->kstack+KSTACK));
	}
	else
		taskswitch(0, (ulong)(proc->kstack+KSTACK));
}

void
mmurelease(Proc* proc)
{
	Page *page, *next;

	/*
	 * Release any pages allocated for a page directory base or page-tables
	 * for this process:
	 *   switch to the prototype pdb for this processor (m->pdb);
	 *   call mmuptefree() to place all pages used for page-tables (proc->mmuused)
	 *   onto the process' free list (proc->mmufree). This has the side-effect of
	 *   cleaning any user entries in the pdb (proc->mmupdb);
	 *   if there's a pdb put it in the cache of pre-initialised pdb's
	 *   for this processor (m->pdbpool) or on the process' free list;
	 *   finally, place any pages freed back into the free pool (palloc).
	 * This routine is only called from sched() with palloc locked.
	 */
	taskswitch(0, (ulong)m + BY2PG);
	mmuptefree(proc);

	if((page = proc->mmupdb) != 0){
		proc->mmupdb = 0;
		while(page){
			next = page->next;
			/* its not a page table anymore, mark it rw */
			xenptunpin(page->va);
			if(paemode || m->pdbcnt > 10){
				page->next = proc->mmufree;
				proc->mmufree = page;
			}
			else{
				page->next = m->pdbpool;
				m->pdbpool = page;
				m->pdbcnt++;
			}
			page = next;
		}
	}

	for(page = proc->mmufree; page; page = next){
		next = page->next;
		if(--page->ref)
			panic("mmurelease: page->ref %ld\n", page->ref);
		pagechainhead(page);
	}
	if(proc->mmufree)
		pagechaindone();
	proc->mmufree = 0;
}

static Page*
mmupdballoc(ulong va, void *mpdb)
{
	int s;
	Page *page;
	Page *badpages, *pg;

	s = splhi();
	/*
	 * All page tables must be read-only.  We will mark them
	 * readwrite later when we free them and they are no
	 * longer used as page tables.
	 */
	if(m->pdbpool == 0){
		spllo();
		badpages = 0;
		for (;;) {
			page = newpage(0, 0, 0);
			page->va = VA(kmap(page));
			if(mpdb)
				memmove((void*)page->va, mpdb, BY2PG);
			else
				memset((void*)page->va, 0, BY2PG);
			if (xenpgdpin(page->va))
				break;
			/*
			 * XXX Plan 9 is a bit lax about putting pages on the free list when they are
			 * still mapped (r/w) by some process's page table.  From Plan 9's point
			 * of view this is safe because the any such process will have up->newtlb set,
			 * so the mapping will be cleared before the process is dispatched.  But the Xen
			 * hypervisor has no way of knowing this, so it refuses to pin the page for use
			 * as a pagetable.
			 */
			if(0) print("bad pgdpin %lux va %lux copy %lux %s\n", MFN(PADDR(page->va)), va, (ulong)mpdb, up? up->text: "");
			page->next = badpages;
			badpages = page;
		}
		while (badpages != 0) {
			pg = badpages;
			badpages = badpages->next;
			putpage(pg);
		}
	}
	else{
		page = m->pdbpool;
		m->pdbpool = page->next;
		m->pdbcnt--;
		if (!xenpgdpin(page->va))
			panic("xenpgdpin");
	}
	splx(s);

	page->next = 0;
	return page;
}

void
checkmmu(ulong va, ulong pa)
{
	ulong *pdb, *pte;
	int pdbx;
	
	if(up->mmupdb == 0)
		return;

	pdb = mmupdb(up->mmupdb, va);
	pdbx = PDX(va);
	if(MAPPN(pdb[pdbx]) == 0){
		/* okay to be empty - will fault and get filled */
		return;
	}
	
	pte = KADDR(MAPPN(pdb[pdbx]));
	if(MAPPN(pte[PTX(va)]) != pa){
		if(!paemode)
		  print("%ld %s: va=0x%08lux pa=0x%08lux pte=0x%08lux (0x%08lux)\n",
			up->pid, up->text,
			va, pa, pte[PTX(va)], MAPPN(pte[PTX(va)]));
		else
		  print("%ld %s: va=0x%08lux pa=0x%08lux pte=0x%16llux (0x%08lux)\n",
			up->pid, up->text,
			va, pa, *(uvlong*)&pte[PTX(va)], MAPPN(pte[PTX(va)]));
	}
}

void
putmmu(ulong va, ulong pa, Page*)
{
	int pdbx;
	Page *page;
	Page *badpages, *pg;
	ulong *pdb, *pte;
	int i, s;

	PUTMMULOG(dprint("putmmu va %lux pa %lux\n", va, pa);)
	if(up->mmupdb == 0){
		if(!paemode)
			up->mmupdb = mmupdballoc(va, m->pdb);
		else {
			page = 0;
			for(i = 4; i >= 0; i -= 2){
				if(m->pdb[i])
					pg = mmupdballoc(va, KADDR(MAPPN(m->pdb[i])));
				else
					pg = mmupdballoc(va, 0);
				pg->next = page;
				page = pg;
			}
			up->mmupdb = page;
		}
	}
	pdb = mmupdb(up->mmupdb, va);
	pdbx = PDX(va);

	if(PPN(pdb[pdbx]) == 0){
		PUTMMULOG(dprint("new pt page for index %d pdb %lux\n", pdbx, (ulong)pdb);)
		/* mark page as readonly before using as a page table */
		if(up->mmufree == 0){
			badpages = 0;
			for (;;) {
				page = newpage(1, 0, 0);
				page->va = VA(kmap(page));
				if (xenptpin(page->va))
					break;
				if(0) print("bad pin %lux va %lux %s\n", MFN(PADDR(page->va)), va, up->text);
				page->next = badpages;
				badpages = page;
			}
			while (badpages != 0) {
				pg = badpages;
				badpages = badpages->next;
				putpage(pg);
			}
		}
		else {
			page = up->mmufree;
			up->mmufree = page->next;
			memset((void*)page->va, 0, BY2PG);
			if (!xenptpin(page->va))
				panic("xenptpin");
		}

		xenupdate(&pdb[pdbx], page->pa|PTEVALID|PTEUSER|PTEWRITE);

		page->daddr = va;
		page->next = up->mmuused;
		up->mmuused = page;
	}

	pte = KADDR(MAPPN(pdb[pdbx]));
	PUTMMULOG(dprint("pte %lux index %lud old %lux new %lux mfn %lux\n", (ulong)pte, PTX(va), pte[PTX(va)], pa|PTEUSER, MFN(pa));)
	xenupdate(&pte[PTX(va)], pa|PTEUSER);

	s = splhi();
	//XXX doesn't work for some reason, but it's not needed for uniprocessor
	//xenupdate(&pdb[PDX(MACHADDR)], m->pdb[PDX(MACHADDR)]);
	mmuflushtlb(up->mmupdb);
	splx(s);
}

ulong*
mmuwalk(ulong* pdb, ulong va, int level, int create)
{
	ulong pa, va2, *table;

	/*
	 * Walk the page-table pointed to by pdb and return a pointer
	 * to the entry for virtual address va at the requested level.
	 * If the entry is invalid and create isn't requested then bail
	 * out early. Otherwise, for the 2nd level walk, allocate a new
	 * page-table page and register it in the 1st level.
	 */
	if(paemode){
		pdb = &pdb[PAX(va)];
		if(!(*pdb & PTEVALID)){
			if(create == 0)
				return 0;
			panic("mmuwalk: missing pgdir ptr for va=%lux\n", va);
		}
		pdb = KADDR(MAPPN(*pdb));
	}
	table = &pdb[PDX(va)];
	if(!(*table & PTEVALID) && create == 0)
		return 0;

	switch(level){

	default:
		return 0;

	case 1:
		return table;

	case 2:
		if(*table & PTESIZE)
			panic("mmuwalk2: va %luX entry %luX\n", va, *table);
		if(!(*table & PTEVALID)){
			va2 = (ulong)xspanalloc(BY2PG, BY2PG, 0);
			pa = PADDR(va2);
			xenptpin(va2);
			xenupdate(table, pa|PTEWRITE|PTEVALID);
		}
		table = KADDR(MAPPN(*table));

		return &table[PTX(va)];
	}
}

int
mmukmapsync(ulong va)
{
	USED(va);
	return 0;
}

/*
 * Return the number of bytes that can be accessed via KADDR(pa).
 * If pa is not a valid argument to KADDR, return 0.
 */
ulong
cankaddr(ulong pa)
{
	if(pa >= -KZERO)
		return 0;
	return -KZERO - pa;
}
