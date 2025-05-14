#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include <tos.h>
#include "ureg.h"

#include "arm.h"

/*
 * A lot of this stuff doesn't belong here
 * but this is a convenient dumping ground for
 * later sorting into the appropriate buckets.
 */

/* Give enough context in the ureg to produce a kernel stack for
 * a sleeping process
 */
void
setkernur(Ureg* ureg, Proc* p)
{
	ureg->pc = p->sched.pc;
	ureg->sp = p->sched.sp+4;
	ureg->r14 = (uintptr)sched;
}

/*
 * called in sysfile.c
 */
void
evenaddr(uintptr addr)
{
	if(addr & 3){
		postnote(up, 1, "sys: odd address", NDebug);
		error(Ebadarg);
	}
}

/*
 *  return the userpc the last exception happened at
 */
uintptr
userpc(void)
{
	Ureg *ureg = up->dbgreg;
	return ureg->pc;
}

/* This routine must save the values of registers the user is not permitted
 * to write from devproc and then restore the saved values before returning.
 */
void
setregisters(Ureg* ureg, char* pureg, char* uva, int n)
{
	ulong v = ureg->psr;
	memmove(pureg, uva, n);
	ureg->psr = ureg->psr & ~(PsrMask|PsrDfiq|PsrDirq) | v & (PsrMask|PsrDfiq|PsrDirq);
}

/*
 *  setup stack and initial PC for a new kernel proc.  This is architecture
 *  dependent because of the starting stack location
 */
void
kprocchild(Proc *p, void (*entry)(void))
{
	p->sched.pc = (uintptr)entry;
	p->sched.sp = (uintptr)p;
}

/*
 *  pc output by dumpaproc
 */
uintptr
dbgpc(Proc* p)
{
	Ureg *ureg;

	ureg = p->dbgreg;
	if(ureg == 0)
		return 0;

	return ureg->pc;
}

/*
 *  set mach dependent process state for a new process
 */
void
procsetup(Proc* p)
{
	fpusysprocsetup(p);
}

void
procfork(Proc*)
{
}

/*
 *  Save the mach dependent part of the process state.
 */
void
procsave(Proc* p)
{
	fpuprocsave(p);
}

void
procrestore(Proc* p)
{
	fpuprocrestore(p);
}

int
userureg(Ureg* ureg)
{
	return (ureg->psr & PsrMask) == PsrMusr;
}

int
cas32(void* addr, u32int old, u32int new)
{
	int r, s;

	s = splhi();
	if(r = (*(u32int*)addr == old))
		*(u32int*)addr = new;
	splx(s);
	if (r)
		coherence();
	return r;
}
