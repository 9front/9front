#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "ureg.h"
#include "io.h"

enum {
	CR4Osfxsr  = 1 << 9,
	CR4Oxmmex  = 1 << 10,
	CR4Oxsave  = 1 << 18,
};

/*
 * SIMD Floating Point.
 * Assembler support to get at the individual instructions
 * is in l.s.
 */
extern void _clts(void);
extern void _fldcw(u16int);
extern void _fnclex(void);
extern void _fninit(void);
extern void _fxrstor(void*);
extern void _fxsave(void*);
extern void _xrstor(void*);
extern void _xsave(void*);
extern void _xsaveopt(void*);
extern void _fwait(void);
extern void _ldmxcsr(u32int);
extern void _stts(void);

static void mathemu(Ureg *ureg, void*);

static void
fpssesave(FPsave *s)
{
	_fxsave(s);
	_stts();
}
static void
fpsserestore(FPsave *s)
{
	_clts();
	_fxrstor(s);
}

static void
fpxsave(FPsave *s)
{
	_xsave(s);
	_stts();
}
static void
fpxrestore(FPsave *s)
{
	_clts();
	_xrstor(s);
}

static void
fpxsaves(FPsave *s)
{
	_xsaveopt(s);
	_stts();
}
static void
fpxrestores(FPsave *s)
{
	_clts();
	_xrstor(s);
}

static void
fpxsaveopt(FPsave *s)
{
	_xsaveopt(s);
	_stts();
}

/*
 *  Turn the FPU on and initialise it for use.
 *  Set the precision and mask the exceptions
 *  we don't care about from the generic Mach value.
 */
void
fpinit(void)
{
	_clts();
	_fninit();
	_fwait();
	_fldcw(0x0232);
	_ldmxcsr(0x1900);
}

static char* mathmsg[] =
{
	nil,	/* handled below */
	"denormalized operand",
	"division by zero",
	"numeric overflow",
	"numeric underflow",
	"precision loss",
};

static void
mathnote(ulong status, uintptr pc, int kernel)
{
	char *msg, note[ERRMAX];
	int i;

	/*
	 * Some attention should probably be paid here to the
	 * exception masks and error summary.
	 */
	msg = "unknown exception";
	for(i = 1; i <= 5; i++){
		if(!((1<<i) & status))
			continue;
		msg = mathmsg[i];
		break;
	}
	if(status & 0x01){
		if(status & 0x40){
			if(status & 0x200)
				msg = "stack overflow";
			else
				msg = "stack underflow";
		}else
			msg = "invalid operation";
	}
	snprint(note, sizeof note, "sys: fp: %s fppc=%#p status=0x%lux", msg, pc, status);
	if(kernel)
		panic("%s", note);

	postnote(up, 1, note, NDebug);
}

/*
 *  math coprocessor error
 */
static void
matherror(Ureg *ureg, void*)
{
	if(!userureg(ureg)){
		if(up == nil)
			mathnote(m->fpsave->fsw, m->fpsave->rip, 1);
		else
			mathnote(up->kfpsave->fsw, up->kfpsave->rip, 1);
		return;
	}
	if(up->fpstate != FPinactive){
		_clts();
		fpsave(up->fpsave);
		up->fpstate = FPinactive;
	}
	mathnote(up->fpsave->fsw, up->fpsave->rip, 0);
}

/*
 *  SIMD error
 */
static void
simderror(Ureg *ureg, void*)
{
	if(!userureg(ureg)){
		if(up == nil)
			mathnote(m->fpsave->mxcsr & 0x3f, ureg->pc, 1);
		else
			mathnote(up->kfpsave->mxcsr & 0x3f, ureg->pc, 1);
		return;
	}
	if(up->fpstate != FPinactive){
		_clts();
		fpsave(up->fpsave);
		up->fpstate = FPinactive;
	}
	mathnote(up->fpsave->mxcsr & 0x3f, ureg->pc, 0);
}

/*
 *  math coprocessor segment overrun
 */
static void
mathover(Ureg *ureg, void*)
{
	if(!userureg(ureg))
		panic("math overrun");

	pexit("math overrun", 0);
}

