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

static FPsave fpsave0;

static void
fpsave(FPsave *p)
{
	p->control = getfcr();
	p->status = getfsr();
	fpsaveregs(p->regs);
	fpoff();
}

static void
fprestore(FPsave *p)
{
	fpon();
	setfcr(p->control);
	setfsr(p->status);
	fploadregs(p->regs);
}

static void
fpinit(void)
{
	fprestore(&fpsave0);
}

void
fpuinit(void)
{
	m->fpstate = FPinit;
	m->fpsave = nil;
	fpoff();
}

static FPsave*
fpalloc(void)
{
	FPsave *save;

	while((save = mallocalign(sizeof(FPsave), 16, 0, 0)) == nil){
		spllo();
		resrcwait("no memory for FPsave");
		splhi();
	}
	return save;
}

static void
fpfree(FPsave *save)
{
	free(save);
}


/*
 *  Protect or save FPU state and setup new state
 *  (lazily in the case of user process) for the kernel.
 *  All syscalls, traps and interrupts (except mathtrap()!)
 *  are handled between fpukenter() and fpukexit(),
 *  so they can use floating point and vector instructions.
 */
FPsave*
fpukenter(Ureg*)
{
	if(up == nil){
		switch(m->fpstate){
		case FPactive:
			fpsave(m->fpsave);
			/* wet floor */
		case FPinactive:
			m->fpstate = FPinit;
			return m->fpsave;
		}
		return nil;
	}

	switch(up->fpstate){
	case FPactive:
		up->fpstate = FPprotected;
		fpoff();
		/* wet floor */
	case FPprotected:
		return nil;
	}

	switch(up->kfpstate){
	case FPactive:
		fpsave(up->kfpsave);
		/* wet floor */
	case FPinactive:
		up->kfpstate = FPinit;
		return up->kfpsave;
	}
	return nil;
}

void
fpukexit(Ureg *ureg, FPsave *save)
{
	if(up == nil){
		switch(m->fpstate){
		case FPactive:
			fpoff();
			/* wet floor */
		case FPinactive:
			fpfree(m->fpsave);
			m->fpstate = FPinit;
		}
		m->fpsave = save;
		if(save != nil)
			m->fpstate = FPinactive;
		return;
	}

	if(up->fpstate == FPprotected){
		if(userureg(ureg)){
			up->fpstate = FPactive;
			fpon();
		}
		return;
	}

	switch(up->kfpstate){
	case FPactive:
		fpoff();
		/* wet floor */
	case FPinactive:
		fpfree(up->kfpsave);
		up->kfpstate = FPinit;
	}
	up->kfpsave = save;
	if(save != nil)
		up->kfpstate = FPinactive;
}

void
fpuprocsetup(Proc *p)
{
	p->fpstate = FPinit;
}

void
fpuprocfork(Proc *p)
{
	int s;

	s = splhi();
	switch(up->fpstate & ~FPillegal){
	case FPprotected:
		fpon();
		/* wet floor */
	case FPactive:
		fpsave(up->fpsave);
		up->fpstate = FPinactive;
		/* wet floor */
	case FPinactive:
		if(p->fpsave == nil)
			p->fpsave = fpalloc();
		memmove(p->fpsave, up->fpsave, sizeof(FPsave));
		p->fpstate = FPinactive;
	}
	splx(s);
}

void
fpuprocsave(Proc *p)
{
	if(p->state == Moribund){
		if(p->fpstate == FPactive || p->kfpstate == FPactive)
			fpoff();
		fpfree(p->fpsave);
		fpfree(p->kfpsave);
		p->fpsave = p->kfpsave = nil;
		p->fpstate = p->kfpstate = FPinit;
		return;
	}
	if(p->kfpstate == FPactive){
		fpsave(p->kfpsave);
		p->kfpstate = FPinactive;
		return;
	}
	if(p->fpstate == FPprotected)
		fpon();
	else if(p->fpstate != FPactive)
		return;
	fpsave(p->fpsave);
	p->fpstate = FPinactive;
}

void
fpuprocrestore(Proc*)
{
	/*
	 * when the scheduler switches,
	 * we can discard its fp state.
	 */
	switch(m->fpstate){
	case FPactive:
		fpoff();
		/* wet floor */
	case FPinactive:
		fpfree(m->fpsave);
		m->fpsave = nil;
		m->fpstate = FPinit;
	}
}

void
mathtrap(Ureg *ureg)
{
	if(!userureg(ureg)){
		if(up == nil){
			switch(m->fpstate){
			case FPinit:
				m->fpsave = fpalloc();
				m->fpstate = FPactive;
				fpinit();
				break;
			case FPinactive:
				fprestore(m->fpsave);
				m->fpstate = FPactive;
				break;
			default:
				panic("floating point error in irq");
			}
			return;
		}

		if(up->fpstate == FPprotected){
			fpon();
			fpsave(up->fpsave);
			up->fpstate = FPinactive;
		}

		switch(up->kfpstate){
		case FPinit:
			up->kfpsave = fpalloc();
			up->kfpstate = FPactive;
			fpinit();
			break;
		case FPinactive:
			fprestore(up->kfpsave);
			up->kfpstate = FPactive;
			break;
		default:
			panic("floating point error in trap");
		}
		return;
	}

	if(up->fpstate & FPillegal){
		postnote(up, 1, "sys: floating point in note handler", NDebug);
		return;
	}
	switch(up->fpstate){
	case FPinit:
		if(up->fpsave == nil)
			up->fpsave = fpalloc();
		up->fpstate = FPactive;
		fpinit();
		break;
	case FPinactive:
		fprestore(up->fpsave);
		up->fpstate = FPactive;
		break;
	case FPprotected:
		up->fpstate = FPactive;
		fpon();
		break;
	case FPactive:
		postnote(up, 1, "sys: floating point error", NDebug);
		break;
	}
}
