#include	"u.h"
#include	"tos.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"ureg.h"
#include	"../port/error.h"
#include	<trace.h>

extern int irqhandled(Ureg*, int);
extern void irqinit(void);

static void debugexc(Ureg*, void*);
static void debugbpt(Ureg*, void*);
static void fault386(Ureg*, void*);
static void doublefault(Ureg*, void*);
static void unexpected(Ureg*, void*);
static void _dumpstack(Ureg*);

/*
 * Minimal trap setup.  Just enough so that we can panic
 * on traps (bugs) during kernel initialization.
 * Called very early - malloc is not yet available.
 */
void
trapinit0(void)
{
	int d1, v;
	ulong vaddr;
	Segdesc *idt;
	ushort ptr[3];

	idt = (Segdesc*)IDTADDR;
	vaddr = (ulong)vectortable;
	for(v = 0; v < 256; v++){
		d1 = (vaddr & 0xFFFF0000)|SEGP;
		switch(v){
		case VectorBPT:
			d1 |= SEGPL(3)|SEGIG;
			break;

		case VectorSYSCALL:
			d1 |= SEGPL(3)|SEGIG;
			break;

		default:
			d1 |= SEGPL(0)|SEGIG;
			break;
		}
		idt[v].d0 = (vaddr & 0xFFFF)|(KESEL<<16);
		idt[v].d1 = d1;
		vaddr += 6;
	}
	ptr[0] = sizeof(Segdesc)*256-1;
	ptr[1] = IDTADDR & 0xFFFF;
	ptr[2] = IDTADDR >> 16;
	lidt(ptr);
}

void
trapinit(void)
{
	irqinit();

	nmienable();

	/*
	 * Special traps.
	 * Syscall() is called directly without going through trap().
	 */
	trapenable(VectorDE, debugexc, 0, "debugexc");
	trapenable(VectorBPT, debugbpt, 0, "debugpt");
	trapenable(VectorPF, fault386, 0, "fault386");
	trapenable(Vector2F, doublefault, 0, "doublefault");
	trapenable(Vector15, unexpected, 0, "unexpected");
}

static char* excname[32] = {
	"divide error",
	"debug exception",
	"nonmaskable interrupt",
	"breakpoint",
	"overflow",
	"bounds check",
	"invalid opcode",
	"coprocessor not available",
	"double fault",
	"coprocessor segment overrun",
	"invalid TSS",
	"segment not present",
	"stack exception",
	"general protection violation",
	"page fault",
	"15 (reserved)",
	"coprocessor error",
	"alignment check",
	"machine check",
	"simd error",
	"20 (reserved)",
	"21 (reserved)",
	"22 (reserved)",
	"23 (reserved)",
	"24 (reserved)",
	"25 (reserved)",
	"26 (reserved)",
	"27 (reserved)",
	"28 (reserved)",
	"29 (reserved)",
	"30 (reserved)",
	"31 (reserved)",
};

static int
usertrap(int vno)
{
	char buf[ERRMAX];

	if(vno < nelem(excname)){
		spllo();
		sprint(buf, "sys: trap: %s", excname[vno]);
		postnote(up, 1, buf, NDebug);
		return 1;
	}
	return 0;
}

/*
 *  All traps come here.  It is slower to have all traps call trap()
 *  rather than directly vectoring the handler.  However, this avoids a
 *  lot of code duplication and possible bugs.  The only exception is
 *  VectorSYSCALL.
 *  Trap is called with interrupts disabled via interrupt-gates.
 */
