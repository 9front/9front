#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#define INITMAP	(ROUND((uintptr)end + BY2PG, PGLSZ(1))-KZERO)

/*
 * Create initial identity map in top-level page table
 * (L1BOT) for TTBR0. This page table is only used until
 * mmu1init() loads m->mmutop.
 */
void
mmuidmap(uintptr *l1bot)
{
	uintptr pa, pe, attr;

	attr = PTEWRITE | PTEAF | PTEKERNEL | PTEUXN | PTESH(SHARE_INNER);
	pe = -KZERO;
	for(pa = PHYSDRAM; pa < pe; pa += PGLSZ(PTLEVELS-1))
		l1bot[PTLX(pa, PTLEVELS-1)] = pa | PTEVALID | PTEBLOCK | attr;
}

/*
 * Create initial shared kernel page table (L1) for TTBR1.
 * This page table coveres the KZERO and VIRTIO.
 */
void
mmu0init(uintptr *l1)
{
	uintptr va, pa, pe, attr;

	/* KZERO */
	attr = PTEWRITE | PTEAF | PTEKERNEL | PTEUXN | PTESH(SHARE_INNER);
	pe = -KZERO;
	for(pa = PHYSDRAM, va = KZERO; pa < pe; pa += PGLSZ(1), va += PGLSZ(1))
		l1[PTL1X(va, 1)] = pa | PTEVALID | PTEBLOCK | attr;

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
	for(va = INITMAP+KZERO; va != 0; va += PGLSZ(1))
		((uintptr*)L1)[PTL1X(va, 1)] = 0;
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