void
mathinit(void)
{
	trapenable(VectorCERR, matherror, 0, "matherror");
	if(m->cpuidfamily == 3)
		intrenable(IrqIRQ13, matherror, 0, BUSUNKNOWN, "matherror");
	trapenable(VectorCNA, mathemu, 0, "mathemu");
	trapenable(VectorCSO, mathover, 0, "mathover");
	trapenable(VectorSIMD, simderror, 0, "simderror");
}

/*
 *  fpuinit(), called from cpuidentify() for each cpu.
 */
void
fpuinit(void)
{
	u64int cr4;
	ulong regs[4];

	cr4 = getcr4() | CR4Osfxsr|CR4Oxmmex;
	if((m->cpuidcx & (Xsave|Avx)) == (Xsave|Avx) && getconf("*noavx") == nil){
		cr4 |= CR4Oxsave;
		putcr4(cr4);
		m->xcr0 = 7; /* x87, sse, avx */
		putxcr0(m->xcr0);
		cpuid(0xd, 1, regs);
		if(regs[0] & Xsaves){
			fpsave = fpxsaves;
			fprestore = fpxrestores;
		} else {
			if(regs[0] & Xsaveopt)
				fpsave = fpxsaveopt;
			else
				fpsave = fpxsave;
			fprestore = fpxrestore;
		}
	} else {
		putcr4(cr4);
		fpsave = fpssesave;
		fprestore = fpsserestore;
	}

	m->fpsave = nil;
	m->fpstate = FPinit;
	_stts();
}

static FPsave*
fpalloc(void)
{
	FPsave *save;

	while((save = mallocalign(sizeof(FPsave), FPalign, 0, 0)) == nil){
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
		_clts();
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
		if(p->fpstate == FPactive || p->kfpstate == FPactive){
			_fnclex();
			_stts();
		}
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
		_clts();
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
		_fnclex();
		_stts();
		/* wet floor */
	case FPinactive:
		fpfree(m->fpsave);
		m->fpsave = nil;
		m->fpstate = FPinit;
	}
}

/*
 *  Protect or save FPU state and setup new state
 *  (lazily in the case of user process) for the kernel.
 *  All syscalls, traps and interrupts (except mathemu()!)
 *  are handled between fpukenter() and fpukexit(),
 *  so they can use floating point and vector instructions.
 */
FPsave*
fpukenter(Ureg *)
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
		_stts();
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
			_fnclex();
			_stts();
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
			_clts();
		}
		return;
	}

	switch(up->kfpstate){
	case FPactive:
		_fnclex();
		_stts();
		/* wet floor */
	case FPinactive:
		fpfree(up->kfpsave);
		up->kfpstate = FPinit;
	}
	up->kfpsave = save;
	if(save != nil)
		up->kfpstate = FPinactive;
}

/*
 *  Before restoring the state, check for any pending
 *  exceptions, there's no way to restore the state without
 *  generating an unmasked exception.
 *  More attention should probably be paid here to the
 *  exception masks and error summary.
 */
static int
fpcheck(FPsave *save, int kernel)
{
	ulong status, control;

	status = save->fsw;
	control = save->fcw;
	if((status & ~control) & 0x07F){
		mathnote(status, save->rip, kernel);
		return 1;
	}
	return 0;
}

/*
 *  math coprocessor emulation fault
 */
static void
mathemu(Ureg *ureg, void*)
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
				fpcheck(m->fpsave, 1);
				fprestore(m->fpsave);
				m->fpstate = FPactive;
				break;
			default:
				panic("floating point error in irq");
			}
			return;
		}

		if(up->fpstate == FPprotected){
			_clts();
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
			fpcheck(up->kfpsave, 1);
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
		if(fpcheck(up->fpsave, 0))
			break;
		fprestore(up->fpsave);
		up->fpstate = FPactive;
		break;
	case FPprotected:
		up->fpstate = FPactive;
		_clts();
		break;
	case FPactive:
		postnote(up, 1, "sys: floating point error", NDebug);
		break;
	}
}