void
trap(Ureg* ureg)
{
	int vno, user;

	user = kenter(ureg);
	vno = ureg->trap;
	if(!irqhandled(ureg, vno) && (!user || !usertrap(vno))){
		if(!user){
			void (*pc)(void);
			ulong *sp; 

			extern void _forkretpopgs(void);
			extern void _forkretpopfs(void);
			extern void _forkretpopes(void);
			extern void _forkretpopds(void);
			extern void _forkretiret(void);
			extern void _rdmsrinst(void);
			extern void _wrmsrinst(void);
			extern void _peekinst(void);

			extern void load_fs(ulong);
			extern void load_gs(ulong);

			load_fs(NULLSEL);
			load_gs(NULLSEL);

			sp = (ulong*)&ureg->sp;	/* kernel stack */
			pc = (void*)ureg->pc;

			if(pc == _forkretpopgs || pc == _forkretpopfs || 
			   pc == _forkretpopes || pc == _forkretpopds){
				if(vno == VectorGPF || vno == VectorSNP){
					sp[0] = NULLSEL;
					return;
				}
			} else if(pc == _forkretiret){
				if(vno == VectorGPF || vno == VectorSNP){
					sp[1] = UESEL;	/* CS */
					sp[4] = UDSEL;	/* SS */
					return;
				}
			} else if(pc == _rdmsrinst || pc == _wrmsrinst){
				if(vno == VectorGPF){
					ureg->bp = -1;
					ureg->pc += 2;
					return;
				}
			} else if(pc == _peekinst){
				if(vno == VectorGPF){
					ureg->pc += 2;
					return;
				}
			}

			/* early fault before trapinit() */
			if(vno == VectorPF)
				fault386(ureg, 0);
		}

		dumpregs(ureg);
		if(!user){
			ureg->sp = (ulong)&ureg->sp;
			_dumpstack(ureg);
		}
		if(vno < nelem(excname))
			panic("%s", excname[vno]);
		panic("unknown trap/intr: %d", vno);
	}
	splhi();

	if(user){
		if(up->procctl || up->nnote)
			donotify(ureg);
		kexit(ureg);
	}
}

/*
 *  dump registers
 */
void
dumpregs2(Ureg* ureg)
{
	if(up)
		iprint("cpu%d: registers for %s %lud\n",
			m->machno, up->text, up->pid);
	else
		iprint("cpu%d: registers for kernel\n", m->machno);
	iprint("FLAGS=%luX TRAP=%luX ECODE=%luX PC=%luX",
		ureg->flags, ureg->trap, ureg->ecode, ureg->pc);
	if(userureg(ureg))
		iprint(" SS=%4.4luX USP=%luX\n", ureg->ss & 0xFFFF, ureg->usp);
	else
		iprint(" SP=%luX\n", (ulong)&ureg->sp);
	iprint("  AX %8.8luX  BX %8.8luX  CX %8.8luX  DX %8.8luX\n",
		ureg->ax, ureg->bx, ureg->cx, ureg->dx);
	iprint("  SI %8.8luX  DI %8.8luX  BP %8.8luX\n",
		ureg->si, ureg->di, ureg->bp);
	iprint("  CS %4.4luX  DS %4.4luX  ES %4.4luX  FS %4.4luX  GS %4.4luX\n",
		ureg->cs & 0xFFFF, ureg->ds & 0xFFFF, ureg->es & 0xFFFF,
		ureg->fs & 0xFFFF, ureg->gs & 0xFFFF);
}

void
dumpregs(Ureg* ureg)
{
	dumpregs2(ureg);

	/*
	 * Processor control registers.
	 * If machine check exception, time stamp counter, page size extensions
	 * or enhanced virtual 8086 mode extensions are supported, there is a
	 * CR4. If there is a CR4 and machine check extensions, read the machine
	 * check address and machine check type registers if RDMSR supported.
	 */
	iprint("  CR0 %8.8lux CR2 %8.8lux CR3 %8.8lux",
		getcr0(), getcr2(), getcr3());
	if(m->cpuiddx & (Mce|Tsc|Pse|Vmex)){
		iprint(" CR4 %8.8lux\n", getcr4());
		if(ureg->trap == VectorMC)
			dumpmcregs();
	}
	iprint("\n  ur %#p up %#p\n", ureg, up);
}


/*
 * Fill in enough of Ureg to get a stack trace, and call a function.
 * Used by debugging interface rdb.
 */
void
callwithureg(void (*fn)(Ureg*))
{
	Ureg ureg;
	ureg.pc = getcallerpc(&fn);
	ureg.sp = (ulong)&fn;
	fn(&ureg);
}

