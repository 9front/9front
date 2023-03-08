/*
 * Machine specific stuff
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "tos.h"

#include "ureg.h"


/*
 * Some of these functions are expected by the 
 * port code, but might need to be implement in 
 * ways specific to the achitecture
 */

void
idlehands(void)
{
	idle();
}


/*
 * called in sysfile.c
 */
void
evenaddr(uintptr va)
{
	if((va & 3) != 0){
		dumpstack();
		postnote(up, 1, "sys: odd address", NDebug);
		error(Ebadarg);
	}
}

void
procsetup(Proc *p)
{
	p->fpstate = FPinit;
	p->fpsave->fpstatus = initfp.fpstatus;

//	memmove(p->fpsave, &initfp, sizeof(FPsave));
}


void
procfork(Proc*)
{
// stub
}

void
procsave(Proc*)
{
// stub
}

void
procrestore(Proc*)
{
}


ulong
userpc(void)
{
	Ureg *ur;

	ur = (Ureg*)up->dbgreg;
	return ur->pc;
}


/*
 * This routine must save the values of registers the user is not
 * permitted to write from devproc and then restore the saved values
 * before returning
 */
void
setregisters(Ureg *xp, char *pureg, char *uva, int n)
{
	ulong status, r27;

	r27 = xp->r27;			/* return PC for GEVector() */
	status = xp->status;
	memmove(pureg, uva, n);
	xp->r27 = r27;
	xp->status = status;
}

/*
 * Give enough context in the ureg to produce a kernel stack for
 * a sleeping process
 */
void
setkernur(Ureg *xp, Proc *p)
{
	xp->pc = p->sched.pc;
	xp->sp = p->sched.sp;
	xp->r24 = (ulong)p;		/* up */
	xp->r31 = (ulong)sched;
}

ulong
dbgpc(Proc *p)
{
	Ureg *ur;

	ur = p->dbgreg;
	if(ur == 0)
		return 0;

	return ur->pc;
}

void
kprocchild(Proc *p, void (*entry)(void))
{
	p->sched.pc = (ulong)entry;
	p->sched.sp = (ulong)p;
}
