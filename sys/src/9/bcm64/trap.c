#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "../port/systab.h"

#include <tos.h>
#include "ureg.h"
#include "sysreg.h"

/* SPSR bits user can modify */
#define USPSRMASK	(0xFULL<<28)

static void
setupvector(u32int *v, void (*t)(void), void (*f)(void))
{
	int i;

	for(i = 0; i < 0x80/4; i++){
		v[i] = ((u32int*)t)[i];
		if(v[i] == 0x14000000){
			v[i] |= ((u32int*)f - &v[i]) & 0x3ffffff;
			return;
		}
	}
	panic("bug in vector code");
}

void
trapinit(void)
{
	extern void vsys(void);
	extern void vtrap(void);
	extern void virq(void);
	extern void vfiq(void);
	extern void vserr(void);

	extern void vsys0(void);
	extern void vtrap0(void);
	extern void vtrap1(void);

	static u32int *v;

	intrcpushutdown();
	if(v == nil){
		/* disable everything */
		intrsoff();

		v = mallocalign(0x80*4*4, 1<<11, 0, 0);
		if(v == nil)
			panic("no memory for vector table");

		setupvector(&v[0x000/4], vtrap,	vtrap0);
		setupvector(&v[0x080/4], virq,	vtrap0);
		setupvector(&v[0x100/4], vfiq,	vtrap0);
		setupvector(&v[0x180/4], vserr,	vtrap0);

		setupvector(&v[0x200/4], vtrap,	vtrap1);
		setupvector(&v[0x280/4], virq,	vtrap1);
		setupvector(&v[0x300/4], vfiq,	vtrap1);
		setupvector(&v[0x380/4], vserr,	vtrap1);

		setupvector(&v[0x400/4], vsys,	vsys0);
		setupvector(&v[0x480/4], virq,	vtrap0);
		setupvector(&v[0x500/4], vfiq,	vtrap0);
		setupvector(&v[0x580/4], vserr, vtrap0);

		setupvector(&v[0x600/4], vtrap,	vtrap0);
		setupvector(&v[0x680/4], virq,	vtrap0);
		setupvector(&v[0x700/4], vfiq,	vtrap0);
		setupvector(&v[0x780/4], vserr,	vtrap0);

		cacheduwbse(v, 0x80*4*4);
	}
	cacheiinvse(v, 0x80*4*4);
	syswr(VBAR_EL1, (uintptr)v);
	splx(0x3<<6);	// unmask serr and debug
}

void
kexit(Ureg*)
{
	Tos *tos;
	uvlong t;

	t = cycles();

	tos = (Tos*)(USTKTOP-sizeof(Tos));
	tos->kcycles += t - up->kentry;
	tos->pcycles = t + up->pcycles;
	tos->pid = up->pid;
}

static char *traps[64] = {
	[0x00]	"sys: trap: unknown",
	[0x01]	"sys: trap: WFI or WFE instruction execution",
	[0x0E]	"sys: trap: illegal execution state",
	[0x18]	"sys: trap: illegal MSR/MRS access",
	[0x22]	"sys: trap: misaligned pc",
	[0x26]	"sys: trap: stack pointer misaligned",
	[0x30]	"sys: trap: breakpoint",
	[0x32]	"sys: trap: software step",
	[0x34]	"sys: trap: watchpoint",
	[0x3C]	"sys: trap: BRK instruction",
};

