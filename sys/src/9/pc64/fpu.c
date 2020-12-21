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
mathnote(ulong status, uintptr pc)
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
	snprint(note, sizeof note, "sys: fp: %s fppc=%#p status=0x%lux",
		msg, pc, status);
	postnote(up, 1, note, NDebug);
}

/*
 *  math coprocessor error
 */
static void
matherror(Ureg *, void*)
{
	/*
	 * Save FPU state to check out the error.
	 */
	fpsave(up->fpsave);
	up->fpstate = FPinactive | (up->fpstate & (FPnouser|FPkernel|FPindexm));
	mathnote(up->fpsave->fsw, up->fpsave->rip);
}

/*
 *  SIMD error
 */
static void
simderror(Ureg *ureg, void*)
{
	fpsave(up->fpsave);
	up->fpstate = FPinactive | (up->fpstate & (FPnouser|FPkernel|FPindexm));
	mathnote(up->fpsave->mxcsr & 0x3f, ureg->pc);
}

void
fpinit(void)
{
	/*
	 * A process tries to use the FPU for the
	 * first time and generates a 'device not available'
	 * exception.
	 * Turn the FPU on and initialise it for use.
	 * Set the precision and mask the exceptions
	 * we don't care about from the generic Mach value.
	 */
	_clts();
	_fninit();
	_fwait();
	_fldcw(0x0232);
	_ldmxcsr(0x1900);
}

/*
 *  math coprocessor emulation fault
 */
static void
mathemu(Ureg *ureg, void*)
{
	ulong status, control;
	int index;

	if(up->fpstate & FPillegal){
		/* someone did floating point in a note handler */
		postnote(up, 1, "sys: floating point in note handler", NDebug);
		return;
	}
	switch(up->fpstate & ~(FPnouser|FPkernel|FPindexm)){
	case FPactive	| FPpush:
		_clts();
		fpsave(up->fpsave);
	case FPinactive	| FPpush:
		up->fpstate += FPindex1;
	case FPinit	| FPpush:
	case FPinit:
		fpinit();
		index = up->fpstate >> FPindexs;
		if(index < 0 || index > (FPindexm>>FPindexs))
			panic("fpslot index overflow: %d", index);
		if(userureg(ureg)){
			if(index != 0)
				panic("fpslot index %d != 0 for user", index);
		} else {
			if(index == 0)
				up->fpstate |= FPnouser;
			up->fpstate |= FPkernel;
		}
		while(up->fpslot[index] == nil)
			up->fpslot[index] = mallocalign(sizeof(FPsave), FPalign, 0, 0);
		up->fpsave = up->fpslot[index];
		up->fpstate = FPactive | (up->fpstate & (FPnouser|FPkernel|FPindexm));
		break;
	case FPinactive:
		/*
		 * Before restoring the state, check for any pending
		 * exceptions, there's no way to restore the state without
		 * generating an unmasked exception.
		 * More attention should probably be paid here to the
		 * exception masks and error summary.
		 */
		status = up->fpsave->fsw;
		control = up->fpsave->fcw;
		if((status & ~control) & 0x07F){
			mathnote(status, up->fpsave->rip);
			break;
		}
		fprestore(up->fpsave);
		up->fpstate = FPactive | (up->fpstate & (FPnouser|FPkernel|FPindexm));
		break;
	case FPactive:
		panic("math emu pid %ld %s pc %#p", 
			up->pid, up->text, ureg->pc);
		break;
	}
}

/*
 *  math coprocessor segment overrun
 */
static void
mathover(Ureg*, void*)
{
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
 * fpuinit(), called from cpuidentify() for each cpu.
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
}

void
fpuprocsetup(Proc *p)
{
	p->fpstate = FPinit;
	_stts();
}

void
fpuprocfork(Proc *p)
{
	int s;

	/* save floating point state */
	s = splhi();
	switch(up->fpstate & ~FPillegal){
	case FPactive	| FPpush:
		_clts();
	case FPactive:
		fpsave(up->fpsave);
		up->fpstate = FPinactive | (up->fpstate & FPpush);
	case FPactive	| FPkernel:
	case FPinactive	| FPkernel:
	case FPinactive	| FPpush:
	case FPinactive:
		while(p->fpslot[0] == nil)
			p->fpslot[0] = mallocalign(sizeof(FPsave), FPalign, 0, 0);
		memmove(p->fpsave = p->fpslot[0], up->fpslot[0], sizeof(FPsave));
		p->fpstate = FPinactive;
	}
	splx(s);
}

void
fpuprocsave(Proc *p)
{
	switch(p->fpstate & ~(FPnouser|FPkernel|FPindexm)){
	case FPactive	| FPpush:
		_clts();
	case FPactive:
		if(p->state == Moribund){
			_fnclex();
			_stts();
			break;
		}
		/*
		 * Fpsave() stores without handling pending
		 * unmasked exeptions. Postnote() can't be called
		 * so the handling of pending exceptions is delayed
		 * until the process runs again and generates an
		 * emulation fault to activate the FPU.
		 */
		fpsave(p->fpsave);
		p->fpstate = FPinactive | (p->fpstate & ~FPactive);
		break;
	}
}

void
fpuprocrestore(Proc*)
{
}


/*
 * Fpusave and fpurestore lazily save and restore FPU state across
 * system calls and the pagefault handler so that we can take
 * advantage of SSE instructions such as AES-NI in the kernel.
 */
int
fpusave(void)
{
	int ostate = up->fpstate;
	if((ostate & ~(FPnouser|FPkernel|FPindexm)) == FPactive)
		_stts();
	up->fpstate = FPpush | (ostate & ~FPillegal);
	return ostate;
}
void
fpurestore(int ostate)
{
	int astate = up->fpstate;
	if(astate == (FPpush | (ostate & ~FPillegal))){
		if((ostate & ~(FPnouser|FPkernel|FPindexm)) == FPactive)
			_clts();
	} else {
		if(astate == FPinit)	/* don't restore on procexec()/procsetup() */
			return;
		if((astate & ~(FPnouser|FPkernel|FPindexm)) == FPactive)
			_stts();
		up->fpsave = up->fpslot[ostate>>FPindexs];
		if(ostate & FPactive)
			ostate = FPinactive | (ostate & ~FPactive);
	}
	up->fpstate = ostate;
}
