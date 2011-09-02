#include "u.h"
#include "ureg.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "arm.h"
#include "tos.h"

void (*proctrace)(Proc *, int, vlong);

ulong
userpc(void)
{
	return dbgpc(up);
}

ulong
dbgpc(Proc *p)
{
	Ureg *ureg;
	
	ureg = p->dbgreg;
	if(ureg == 0)
		return 0;
	return ureg->pc;
}

void
procsave(Proc *)
{
}

void
procrestore(Proc *)
{
}

void
procfork(Proc *)
{
}

void
procsetup(Proc *p)
{
	p->fpstate = FPinit;
}

static void
linkproc(void)
{
	spllo();
	up->kpfun(up->kparg);
	pexit("kproc exiting", 0);
}

void
kprocchild(Proc *p, void (*func)(void *), void *arg)
{
	p->sched.pc = (ulong) linkproc;
	p->sched.sp = (ulong) p->kstack + KSTACK;
	p->kpfun = func;
	p->kparg = arg;
}

void
forkchild(Proc *p, Ureg *ureg)
{
	Ureg *cureg;
	
	p->sched.sp = (ulong) p->kstack + KSTACK - (sizeof(Ureg) + 8);
	p->sched.pc = (ulong) forkret;
	cureg = (Ureg*) (p->sched.sp + 8);
	memmove(cureg, ureg, sizeof(Ureg));
	cureg->r0 = 0;
	p->psstate = 0;
	p->insyscall = 0;
}

long
execregs(ulong entry, ulong ssize, ulong nargs)
{
	ulong *sp;
	Ureg *ureg;
	
	up->fpstate = FPinit;
	sp = (ulong *) (USTKTOP - ssize);
	*--sp = nargs;
	
	ureg = up->dbgreg;
	memset(ureg, 0, sizeof *ureg);
	ureg->psr = PsrMusr;
	ureg->sp = (ulong) sp;
	ureg->pc = entry;
	return USTKTOP - sizeof(Tos);
}

void
evenaddr(uintptr addr)
{
	if(addr & 3){
		postnote(up, 1, "sys: odd address", NDebug);
		error(Ebadarg);
	}
}

Segment *
data2txt(Segment *)
{
	panic("data2txt");
}

void
_dumpstack(ulong sp, ulong pc)
{
	int x;
	uintptr l, v, i, estack;

	x = 0;
	x += iprint("ktrace /arm/s9panda %#.8lux %#.8lux <<EOF\n",
		pc, sp);
	i = 0;
	if(up
	&& (uintptr)&l >= (uintptr)up->kstack
	&& (uintptr)&l <= (uintptr)up->kstack+KSTACK)
		estack = (uintptr)up->kstack+KSTACK;
	else if((uintptr)&l >= (uintptr)(KTZERO - BY2PG)
	&& (uintptr)&l <= (uintptr)KTZERO)
		estack = (uintptr)KTZERO;
	else
		return;
	x += iprint("estackx %p\n", estack);

	for(l = (uintptr)&l; l < estack; l += sizeof(uintptr)){
		v = *(uintptr*)l;
		if((KTZERO < v && v < (uintptr)etext) || estack-l < 32){
			x += iprint("%.8p=%.8p ", l, v);
			i++;
		}
		if(i == 4){
			i = 0;
			x += iprint("\n");
		}
	}
	if(i)
		iprint("\n");
	iprint("EOF\n");
}

void
printureg(Ureg *ureg)
{
	print("R0  %.8ulx R1  %.8ulx R2  %.8ulx R3  %.8ulx\n", ureg->r0, ureg->r1, ureg->r2, ureg->r3);
	print("R4  %.8ulx R5  %.8ulx R6  %.8ulx R7  %.8ulx\n", ureg->r4, ureg->r5, ureg->r6, ureg->r7);
	print("R8  %.8ulx R9  %.8ulx R10 %.8ulx R11 %.8ulx\n", ureg->r8, ureg->r9, ureg->r10, ureg->r11);
	print("R12 %.8ulx R13 %.8ulx R14 %.8ulx R15 %.8ulx\n", ureg->r12, ureg->r13, ureg->r14, ureg->pc);
	print("PSR %.8ulx exception %ld\n", ureg->psr, ureg->type);
}
