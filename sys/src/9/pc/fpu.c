#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

enum {
	CR4Osfxsr  = 1 << 9,
	CR4Oxmmex  = 1 << 10,
};

void
putxcr0(ulong)
{
}

void
fpuinit(void)
{
	uintptr cr4;

	if((m->cpuiddx & (Sse|Fxsr)) == (Sse|Fxsr)){ /* have sse fp? */
		fpsave = fpssesave;
		fprestore = fpsserestore;
		cr4 = getcr4() | CR4Osfxsr|CR4Oxmmex;
		putcr4(cr4);
	} else {
		fpsave = fpx87save;
		fprestore = fpx87restore;
	}
}
