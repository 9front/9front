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

	/* VDRAM */
	attr = PTEWRITE | PTEAF | PTEKERNEL | PTEUXN | PTESH(SHARE_INNER);
	pe = -KZERO;
	for(pa = VDRAM - KZERO; pa < pe; pa += PGLSZ(PTLEVELS-1))
		l1bot[PTLX(pa, PTLEVELS-1)] = pa | PTEVALID | PTEBLOCK | attr;
}

/*
 * Create initial shared kernel page table (L1) for TTBR1.
 * This page table coveres the INITMAP and VIRTIO,
 * and later we fill the ram mappings in meminit().
 */
void
mmu0init(uintptr *l1)
{
	uintptr va, pa, pe, attr;

	/* DRAM - INITMAP */
	attr = PTEWRITE | PTEAF | PTEKERNEL | PTEUXN | PTESH(SHARE_INNER);
	pe = INITMAP;
	for(pa = VDRAM - KZERO, va = VDRAM; pa < pe; pa += PGLSZ(1), va += PGLSZ(1))
		l1[PTL1X(va, 1)] = pa | PTEVALID | PTEBLOCK | attr;

	/* VIRTIO */
	attr = PTEWRITE | PTEAF | PTEKERNEL | PTEUXN | PTEPXN | PTESH(SHARE_OUTER) | PTEDEVICE;
	pe = PHYSIOEND;
	for(pa = PHYSIO, va = VIRTIO; pa < pe; pa += PGLSZ(1), va += PGLSZ(1)){
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
	char *p;
	uintptr l = GiB + 128 * MiB;

	if(p = getconf("*maxmem"))
		l = strtoull(p, 0, 0);
	conf.mem[0].base = PGROUND((uintptr)end - KZERO);
	conf.mem[0].limit = l;

	if(l > KLIMIT)
		l = KLIMIT;
	kmapram(conf.mem[0].base, l);

	conf.mem[0].npage = (conf.mem[0].limit - conf.mem[0].base)/BY2PG;
}