void
trap(Ureg *ureg)
{
	u32int type, intr;
	
	intr = ureg->type >> 32;
	if(intr == 2){
		fiq(ureg);
		return;
	}
	splflo();
	if(userureg(ureg)){
		up->dbgreg = ureg;
		up->kentry = cycles();
	}
	type = (u32int)ureg->type >> 26;
	switch(type){
	case 0x20:	// instruction abort from lower level
	case 0x21:	// instruction abort from same level
	case 0x24:	// data abort from lower level
	case 0x25:	// data abort from same level
		faultarm64(ureg);
		break;
	case 0x07:	// SIMD/FP
	case 0x2C:	// FPU exception (A64 only)
		mathtrap(ureg);
		break;
	case 0x00:	// unknown
		if(intr == 1){
			if(irq(ureg) && up != nil && up->delaysched)
				sched();
			break;
		}
		if(intr == 3){
	case 0x2F:	// SError interrupt
			dumpregs(ureg);
			panic("SError interrupt");
			break;
		}
		/* wet floor */
	case 0x01:	// WFI or WFE instruction execution
	case 0x03:	// MCR or MRC access to CP15 (A32 only)
	case 0x04:	// MCRR or MRC access to CP15 (A32 only)
	case 0x05:	// MCR or MRC access to CP14 (A32 only)
	case 0x06:	// LDC or STD access to CP14 (A32 only)
	case 0x08:	// MCR or MRC to CP10 (A32 only)
	case 0x0C:	// MRC access to CP14 (A32 only)
	case 0x0E:	// Illegal Execution State
	case 0x11:	// SVC instruction execution (A32 only)
	case 0x12:	// HVC instruction execution (A32 only)
	case 0x13:	// SMC instruction execution (A32 only)
	case 0x15:	// SVC instruction execution (A64 only)
	case 0x16:	// HVC instruction execution (A64 only)
	case 0x17:	// SMC instruction execution (A64 only)
	case 0x18:	// MSR/MRS (A64)
	case 0x22:	// misaligned pc
	case 0x26:	// stack pointer misaligned
	case 0x28:	// FPU exception (A32 only)
	case 0x30:	// breakpoint from lower level
	case 0x31:	// breakpoint from same level
	case 0x32:	// software step from lower level
	case 0x33:	// software step from same level
	case 0x34:	// watchpoint execution from lower level
	case 0x35:	// watchpoint exception from same level
	case 0x38:	// breapoint (A32 only)
	case 0x3A:	// vector catch exception (A32 only)
	case 0x3C:	// BRK instruction (A64 only)
	default:
		if(!userureg(ureg)){
			dumpregs(ureg);
			panic("unhandled trap");
		}
		if(traps[type] == nil) type = 0;	// unknown
		postnote(up, 1, traps[type], NDebug);
		break;
	}
	splhi();
	if(userureg(ureg)){
		if(up->procctl || up->nnote)
			notify(ureg);
		kexit(ureg);
	}
}

void
syscall(Ureg *ureg)
{
	vlong startns, stopns;
	uintptr sp, ret;
	ulong scallnr;
	int i, s;
	char *e;

	up->kentry = cycles();
	
	m->syscall++;
	up->insyscall = 1;
	up->pc = ureg->pc;
	up->dbgreg = ureg;
	
	sp = ureg->sp;
	up->scallnr = scallnr = ureg->r0;

	spllo();
	
	up->nerrlab = 0;
	startns = 0;
	ret = -1;
	if(!waserror()){
		if(sp < USTKTOP - BY2PG || sp > USTKTOP - sizeof(Sargs) - BY2WD){
			validaddr(sp, sizeof(Sargs)+BY2WD, 0);
			evenaddr(sp);
		}
		up->s = *((Sargs*) (sp + BY2WD));

		if(up->procctl == Proc_tracesyscall){
			syscallfmt(scallnr, ureg->pc, (va_list) up->s.args);
			s = splhi();
			up->procctl = Proc_stopme;
			procctl();
			splx(s);
			startns = todget(nil);
		}
		
		if(scallnr >= nsyscall || systab[scallnr] == nil){
			pprint("bad sys call number %lud pc %#p", scallnr, ureg->pc);
			postnote(up, 1, "sys: bad sys call", NDebug);
			error(Ebadarg);
		}
		up->psstate = sysctab[scallnr];
		ret = systab[scallnr]((va_list)up->s.args);
		poperror();
	}else{
		e = up->syserrstr;
		up->syserrstr = up->errstr;
		up->errstr = e;
	}
	if(up->nerrlab){
		print("bad errstack [%lud]: %d extra\n", scallnr, up->nerrlab);
		for(i = 0; i < NERR; i++)
			print("sp=%#p pc=%#p\n", up->errlab[i].sp, up->errlab[i].pc);
		panic("error stack");
	}
	ureg->r0 = ret;
	if(up->procctl == Proc_tracesyscall){
		stopns = todget(nil);
		sysretfmt(scallnr, (va_list) up->s.args, ret, startns, stopns);
		s = splhi();
		up->procctl = Proc_stopme;
		procctl();
		splx(s);
	}
	
	up->insyscall = 0;
	up->psstate = 0;
	if(scallnr == NOTED){
		noted(ureg, *((ulong*) up->s.args));
		/*
		 * normally, syscall() returns to forkret()
		 * not restoring general registers when going
		 * to userspace. to completely restore the
		 * interrupted context, we have to return thru
		 * noteret(). we override return pc to jump to
		 * to it when returning form syscall()
		 */
		returnto(noteret);
	}

	if(scallnr != RFORK && (up->procctl || up->nnote)){
		splhi();
		notify(ureg);
	}
	if(up->delaysched)
		sched();
	kexit(ureg);
}

