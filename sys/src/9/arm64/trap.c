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

int	(*buserror)(Ureg*);

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

static char *traps[64] = {
	[0x00]	"sys: trap: unknown",
	[0x01]	"sys: trap: WFI or WFE instruction execution",
	[0x0E]	"sys: trap: illegal execution state",
	[0x18]	"sys: trap: illegal MSR/MRS access",
	[0x22]	"sys: trap: misaligned pc",
	[0x26]	"sys: trap: stack pointer misaligned",
	[0x30]	"sys: breakpoint",
	[0x32]	"sys: software step",
	[0x34]	"sys: watchpoint",
	[0x3C]	"sys: breakpoint",
};

void
trap(Ureg *ureg)
{
	FPsave *f = nil;
	u32int type, intr;
	int user;

	intr = ureg->type >> 32;
	if(intr == 2){
		fiq(ureg);
		return;
	}
	splflo();
	user = kenter(ureg);
	type = (u32int)ureg->type >> 26;
	switch(type){
	case 0x20:	// instruction abort from lower level
	case 0x21:	// instruction abort from same level
	case 0x24:	// data abort from lower level
	case 0x25:	// data abort from same level
		f = fpukenter(ureg);
		faultarm64(ureg);
		break;
	case 0x07:	// SIMD/FP
	case 0x2C:	// FPU exception (A64 only)
		mathtrap(ureg);
		break;
	case 0x00:	// unknown
		if(intr == 1){
			f = fpukenter(ureg);
			if(!irq(ureg))
				preempted();
			else if(up != nil && up->delaysched)
				sched();
			break;
		}
		if(intr == 3){
	case 0x2F:	// SError interrupt
			f = fpukenter(ureg);
			if(buserror != nil && (*buserror)(ureg))
				break;
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
		f = fpukenter(ureg);
		if(!userureg(ureg)){
			dumpregs(ureg);
			panic("unhandled trap");
		}
		if(traps[type] == nil) type = 0;	// unknown
		postnote(up, 1, traps[type], NDebug);
		break;
	}

	splhi();
	if(user){
		if(up->procctl || up->nnote)
			notify(ureg);
		kexit(ureg);
	}
	if(type != 0x07 && type != 0x2C)
		fpukexit(ureg, f);
}

void
syscall(Ureg *ureg)
{
	vlong startns, stopns;
	uintptr sp, ret;
	ulong scallnr;
	int i, s;
	char *e;

	if(!kenter(ureg))
		panic("syscall from  kernel");
	fpukenter(ureg);
	
	m->syscall++;
	up->insyscall = 1;
	up->pc = ureg->pc;
	
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

		splhi();
		up->fpstate &= ~FPillegal;
	}
	else
		splhi();

	if(scallnr != RFORK && (up->procctl || up->nnote))
		notify(ureg);

	if(up->delaysched){
		sched();
		splhi();
	}

	kexit(ureg);
	fpukexit(ureg, nil);
}

int
notify(Ureg *ureg)
{
	uintptr sp;
	char *msg;

	if(up->procctl)
		procctl();
	if(up->nnote == 0)
		return 0;

	spllo();
	qlock(&up->debug);
	msg = popnote(ureg);
	if(msg == nil){
		qunlock(&up->debug);
		splhi();
		return 0;
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
	memmove((char*)sp, msg, ERRMAX);
	sp -= 3*BY2WD;
	*(uintptr*)(sp+2*BY2WD) = sp+3*BY2WD;
	*(uintptr*)(sp+1*BY2WD) = (uintptr)up->ureg;
	ureg->r0 = (uintptr) up->ureg;
	ureg->sp = sp;
	ureg->pc = (uintptr) up->notify;
	ureg->link = 0;
	qunlock(&up->debug);

	splhi();
	fpuprocsave(up);
	up->fpstate |= FPillegal;
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
		ureg->sp = sp;
		ureg->r0 = (uintptr) oureg;
		((uintptr *) sp)[1] = oureg;
		((uintptr *) sp)[0] = 0;
		break;
	
	default:
		up->lastnote->flag = NDebug;
	
	case NDFLT:
		qunlock(&up->debug);
		if(up->lastnote->flag == NDebug)
			pprint("suicide: %s\n", up->lastnote->msg);
		pexit(up->lastnote->msg, up->lastnote->flag != NDebug);
	}
}

static void
faultnote(Ureg *ureg, char *access, uintptr addr)
{
	extern void checkpages(void);
	char buf[ERRMAX];

	if(!userureg(ureg)){
		dumpregs(ureg);
		panic("fault: %s addr=%#p", access, addr);
	}
	checkpages();
	snprint(buf, sizeof(buf), "sys: trap: fault %s addr=%#p", access, addr);
	postnote(up, 1, buf, NDebug);
}

void
faultarm64(Ureg *ureg)
{
	int user, read;
	uintptr addr;

	user = userureg(ureg);
	if(user)
		up->insyscall = 1;
	else {
		extern void _peekinst(void);

		if(ureg->pc == (uintptr)_peekinst){
			ureg->pc = ureg->link;
			return;
		}
		if(waserror()){
			if(up->nerrlab == 0){
				pprint("suicide: sys: %s\n", up->errstr);
				pexit(up->errstr, 1);
			}
			nexterror();
		}
	}

	addr = getfar();
	read = (ureg->type & (1<<6)) == 0;

	switch((u32int)ureg->type & 0x3F){
	case  4: case  5: case  6: case  7:	// Tanslation fault.
	case  8: case  9: case 10: case 11:	// Access flag fault.
	case 12: case 13: case 14: case 15:	// Permission fault.
	case 48:				// tlb conflict fault.
		if(fault(addr, ureg->pc, read) == 0)
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
		faultnote(ureg, read? "read": "write", addr);
	}

	if(user)
		up->insyscall = 0;
	else
		poperror();
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
	fpuprocfork(p);
	p->tpidr = up->tpidr;
}

void
procsetup(Proc *p)
{
	fpuprocsetup(p);
	p->tpidr = 0;
	syswr(TPIDR_EL0, p->tpidr);
}

void
procsave(Proc *p)
{
	fpuprocsave(p);
	if(p->kp == 0)
		p->tpidr = sysrd(TPIDR_EL0);
	putasid(p);	// release asid
}

void
procrestore(Proc *p)
{
	fpuprocrestore(p);
	if(p->kp == 0)
		syswr(TPIDR_EL0, p->tpidr);
}

void
kprocchild(Proc *p, void (*entry)(void))
{
	p->sched.pc = (uintptr) entry;
	p->sched.sp = (uintptr) p - 16;
	*(void**)p->sched.sp = kprocchild;	/* fake */
}

void
forkchild(Proc *p, Ureg *ureg)
{
	Ureg *cureg;

	p->sched.pc = (uintptr) forkret;
	p->sched.sp = (uintptr) p - TRAPFRAMESIZE;

	cureg = (Ureg*) (p->sched.sp + 16);
	memmove(cureg, ureg, sizeof(Ureg));
	cureg->r0 = 0;
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
	if(up != nil && sp <= (uintptr)up)
		estack = (uintptr)up;

	if(sp > estack){
		if(up != nil)
			iprint("&up %#p sp %#p\n", up, sp);
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
