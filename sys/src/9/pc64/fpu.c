#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

enum {
	CR4Osfxsr  = 1 << 9,
	CR4Oxmmex  = 1 << 10,
	CR4Oxsave  = 1 << 18,
};

void
fpuinit(void)
{
	uintptr cr4;
	ulong regs[4];

	m->fpsavesz = sizeof(FPsave); /* always enough to fit sse+avx */
	if((m->cpuiddx & (Sse|Fxsr)) == (Sse|Fxsr)){ /* have sse fp? */
		cr4 = getcr4() | CR4Osfxsr|CR4Oxmmex;
		putcr4(cr4);
		fpsave = fpssesave;
		fprestore = fpsserestore;

		if((m->cpuidcx & (Xsave|Avx)) == (Xsave|Avx) && getconf("*noavx") == nil){
			cr4 |= CR4Oxsave;
			putcr4(cr4);
			m->xcr0 = 7; /* x87, sse, avx */
			putxcr0(m->xcr0);
			fpsave = fpxsave;
			fprestore = fpxrestore;

			cpuid(0xd, 0, regs);
			m->fpsavesz = regs[1];

			cpuid(0xd, 1, regs);
			if(regs[0] & Xsaveopt)
				fpsave = fpxsaveopt;
			if(regs[0] & Xsaves){
				fpsave = fpxsaves;
				fprestore = fpxrestores;
			}
		}
	} else {
		fpsave = fpx87save;
		fprestore = fpx87restore;
	}
}
