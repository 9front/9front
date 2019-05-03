#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "ureg.h"
#include "sysreg.h"

/* libc */
extern ulong getfcr(void);
extern void setfcr(ulong fcr);
extern ulong getfsr(void);
extern void setfsr(ulong fsr);

void
fpuinit(void)
{
	fpoff();
}

void
fpon(void)
{
	syswr(CPACR_EL1, 3<<20);
}

void
fpoff(void)
{
	syswr(CPACR_EL1, 0<<20);
}

void
fpinit(void)
{
	fpon();
	setfcr(0);
	setfsr(0);
}

void
fpclear(void)
{
	fpoff();
}

void
fpsave(FPsave *p)
{
	p->control = getfcr();
	p->status = getfsr();
	fpsaveregs(p->regs);
	fpoff();
}

void
fprestore(FPsave *p)
{
	fpon();
	setfcr(p->control);
	setfsr(p->status);
	fploadregs(p->regs);
}

void
mathtrap(Ureg*)
{
	int s;

	if((up->fpstate & FPillegal) != 0){
		postnote(up, 1, "sys: floating point in note handler", NDebug);
		return;
	}
	switch(up->fpstate){
	case FPinit:
		s = splhi();
		fpinit();
		up->fpstate = FPactive;
		splx(s);
		break;
	case FPinactive:
		s = splhi();
		fprestore(up->fpsave);
		up->fpstate = FPactive;
		splx(s);
		break;
	case FPactive:
		postnote(up, 1, "sys: floating point error", NDebug);
		break;
	}
}
