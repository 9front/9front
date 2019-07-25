#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "ureg.h"

/*
 * GISB arbiter registers
 */
static u32int *regs = (u32int*)(VIRTIO2 + 0x400000);

enum {
	ArbTimer	= 0x008/4,
	ArbErrCapClear	= 0x7e4/4,
	ArbErrCapAddrHi	= 0x7e8/4,
	ArbErrCapAddr	= 0x7ec/4,
	ArbErrCapStatus	= 0x7f4/4,
		CapStatusTimeout	= 1<<12,
		CapStatusAbort		= 1<<11,
		CapStatusWrite		= 1<<1,
		CapStatusValid		= 1<<0,
	ArbErrCapMaster	= 0x7f8/4,
};

static int
arbinterrupt(Ureg *)
{
	u32int status = regs[ArbErrCapStatus];
	u32int master;
	uvlong addr;

	if((status & CapStatusValid) == 0)
		return 0;

	master = regs[ArbErrCapMaster];

	addr = regs[ArbErrCapAddr];
	addr |= (uvlong)regs[ArbErrCapAddrHi]<<32;

	regs[ArbErrCapClear] = CapStatusValid;

	iprint("cpu%d: GISB arbiter error: %s%s %s bus addr %llux, master %.8ux\n",
		m->machno,
		(status & CapStatusTimeout) ? "timeout" : "",
		(status & CapStatusAbort) ? "abort" : "",
		(status & CapStatusWrite) ? "writing" : "reading",
		addr,
		master);

	return 1;
}

void
gisblink(void)
{
	extern int (*buserror)(Ureg*);	// trap.c

	regs[ArbErrCapClear] = CapStatusValid;

	buserror = arbinterrupt;
}
