#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"

enum {
	CR4Osfxsr  = 1 << 9,
	CR4Oxmmex  = 1 << 10,
};

/* from l.s */
extern void fpsserestore(FPsave*);
extern void fpssesave(FPsave*);
extern void fpx87restore0(FPsave*);
extern void fpx87save0(FPsave*);
extern void ldmxcsr(ulong);

void
putxcr0(ulong)
{
}

/*
 * we keep FPsave structure in SSE format emulating FXSAVE / FXRSTOR
 * instructions for legacy x87 fpu.
 */
static void
fpx87save(FPsave *fps)
{
	ushort tag;

	fpx87save0(fps);

	/*
	 * convert x87 tag word to fxsave tag byte:
	 * 00, 01, 10 -> 1, 11 -> 0
	 */
	tag = ~fps->tag;
	tag = (tag | (tag >> 1)) & 0x5555;
	tag = (tag | (tag >> 1)) & 0x3333;
	tag = (tag | (tag >> 2)) & 0x0F0F;
	tag = (tag | (tag >> 4)) & 0x00FF;

	/* NOP fps->fcw = fps->control; */
	fps->fsw = fps->status;
	fps->ftw = tag;
	fps->fop = fps->opcode;
	fps->fpuip = fps->pc;
	fps->cs = fps->selector;
	fps->fpudp = fps->operand;
	fps->ds = fps->oselector;

#define MOVA(d,s) \
	*((ushort*)(d+8)) = *((ushort*)(s+8)), \
	*((ulong*)(d+4)) = *((ulong*)(s+4)), \
	*((ulong*)(d)) = *((ulong*)(s))

	MOVA(fps->xregs+0x70, fps->regs+70);
	MOVA(fps->xregs+0x60, fps->regs+60);
	MOVA(fps->xregs+0x50, fps->regs+50);
	MOVA(fps->xregs+0x40, fps->regs+40);
	MOVA(fps->xregs+0x30, fps->regs+30);
	MOVA(fps->xregs+0x20, fps->regs+20);
	MOVA(fps->xregs+0x10, fps->regs+10);
	MOVA(fps->xregs+0x00, fps->regs+00);

#undef MOVA

#define CLR6(d)	\
	*((ulong*)(d)) = 0, \
	*((ushort*)(d+4)) = 0

	CLR6(fps->xregs+0x70+10);
	CLR6(fps->xregs+0x60+10);
	CLR6(fps->xregs+0x50+10);
	CLR6(fps->xregs+0x40+10);
	CLR6(fps->xregs+0x30+10);
	CLR6(fps->xregs+0x20+10);
	CLR6(fps->xregs+0x10+10);
	CLR6(fps->xregs+0x00+10);

#undef CLR6

	fps->rsrvd1 = fps->rsrvd2 = fps->mxcsr = fps->mxcsr_mask = 0;
}