int
notify(Ureg *ureg)
{
	int l;
	uintptr s, sp;
	Note *n;

	if(up->procctl)
		procctl();
	if(up->nnote == 0)
		return 0;
	if(up->fpstate == FPactive){
		fpsave(up->fpsave);
		up->fpstate = FPinactive;
	}
	up->fpstate |= FPillegal;

	s = spllo();
	qlock(&up->debug);
	up->notepending = 0;
	n = &up->note[0];
	if(strncmp(n->msg, "sys:", 4) == 0){
		l = strlen(n->msg);
		if(l > ERRMAX-23)	/* " pc=0x0123456789abcdef\0" */
			l = ERRMAX-23;
		sprint(n->msg+l, " pc=%#p", ureg->pc);
	}

	if(n->flag!=NUser && (up->notified || up->notify==0)){
		qunlock(&up->debug);
		if(n->flag == NDebug)
			pprint("suicide: %s\n", n->msg);
		pexit(n->msg, n->flag!=NDebug);
	}

	if(up->notified){
		qunlock(&up->debug);
		splhi();
		return 0;
	}

	if(!up->notify){
		qunlock(&up->debug);
		pexit(n->msg, n->flag!=NDebug);
	}
	sp = ureg->sp;
	sp -= 256;	/* debugging: preserve context causing problem */
	sp -= sizeof(Ureg);
	sp = STACKALIGN(sp);

	if(!okaddr((uintptr)up->notify, 1, 0)
	|| !okaddr(sp-ERRMAX-4*BY2WD, sizeof(Ureg)+ERRMAX+4*BY2WD, 1)
	|| ((uintptr) up->notify & 3) != 0
	|| (sp & 7) != 0){
		qunlock(&up->debug);
		pprint("suicide: bad address in notify: handler=%#p sp=%#p\n",
			up->notify, sp);
		pexit("Suicide", 0);
	}

	memmove((Ureg*)sp, ureg, sizeof(Ureg));
	*(Ureg**)(sp-BY2WD) = up->ureg;	/* word under Ureg is old up->ureg */
	up->ureg = (void*)sp;
	sp -= BY2WD+ERRMAX;
	memmove((char*)sp, up->note[0].msg, ERRMAX);
	sp -= 3*BY2WD;
	*(uintptr*)(sp+2*BY2WD) = sp+3*BY2WD;
	*(uintptr*)(sp+1*BY2WD) = (uintptr)up->ureg;
	ureg->r0 = (uintptr) up->ureg;
	ureg->sp = sp;
	ureg->pc = (uintptr) up->notify;
	ureg->link = 0;
	up->notified = 1;
	up->nnote--;
	memmove(&up->lastnote, &up->note[0], sizeof(Note));
	memmove(&up->note[0], &up->note[1], up->nnote*sizeof(Note));

	qunlock(&up->debug);
	splx(s);
	return 1;
}

