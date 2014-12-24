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
	iprint("dumpstack\n");

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

static void
faultarm(Ureg *ureg, ulong fsr, uintptr addr)
{
	int user, insyscall, read, n;
	static char buf[ERRMAX];
	
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
	n = fault(addr, read);
	if(n < 0){
		if(!user){
			dumpregs(ureg);
			_dumpstack(ureg);
			panic("kernel fault: pc=%#.8lux addr=%#.8lux fsr=%#.8lux", ureg->pc, addr, fsr);
		}
		sprint(buf, "sys: trap: fault %s addr=%#.8lux", read ? "read" : "write", addr);
		postnote(up, 1, buf, NDebug);
	}
	up->insyscall = insyscall;
}

static void
mathtrap(Ureg *, ulong)
{
	if((up->fpstate & FPillegal) != 0){
		postnote(up, 1, "sys: floating point in note handler", NDebug);
		return;
	}
	switch(up->fpstate){
	case FPinit:
		fpinit();
		up->fpstate = FPactive;
		break;
	case FPinactive:
		fprestore(&up->fpsave);
		up->fpstate = FPactive;
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
		print("unknown trap type %ulx\n", ureg->type);
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
			procctl(up);
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
		procctl(up);
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
		procctl(up);
	if(up->nnote == 0)
		return 0;

	if(up->fpstate == FPactive){
		fpsave(&up->fpsave);
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
dumpregs(Ureg *)
{
	print("dumpregs\n");
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
			fpsave(&p->fpsave);
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

	p->psstate = 0;
	p->insyscall = 0;
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