static void
fpx87restore(FPsave *fps)
{
	ushort msk, tos, tag, *reg;

	/* convert fxsave tag byte to x87 tag word */
	tag = 0;
	tos = 7 - ((fps->fsw >> 11) & 7);
	for(msk = 0x80; msk != 0; tos--, msk >>= 1){
		tag <<= 2;
		if((fps->ftw & msk) != 0){
			reg = (ushort*)&fps->xregs[(tos & 7) << 4];
			switch(reg[4] & 0x7fff){
			case 0x0000:
				if((reg[0] | reg[1] | reg[2] | reg[3]) == 0){
					tag |= 1;	/* 01 zero */
					break;
				}
				/* no break */
			case 0x7fff:
				tag |= 2;		/* 10 special */
				break;
			default:
				if((reg[3] & 0x8000) == 0)
					break;		/* 00 valid */
				tag |= 2;		/* 10 special */
				break;
			}
		}else{
			tag |= 3;			/* 11 empty */
		}
	}

#define MOVA(d,s) \
	*((ulong*)(d)) = *((ulong*)(s)), \
	*((ulong*)(d+4)) = *((ulong*)(s+4)), \
	*((ushort*)(d+8)) = *((ushort*)(s+8))

	MOVA(fps->regs+00, fps->xregs+0x00);
	MOVA(fps->regs+10, fps->xregs+0x10);
	MOVA(fps->regs+20, fps->xregs+0x20);
	MOVA(fps->regs+30, fps->xregs+0x30);
	MOVA(fps->regs+40, fps->xregs+0x40);
	MOVA(fps->regs+50, fps->xregs+0x50);
	MOVA(fps->regs+60, fps->xregs+0x60);
	MOVA(fps->regs+70, fps->xregs+0x70);

#undef MOVA

	fps->oselector = fps->ds;
	fps->operand = fps->fpudp;
	fps->opcode = fps->fop & 0x7ff;
	fps->selector = fps->cs;
	fps->pc = fps->fpuip;
	fps->tag = tag;
	fps->status = fps->fsw;
	/* NOP fps->control = fps->fcw;  */

	fps->r1 = fps->r2 = fps->r3 = fps->r4 = 0;

	fpx87restore0(fps);
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
mathnote(ulong status, ulong pc)
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
	snprint(note, sizeof note, "sys: fp: %s fppc=0x%lux status=0x%lux",
		msg, pc, status);
	postnote(up, 1, note, NDebug);
}

/*
 *  math coprocessor error
 */
static void
matherror(Ureg*, void*)
{
	/*
	 *  a write cycle to port 0xF0 clears the interrupt latch attached
	 *  to the error# line from the 387
	 */
	if(!(m->cpuiddx & Fpuonchip))
		outb(0xF0, 0xFF);

	/*
	 *  get floating point state to check out error
	 */
	fpsave(up->fpsave);
	up->fpstate = FPinactive;
	mathnote(up->fpsave->fsw, up->fpsave->fpuip);
}

/*
 *  SIMD error
 */
static void
simderror(Ureg *ureg, void*)
{
	fpsave(up->fpsave);
	up->fpstate = FPinactive;
	mathnote(up->fpsave->mxcsr & 0x3f, ureg->pc);
}

/*
 *  math coprocessor emulation fault
 */
static void
mathemu(Ureg *ureg, void*)
{
	ulong status, control;

	if(up->fpstate & FPillegal){
		/* someone did floating point in a note handler */
		postnote(up, 1, "sys: floating point in note handler", NDebug);
		return;
	}
	switch(up->fpstate){
	case FPinit:
		fpinit();
		if(fpsave == fpssesave)
			ldmxcsr(0x1f80);	/* no simd exceptions on 386 */
		while(up->fpsave == nil)
			up->fpsave = mallocalign(sizeof(FPsave), FPalign, 0, 0);
		up->fpstate = FPactive;
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
			mathnote(status, up->fpsave->fpuip);
			break;
		}
		fprestore(up->fpsave);
		up->fpstate = FPactive;
		break;
	case FPactive:
		panic("math emu pid %ld %s pc 0x%lux", 
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

void
fpuprocsetup(Proc *p)
{
	p->fpstate = FPinit;
	fpoff();
}

void
fpuprocfork(Proc *p)
{
	int s;

	s = splhi();
	switch(up->fpstate & ~FPillegal){
	case FPactive:
		fpsave(up->fpsave);
		up->fpstate = FPinactive;
	case FPinactive:
		while(p->fpsave == nil)
			p->fpsave = mallocalign(sizeof(FPsave), FPalign, 0, 0);
		memmove(p->fpsave, up->fpsave, sizeof(FPsave));
		p->fpstate = FPinactive;
	}
	splx(s);
}

void
fpuprocsave(Proc *p)
{
	if(p->fpstate == FPactive){
		if(p->state == Moribund)
			fpclear();
		else{
			/*
			 * Fpsave() stores without handling pending
			 * unmasked exeptions. Postnote() can't be called
			 * so the handling of pending exceptions is delayed
			 * until the process runs again and generates an
			 * emulation fault to activate the FPU.
			 */
			fpsave(p->fpsave);
		}
		p->fpstate = FPinactive;
	}
}

void
fpuprocrestore(Proc*)
{
}
