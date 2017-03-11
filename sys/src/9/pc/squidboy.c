#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"

#include "mp.h"

static void
squidboy(Apic* apic)
{
//	iprint("Hello Squidboy\n");
	machinit();
	mmuinit();
	cpuidentify();
	cpuidprint();
	syncclock();
	active.machs[m->machno] = 1;
	apic->online = 1;
	lapicinit(apic);
	lapiconline();
	timersinit();
	fpoff();
	schedinit();
}

void
mpstartap(Apic* apic)
{
	ulong *apbootp, *pdb, *pte;
	Mach *mach, *mach0;
	int i, machno;
	uchar *p;

	mach0 = MACHP(0);

	/*
	 * Initialise the AP page-tables and Mach structure. The page-tables
	 * are the same as for the bootstrap processor with the exception of
	 * the PTE for the Mach structure.
	 * Xspanalloc will panic if an allocation can't be made.
	 */
	p = xspanalloc(4*BY2PG, BY2PG, 0);
	pdb = (ulong*)p;
	memmove(pdb, mach0->pdb, BY2PG);
	p += BY2PG;

	if((pte = mmuwalk(pdb, MACHADDR, 1, 0)) == nil)
		return;
	memmove(p, KADDR(PPN(*pte)), BY2PG);
	*pte = PADDR(p)|PTEWRITE|PTEVALID;
	if(mach0->havepge)
		*pte |= PTEGLOBAL;
	p += BY2PG;

	mach = (Mach*)p;
	if((pte = mmuwalk(pdb, MACHADDR, 2, 0)) == nil)
		return;
	*pte = PADDR(mach)|PTEWRITE|PTEVALID;
	if(mach0->havepge)
		*pte |= PTEGLOBAL;
	p += BY2PG;

	machno = apic->machno;
	MACHP(machno) = mach;
	mach->machno = machno;
	mach->pdb = pdb;
	mach->gdt = (Segdesc*)p;	/* filled by mmuinit */

	/*
	 * Tell the AP where its kernel vector and pdb are.
	 * The offsets are known in the AP bootstrap code.
	 */
	apbootp = (ulong*)(APBOOTSTRAP+0x08);
	*apbootp++ = (ulong)squidboy;	/* assembler jumps here eventually */
	*apbootp++ = PADDR(pdb);
	*apbootp = (ulong)apic;

	/*
	 * Universal Startup Algorithm.
	 */
	p = KADDR(0x467);		/* warm-reset vector */
	*p++ = PADDR(APBOOTSTRAP);
	*p++ = PADDR(APBOOTSTRAP)>>8;
	i = (PADDR(APBOOTSTRAP) & ~0xFFFF)/16;
	/* code assumes i==0 */
	if(i != 0)
		print("mp: bad APBOOTSTRAP\n");
	*p++ = i;
	*p = i>>8;
	coherence();

	nvramwrite(0x0F, 0x0A);		/* shutdown code: warm reset upon init ipi */
	lapicstartap(apic, PADDR(APBOOTSTRAP));
	for(i = 0; i < 100000; i++){
		if(arch->fastclock == tscticks)
			cycles(&m->tscticks);	/* for ap's syncclock(); */
		if(apic->online)
			break;
		delay(1);
	}
	nvramwrite(0x0F, 0x00);
}