static void
_dumpstack(Ureg *ureg)
{
	uintptr l, v, i, estack;
	extern ulong etext;
	int x;
	char *s;

	if((s = getconf("*nodumpstack")) != nil && strcmp(s, "0") != 0){
		iprint("dumpstack disabled\n");
		return;
	}
	iprint("dumpstack\n");

	x = 0;
	x += iprint("ktrace /kernel/path %.8lux %.8lux <<EOF\n", ureg->pc, ureg->sp);
	i = 0;
	if(up
	&& (uintptr)&l >= (uintptr)up - KSTACK
	&& (uintptr)&l <= (uintptr)up)
		estack = (uintptr)up;
	else if((uintptr)&l >= (uintptr)m->stack
	&& (uintptr)&l <= (uintptr)m+MACHSIZE)
		estack = (uintptr)m+MACHSIZE;
	else
		return;
	x += iprint("estackx %p\n", estack);

	for(l = (uintptr)&l; l < estack; l += sizeof(uintptr)){
		v = *(uintptr*)l;
		if((KTZERO < v && v < (uintptr)&etext) || estack-l < 32){
			/*
			 * Could Pick off general CALL (((uchar*)v)[-5] == 0xE8)
			 * and CALL indirect through AX
			 * (((uchar*)v)[-2] == 0xFF && ((uchar*)v)[-2] == 0xD0),
			 * but this is too clever and misses faulting address.
			 */
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

	if(ureg->trap != VectorNMI)
		return;

	i = 0;
	for(l = (uintptr)&l; l < estack; l += sizeof(uintptr)){
		iprint("%.8p ", *(uintptr*)l);
		if(++i == 8){
			i = 0;
			iprint("\n");
		}
	}
	if(i)
		iprint("\n");
}

void
dumpstack(void)
{
	callwithureg(_dumpstack);
}

static void
debugexc(Ureg *ureg, void *)
{
	u32int dr6, m;
	char buf[ERRMAX];
	char *p, *e;
	int i;

	dr6 = getdr6();
	if(up == nil)
		panic("kernel debug exception dr6=%#.8ux", dr6);
	putdr6(up->dr[6]);
	if(userureg(ureg))
		qlock(&up->debug);
	else if(!canqlock(&up->debug))
		return;
	m = up->dr[7];
	m = (m >> 4 | m >> 3) & 8 | (m >> 3 | m >> 2) & 4 | (m >> 2 | m >> 1) & 2 | (m >> 1 | m) & 1;
	m &= dr6;
	if(m == 0){
		sprint(buf, "sys: debug exception dr6=%#.8ux", dr6);
		postnote(up, 0, buf, NDebug);
	}else{
		p = buf;
		e = buf + sizeof(buf);
		p = seprint(p, e, "sys: watchpoint ");
		for(i = 0; i < 4; i++)
			if((m & 1<<i) != 0)
				p = seprint(p, e, "%d%s", i, (m >> i + 1 != 0) ? "," : "");
		postnote(up, 0, buf, NDebug);
	}
	qunlock(&up->debug);
}

static void
debugbpt(Ureg* ureg, void*)
{
	if(up == 0)
		panic("kernel bpt");
	/* restore pc to instruction that caused the trap */
	ureg->pc--;
	postnote(up, 1, "sys: breakpoint", NDebug);
}

static void
doublefault(Ureg*, void*)
{
	panic("double fault");
}

static void
unexpected(Ureg* ureg, void*)
{
	print("unexpected trap %lud; ignoring\n", ureg->trap);
}

static void
fault386(Ureg* ureg, void*)
{
	ulong addr;
	int read;

	addr = getcr2();
	read = !(ureg->ecode & 2);

	if(!userureg(ureg)){
		if(vmapsync(addr))
			return;
		{
			extern void _peekinst(void);
			if((void(*)(void))ureg->pc == _peekinst){
				ureg->pc += 2;
				return;
			}
		}
		if(addr >= USTKTOP)
			panic("kernel fault: bad address pc=0x%.8lux addr=0x%.8lux", ureg->pc, addr);
	}

	if(fault(addr, ureg->pc, read) < 0){
		if(!userureg(ureg)){
			dumpregs(ureg);
			panic("kernel fault: %s addr=%#p", read? "read": "write", addr);
		}
		faultnote("fault", read? "read": "write", addr);
	}
}

/*
 *  Syscall is called directly from assembler without going through trap().
 */
void
syscall(Ureg* ureg)
{
	ulong scallnr;

	if(!kenter(ureg))
		panic("syscall: cs 0x%4.4luX", ureg->cs);
	scallnr = ureg->ax;
	dosyscall(scallnr, (Sargs*)(ureg->sp + BY2WD), &ureg->ax);
	if(up->procctl || up->nnote)
		donotify(ureg);
	/* if we delayed sched because we held a lock, sched now */
	if(up->delaysched)
		sched();
	kexit(ureg);
}

/*
 *  Call user, if necessary, with note.
 *  Pass user the Ureg struct and the note on his stack.
 */
Ureg*
notify(Ureg* ureg, char *msg)
{
	Ureg *nureg;
	ulong sp;

	sp = ureg->usp;
	sp -= 256;	/* debugging: preserve context causing problem */
	sp -= sizeof(Ureg);

	if(!okaddr(sp-ERRMAX-4*BY2WD, sizeof(Ureg)+ERRMAX+4*BY2WD, 1))
		return nil;

	nureg = (Ureg*)sp;
	memmove(nureg, ureg, sizeof(Ureg));
	sp -= BY2WD+ERRMAX;
	memmove((char*)sp, msg, ERRMAX);
	sp -= 3*BY2WD;
	*(ulong*)(sp+2*BY2WD) = sp+3*BY2WD;		/* arg 2 is string */
	*(ulong*)(sp+1*BY2WD) = (ulong)nureg;		/* arg 1 is ureg* */
	*(ulong*)(sp+0*BY2WD) = 0;			/* arg 0 is pc */
	ureg->usp = sp;
	ureg->pc = (ulong)up->notify;
	ureg->cs = UESEL;
	ureg->ss = ureg->ds = ureg->es = UDSEL;

	return nureg;
}

/*
 *   Return user to state before notify()
 */
int
noted(Ureg* ureg, Ureg *nureg, int arg0)
{
	ulong oureg, sp;

	oureg = (ulong)nureg;

	setregisters(ureg, (char*)ureg, (char*)nureg, sizeof(Ureg));

	switch(arg0){
	case NCONT:
	case NRSTR:
if(0) print("%s %lud: noted %#p %#p\n", up->text, up->pid, ureg->pc, ureg->usp);
		if(!okaddr(ureg->pc, 1, 0) || !okaddr(ureg->usp, BY2WD, 0))
			return -1;
		break;

	case NSAVE:
		sp = oureg-4*BY2WD-ERRMAX;
		if(!okaddr(ureg->pc, 1, 0) || !okaddr(sp, 4*BY2WD, 1))
			return -1;
		ureg->usp = sp;
		((ulong*)sp)[1] = oureg;	/* arg 1 0(FP) is ureg* */
		((ulong*)sp)[0] = 0;		/* arg 0 is pc */
		break;
	}

	return 0;
}

uintptr
execregs(uintptr entry, int argc, char *argv[], Tos *tos)
{
	ulong *sp = (void*)argv;
	Ureg *ureg;

	*--sp = argc;

	ureg = up->dbgreg;
	ureg->usp = (ulong)sp;
	ureg->pc = entry;
	ureg->cs = UESEL;
	ureg->ss = ureg->ds = ureg->es = UDSEL;
	ureg->fs = ureg->gs = NULLSEL;

	return (uintptr)tos;		/* address of kernel/user shared data */
}

/*
 *  return the userpc the last exception happened at
 */
uintptr
userpc(void)
{
	return up->dbgreg->pc;
}

/* This routine must save the values of registers the user is not permitted
 * to write from devproc and then restore the saved values before returning.
 */
void
setregisters(Ureg* ureg, char* pureg, char* uva, int n)
{
	ulong flags;

	flags = ureg->flags;
	memmove(pureg, uva, n);
	ureg->flags = (ureg->flags & 0xCD5) | (flags & ~0xCD5);
	ureg->cs |= 3;
	ureg->ss |= 3;
}

void
kprocchild(Proc *p, void (*entry)(void))
{
	/*
	 * gotolabel() needs a word on the stack in
	 * which to place the return PC used to jump
	 * to linkproc().
	 */
	p->sched.pc = (ulong)entry;
	p->sched.sp = (ulong)p - BY2WD;
}

void
forkchild(Proc *p, Ureg *ureg)
{
	Ureg *cureg;

	/*
	 * Add 2*BY2WD to the stack to account for
	 *  - the return PC
	 *  - trap's argument (ur)
	 */
	p->sched.sp = (ulong)p - (sizeof(Ureg)+2*BY2WD);
	p->sched.pc = (ulong)forkret;

	cureg = (Ureg*)(p->sched.sp+2*BY2WD);
	memmove(cureg, ureg, sizeof(Ureg));
	/* return value of syscall in child */
	cureg->ax = 0;
}

/* Give enough context in the ureg to produce a kernel stack for
 * a sleeping process
 */
void
setkernur(Ureg* ureg, Proc* p)
{
	ureg->pc = p->sched.pc;
	ureg->sp = p->sched.sp+4;
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