void
noted(Ureg *ureg, ulong arg0)
{
	Ureg *nureg;
	uintptr oureg, sp;
	
	qlock(&up->debug);
	if(arg0 != NRSTR && !up->notified){
		qunlock(&up->debug);
		pprint("call to noted() when not notified\n");
		pexit("Suicide", 0);
	}
	up->notified = 0;
	
	nureg = up->ureg;
	up->fpstate &= ~FPillegal;
	
	oureg = (uintptr) nureg;
	if(!okaddr(oureg - BY2WD, BY2WD + sizeof(Ureg), 0) || (oureg & 7) != 0){
		qunlock(&up->debug);
		pprint("bad ureg in noted or call to noted when not notified\n");
		pexit("Suicide", 0);
	}

	nureg->psr = (nureg->psr & USPSRMASK) | (ureg->psr & ~USPSRMASK);
	memmove(ureg, nureg, sizeof(Ureg));
	
	switch(arg0){
	case NCONT: case NRSTR:
		if(!okaddr(nureg->pc, BY2WD, 0) || !okaddr(nureg->sp, BY2WD, 0) ||
				(nureg->pc & 3) != 0 || (nureg->sp & 7) != 0){
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		up->ureg = (Ureg *) (*(uintptr*) (oureg - BY2WD));
		qunlock(&up->debug);
		break;
	
	case NSAVE:
		if(!okaddr(nureg->pc, BY2WD, 0) || !okaddr(nureg->sp, BY2WD, 0) ||
				(nureg->pc & 3) != 0 || (nureg->sp & 7) != 0){
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		qunlock(&up->debug);
		sp = oureg - 4 * BY2WD - ERRMAX;
		splhi();
		ureg->sp = sp;
		ureg->r0 = (uintptr) oureg;
		((uintptr *) sp)[1] = oureg;
		((uintptr *) sp)[0] = 0;
		break;
	
	default:
		up->lastnote.flag = NDebug;
	
	case NDFLT:
		qunlock(&up->debug);
		if(up->lastnote.flag == NDebug)
			pprint("suicide: %s\n", up->lastnote.msg);
		pexit(up->lastnote.msg, up->lastnote.flag != NDebug);
	}
}

void
faultarm64(Ureg *ureg)
{
	extern void checkpages(void);
	char buf[ERRMAX];
	int read, insyscall;
	uintptr addr;

	insyscall = up->insyscall;
	up->insyscall = 1;

	if(!userureg(ureg) && waserror()){
		if(up->nerrlab == 0){
			pprint("suicide: sys: %s\n", up->errstr);
			pexit(up->errstr, 1);
		}
		up->insyscall = insyscall;
		nexterror();
	}

	addr = getfar();
	read = (ureg->type & (1<<6)) == 0;

	switch((u32int)ureg->type & 0x3F){
	case  4: case  5: case  6: case  7:	// Tanslation fault.
	case  8: case  9: case 10: case 11:	// Access flag fault.
	case 12: case 13: case 14: case 15:	// Permission fault.
	case 48:				// tlb conflict fault.
		if(fault(addr, read) == 0)
			break;

		/* wet floor */
	case  0: case  1: case  2: case  3:	// Address size fault.
	case 16: 				// synchronous external abort.
	case 24: 				// synchronous parity error on a memory access.
	case 20: case 21: case 22: case 23:	// synchronous external abort on a table walk.
	case 28: case 29: case 30: case 31:	// synchronous parity error on table walk.
	case 33:				// alignment fault.
	case 52:				// implementation defined, lockdown abort.
	case 53:				// implementation defined, unsuppoted exclusive.
	case 61:				// first level domain fault
	case 62:				// second level domain fault
	default:
		if(!userureg(ureg)){
			dumpregs(ureg);
			panic("fault: %s addr=%#p", read ? "read" : "write", addr);
		}
		checkpages();
		sprint(buf, "sys: trap: fault %s addr=%#p", read ? "read" : "write", addr);
		postnote(up, 1, buf, NDebug);
	}

	if(!userureg(ureg))
		poperror();

	up->insyscall = insyscall;
}

int
userureg(Ureg* ureg)
{
	return (ureg->psr & 15) == 0;
}

uintptr
userpc(void)
{
	Ureg *ur = up->dbgreg;
	return ur->pc;
}

uintptr
dbgpc(Proc *)
{
	Ureg *ur = up->dbgreg;
	if(ur == nil)
		return 0;
	return ur->pc;
}

void
procfork(Proc *p)
{
	int s;

	s = splhi();
	switch(up->fpstate & ~FPillegal){
	case FPactive:
		fpsave(up->fpsave);
		up->fpstate = FPinactive;
	case FPinactive:
		memmove(p->fpsave, up->fpsave, sizeof(FPsave));
		p->fpstate = FPinactive;
	}
	splx(s);

	p->tpidr = up->tpidr;

	p->kentry = up->kentry;
	p->pcycles = -p->kentry;
}

void
procsetup(Proc *p)
{
	p->fpstate = FPinit;
	fpoff();

	p->tpidr = 0;
	syswr(TPIDR_EL0, p->tpidr);

	p->kentry = cycles();
	p->pcycles = -p->kentry;
}

void
procsave(Proc *p)
{
	uvlong t;

	if(p->fpstate == FPactive){
		if(p->state == Moribund)
			fpclear();
		else
			fpsave(p->fpsave);
		p->fpstate = FPinactive;
	}

	if(p->kp == 0)
		p->tpidr = sysrd(TPIDR_EL0);

	putasid(p);	// release asid

	t = cycles();
	p->kentry -= t;
	p->pcycles += t;
}

void
procrestore(Proc *p)
{
	uvlong t;

	if(p->kp)
		return;
	
	syswr(TPIDR_EL0, p->tpidr);

	t = cycles();
	p->kentry += t;
	p->pcycles -= t;
}

static void
linkproc(void)
{
	spllo();
	up->kpfun(up->kparg);
	pexit("kproc dying", 0);
}

void
kprocchild(Proc* p, void (*func)(void*), void* arg)
{
	p->sched.pc = (uintptr) linkproc;
	p->sched.sp = (uintptr) p->kstack + KSTACK - 16;
	*(void**)p->sched.sp = kprocchild;	/* fake */

	p->kpfun = func;
	p->kparg = arg;
}

void
forkchild(Proc *p, Ureg *ureg)
{
	Ureg *cureg;

	p->sched.pc = (uintptr) forkret;
	p->sched.sp = (uintptr) p->kstack + KSTACK - TRAPFRAMESIZE;

	cureg = (Ureg*) (p->sched.sp + 16);
	memmove(cureg, ureg, sizeof(Ureg));
	cureg->r0 = 0;

	p->psstate = 0;
	p->insyscall = 0;
}

uintptr
execregs(uintptr entry, ulong ssize, ulong nargs)
{
	uintptr *sp;
	Ureg *ureg;

	sp = (uintptr*)(USTKTOP - ssize);
	*--sp = nargs;

	ureg = up->dbgreg;
	ureg->sp = (uintptr)sp;
	ureg->pc = entry;
	ureg->link = 0;
	return USTKTOP-sizeof(Tos);
}

void
evenaddr(uintptr addr)
{
	if(addr & 3){
		postnote(up, 1, "sys: odd address", NDebug);
		error(Ebadarg);
	}
}

void
callwithureg(void (*f) (Ureg *))
{
	Ureg u;
	
	u.pc = getcallerpc(&f);
	u.sp = (uintptr) &f;
	f(&u);
}

void
setkernur(Ureg *ureg, Proc *p)
{
	ureg->pc = p->sched.pc;
	ureg->sp = p->sched.sp;
	ureg->link = (uintptr)sched;
}

void
setupwatchpts(Proc*, Watchpt*, int)
{
}

void
setregisters(Ureg* ureg, char* pureg, char* uva, int n)
{
	ulong v;

	v = ureg->psr;
	memmove(pureg, uva, n);
	ureg->psr = (ureg->psr & USPSRMASK) | (v & ~USPSRMASK);
}

static void
dumpstackwithureg(Ureg *ureg)
{
	uintptr v, estack, sp;
	char *s;
	int i;

	if((s = getconf("*nodumpstack")) != nil && strcmp(s, "0") != 0){
		iprint("dumpstack disabled\n");
		return;
	}
	iprint("ktrace /kernel/path %#p %#p %#p # pc, sp, link\n",
		ureg->pc, ureg->sp, ureg->link);
	delay(2000);

	sp = ureg->sp;
	if(sp < KZERO || (sp & 7) != 0)
		sp = (uintptr)&ureg;

	estack = (uintptr)m+MACHSIZE;
	if(up != nil && sp <= (uintptr)up->kstack+KSTACK)
		estack = (uintptr)up->kstack+KSTACK;

	if(sp > estack){
		if(up != nil)
			iprint("&up->kstack %#p sp %#p\n", up->kstack, sp);
		else
			iprint("&m %#p sp %#p\n", m, sp);
		return;
	}

	i = 0;
	for(; sp < estack; sp += sizeof(uintptr)){
		v = *(uintptr*)sp;
		if(KTZERO < v && v < (uintptr)etext && (v & 3) == 0){
			iprint("%#8.8lux=%#8.8lux ", (ulong)sp, (ulong)v);
			i++;
		}
		if(i == 4){
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
	callwithureg(dumpstackwithureg);
}

void
dumpregs(Ureg *ureg)
{
	u64int *r;
	int i, x;

	x = splhi();
	if(up != nil)
		iprint("cpu%d: dumpregs ureg %#p process %lud: %s\n", m->machno, ureg,
			up->pid, up->text);
	else
		iprint("cpu%d: dumpregs ureg %#p\n", m->machno, ureg);
	r = &ureg->r0;
	for(i = 0; i < 30; i += 3)
		iprint("R%d %.16llux  R%d %.16llux  R%d %.16llux\n", i, r[i], i+1, r[i+1], i+2, r[i+2]);
	iprint("PC %#p  SP %#p  LR %#p  PSR %llux  TYPE %llux\n",
		ureg->pc, ureg->sp, ureg->link,
		ureg->psr, ureg->type);
	splx(x);
}
