#include "u.h"
#include <ureg.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "tos.h"

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
	iprint("cpu%d: dumpstack\n", m->machno);

	x = 0;
	x += iprint("ktrace /arm/9zynq %.8lux %.8lux %.8lux <<EOF\n", ureg->pc, ureg->sp, ureg->r14);
	i = 0;
	if(up
	&& (uintptr)&l >= (uintptr)up->kstack
	&& (uintptr)&l <= (uintptr)up->kstack+KSTACK)
		estack = (uintptr)up->kstack+KSTACK;
	else if((uintptr)&l >= (uintptr)m->stack
	&& (uintptr)&l <= (uintptr)m+MACHSIZE)
		estack = (uintptr)m+MACHSIZE;
	else
		return;
	x += iprint("estackx %p\n", estack);

	for(l = (uintptr)&l; l < estack; l += sizeof(uintptr)){
		v = *(uintptr*)l;
		if((KTZERO < v && v < (uintptr)&etext) || estack-l < 32){
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

static char*
faulterr[0x20] = {
[0x01]	"alignement fault",
[0x02]	"debug event",
[0x04]	"fault on instruction cache maintenance",
[0x08]	"synchronous external abort",
[0x0C]	"synchronous external abort on translation table walk L1",
[0x0E]	"synchronous external abort on translation table walk L2",
[0x10]	"tlb conflict abort",
[0x16]	"asynchronous external abort",
[0x19]	"synchronous parity error on memory access",
[0x1C]	"synchronous parity error on translation table walk L1",
[0x1E]	"synchronous parity error on translation table walk L2",
};

static void
faultarm(Ureg *ureg, ulong fsr, uintptr addr)
{
	int user, insyscall, read;
	static char buf[ERRMAX];
	char *err;

	read = (fsr & (1<<11)) == 0;
	user = userureg(ureg);
	if(!user){
		if(addr >= USTKTOP || up == nil)
			_dumpstack(ureg);
		if(addr >= USTKTOP)
			panic("kernel fault: bad address pc=%#.8lux addr=%#.8lux fsr=%#.8lux", ureg->pc, addr, fsr);
		if(up == nil)
			panic("kernel fault: no user process pc=%#.8lux addr=%#.8lux fsr=%#.8lux", ureg->pc, addr, fsr);
	}
	if(up == nil)
		panic("user fault: up=nil pc=%#.8lux addr=%#.8lux fsr=%#.8lux", ureg->pc, addr, fsr);

	insyscall = up->insyscall;
	up->insyscall = 1;
	switch(fsr & 0x1F){
	case 0x05:	/* translation fault L1 */
	case 0x07:	/* translation fault L2 */
	case 0x03:	/* access flag fault L1 */
	case 0x06:	/* access flag fault L2 */
	case 0x09:	/* domain fault L1 */
	case 0x0B:	/* domain fault L2 */
	case 0x0D:	/* permission fault L1 */
	case 0x0F:	/* permission fault L2 */
		if(fault(addr, ureg->pc, read) == 0)
			break;
		/* wet floor */
	default:
		err = faulterr[fsr & 0x1F];
		if(err == nil)
			err = "fault";
		if(!user){
			dumpregs(ureg);
			_dumpstack(ureg);
			panic("kernel %s: pc=%#.8lux addr=%#.8lux fsr=%#.8lux", err, ureg->pc, addr, fsr);
		}
		sprint(buf, "sys: trap: %s %s addr=%#.8lux", err, read ? "read" : "write", addr);
		postnote(up, 1, buf, NDebug);
	}
	up->insyscall = insyscall;
}

static void
mathtrap(Ureg *, ulong)
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

void
trap(Ureg *ureg)
{
	int user;
	ulong opc, cp;

	user = userureg(ureg);
	if(user){
		if(up == nil)
			panic("user trap: up=nil");
		up->dbgreg = ureg;
		cycles(&up->kentry);
	}
	switch(ureg->type){
	case PsrMund:
		ureg->pc -= 4;
		if(user){
			spllo();
			if(okaddr(ureg->pc, 4, 0)){
				opc = *(ulong*)ureg->pc;
				if((opc & 0x0f000000) == 0x0e000000 || (opc & 0x0e000000) == 0x0c000000){
					cp = opc >> 8 & 15;
					if(cp == 10 || cp == 11){
						mathtrap(ureg, opc);
						break;
					}
				}
			}
			postnote(up, 1, "sys: trap: invalid opcode", NDebug);
			break;
		}
		dumpregs(ureg);
		panic("invalid opcode at pc=%#.8lux lr=%#.8lux", ureg->pc, ureg->r14);
		break;
	case PsrMiabt:
		ureg->pc -= 4;
		faultarm(ureg, getifsr(), getifar());
		break;
	case PsrMabt:
		ureg->pc -= 8;
		faultarm(ureg, getdfsr(), getdfar());
		break;
	case PsrMirq:
		ureg->pc -= 4;
		intr(ureg);
		break;
	default:
		iprint("cpu%d: unknown trap type %ulx\n", m->machno, ureg->type);
	}
	splhi();
	if(user){
		if(up->procctl || up->nnote)
			notify(ureg);
		kexit(ureg);
	}
}

#include "../port/systab.h"

void
syscall(Ureg *ureg)
{
	char *e;
	uintptr sp;
	long ret;
	int i, s;
	ulong scallnr;
	vlong startns, stopns;
	
	if(!userureg(ureg))
		panic("syscall: pc=%#.8lux", ureg->pc);
	
	cycles(&up->kentry);
	
	m->syscall++;
	up->insyscall = 1;
	up->pc = ureg->pc;
	up->dbgreg = ureg;
	
	sp = ureg->sp;
	up->scallnr = scallnr = ureg->r0;

	spllo();
	
	up->nerrlab = 0;
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
		
		if(scallnr >= nsyscall || systab[scallnr] == 0){
			pprint("bad sys call number %lud pc %lux", scallnr, ureg->pc);
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
			print("sp=%lux pc=%lux\n", up->errlab[i].sp, up->errlab[i].pc);
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
	if(scallnr == NOTED)
		noted(ureg, *((ulong *) up->s.args));

	if(scallnr != RFORK && (up->procctl || up->nnote)){
		splhi();
		notify(ureg);
	}
	if(up->delaysched)
		sched();
	kexit(ureg);
	splhi();
}

int
notify(Ureg *ureg)
{
	int l;
	ulong s, sp;
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
		if(l > ERRMAX-15)	/* " pc=0x12345678\0" */
			l = ERRMAX-15;
		sprint(n->msg+l, " pc=0x%.8lux", ureg->pc);
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

	if(!okaddr((uintptr)up->notify, 1, 0)
	|| !okaddr(sp-ERRMAX-4*BY2WD, sizeof(Ureg)+ERRMAX+4*BY2WD, 1)
	|| ((uintptr) up->notify & 3) != 0
	|| (sp & 3) != 0){
		qunlock(&up->debug);
		pprint("suicide: bad address in notify\n");
		pexit("Suicide", 0);
	}

	memmove((Ureg*)sp, ureg, sizeof(Ureg));
	*(Ureg**)(sp-BY2WD) = up->ureg;	/* word under Ureg is old up->ureg */
	up->ureg = (void*)sp;
	sp -= BY2WD+ERRMAX;
	memmove((char*)sp, up->note[0].msg, ERRMAX);
	sp -= 3*BY2WD;
	*(ulong*)(sp+2*BY2WD) = sp+3*BY2WD;
	*(ulong*)(sp+1*BY2WD) = (ulong)up->ureg;
	ureg->r0 = (uintptr) up->ureg;
	ureg->sp = sp;
	ureg->pc = (uintptr) up->notify;
	ureg->r14 = 0;
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
	ulong oureg, sp;
	
	qlock(&up->debug);
	if(arg0 != NRSTR && !up->notified){
		qunlock(&up->debug);
		pprint("call to noted() when not notified\n");
		pexit("Suicide", 0);
	}
	up->notified = 0;
	
	nureg = up->ureg;
	up->fpstate &= ~FPillegal;
	
	oureg = (ulong) nureg;
	if(!okaddr(oureg - BY2WD, BY2WD + sizeof(Ureg), 0) || (oureg & 3) != 0){
		qunlock(&up->debug);
		pprint("bad ureg in noted or call to noted when not notified\n");
		pexit("Suicide", 0);
	}
	
	nureg->psr = nureg->psr & 0xf80f0000 | ureg->psr & 0x07f0ffff;
	
	memmove(ureg, nureg, sizeof(Ureg));
	
	switch(arg0){
	case NCONT: case NRSTR:
		if(!okaddr(nureg->pc, BY2WD, 0) || !okaddr(nureg->sp, BY2WD, 0) ||
				(nureg->pc & 3) != 0 || (nureg->sp & 3) != 0){
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		up->ureg = (Ureg *) (*(ulong *) (oureg - BY2WD));
		qunlock(&up->debug);
		break;
	
	case NSAVE:
		if(!okaddr(nureg->pc, BY2WD, 0) || !okaddr(nureg->sp, BY2WD, 0) ||
				(nureg->pc & 3) != 0 || (nureg->sp & 3) != 0){
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		qunlock(&up->debug);
		sp = oureg - 4 * BY2WD - ERRMAX;
		splhi();
		ureg->sp = sp;
		ureg->r0 = (uintptr) oureg;
		((ulong *) sp)[1] = oureg;
		((ulong *) sp)[0] = 0;
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
dumpstack(void)
{
	callwithureg(_dumpstack);
}

void
dumpregs(Ureg *ureg)
{
	iprint("trap: %lux psr %8.8lux type %2.2lux pc %8.8lux link %8.8lux\n",
		ureg->type, ureg->psr, ureg->type, ureg->pc, ureg->link);
	iprint("R14 %8.8lux R13 %8.8lux R12 %8.8lux R11 %8.8lux R10 %8.8lux\n",
		ureg->r14, ureg->r13, ureg->r12, ureg->r11, ureg->r10);
	iprint("R9  %8.8lux R8  %8.8lux R7  %8.8lux R6  %8.8lux R5  %8.8lux\n",
		ureg->r9, ureg->r8, ureg->r7, ureg->r6, ureg->r5);
	iprint("R4  %8.8lux R3  %8.8lux R2  %8.8lux R1  %8.8lux R0  %8.8lux\n",
		ureg->r4, ureg->r3, ureg->r2, ureg->r1, ureg->r0);
	iprint("pc %#lux link %#lux\n", ureg->pc, ureg->link);
}

void
setkernur(Ureg *ureg, Proc *p)
{
	ureg->pc = p->sched.pc;
	ureg->sp = p->sched.sp + 4;
	ureg->r14 = (uintptr) sched;
}

void
setregisters(Ureg* ureg, char* pureg, char* uva, int n)
{
	ulong v;

	v = ureg->psr;
	memmove(pureg, uva, n);
	ureg->psr = ureg->psr & 0xf80f0000 | v & 0x07f0ffff;
}

void
callwithureg(void (*f) (Ureg *))
{
	Ureg u;
	
	u.pc = getcallerpc(&f);
	u.sp = (uintptr) &f - 4;
	f(&u);
}

uintptr
userpc(void)
{
	Ureg *ur;
	
	ur = up->dbgreg;
	return ur->pc;
}

uintptr
dbgpc(Proc *)
{
	Ureg *ur;
	
	ur = up->dbgreg;
	if(ur == nil)
		return 0;
	return ur->pc;
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
	cycles(&t);
	p->kentry -= t;
	p->pcycles += t;

	l1switch(&m->l1, 0);
}

void
procrestore(Proc *p)
{
	uvlong t;

	if(p->kp)
		return;

	cycles(&t);
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
	p->sched.sp = (uintptr) p->kstack + KSTACK;

	p->kpfun = func;
	p->kparg = arg;
}

void
forkchild(Proc *p, Ureg *ureg)
{
	Ureg *cureg;

	p->sched.pc = (uintptr) forkret;
	p->sched.sp = (uintptr) p->kstack + KSTACK - sizeof(Ureg);

	cureg = (Ureg*) p->sched.sp;
	memmove(cureg, ureg, sizeof(Ureg));
	cureg->r0 = 0;
}

uintptr
execregs(uintptr entry, ulong ssize, ulong nargs)
{
	ulong *sp;
	Ureg *ureg;

	sp = (ulong*)(USTKTOP - ssize);
	*--sp = nargs;

	ureg = up->dbgreg;
	ureg->sp = (uintptr) sp;
	ureg->pc = entry;
	ureg->r14 = 0;
	return USTKTOP-sizeof(Tos);
}
