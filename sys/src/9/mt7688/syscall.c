#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "../port/systab.h"

#include "tos.h"
#include "ureg.h"

FPsave initfp;


/*
 * called directly from assembler, not via trap()
 */
void
syscall(Ureg* ureg)
{
	char *e;
	u32int s;
	ulong sp;
	long ret;
	int i;
	vlong startns, stopns;
	ulong scallnr;

	if(!kenter(ureg))
		panic("syscall from kernel");

	m->syscall++;
	up->insyscall = 1;
	up->pc = ureg->pc;

	scallnr = ureg->r1;
	up->scallnr = ureg->r1;
	spllo();
	sp = ureg->sp;

	up->nerrlab = 0;
	ret = -1;

	if(!waserror()){
		if(sp < (USTKTOP-BY2PG) || sp > (USTKTOP-sizeof(Sargs)))
			validaddr(sp, sizeof(Sargs), 0);

		up->s = *((Sargs*)(sp));	/* spim's libc is different to mips ... */

		if(up->procctl == Proc_tracesyscall){
			syscallfmt(scallnr, ureg->pc, (va_list)up->s.args);
			s = splhi();
			up->procctl = Proc_stopme;
			procctl();
			splx(s);
			startns = todget(nil);
		}

		if(scallnr >= nsyscall || systab[scallnr] == nil){
			postnote(up, 1, "sys: bad sys call", NDebug);
			error(Ebadarg);
		}
		up->psstate = sysctab[scallnr];
		ret = systab[scallnr]((va_list)up->s.args);
		poperror();
	}else{
		/* failure: save the error buffer for errstr */
		e = up->syserrstr;
		up->syserrstr = up->errstr;
		up->errstr = e;
//		iprint("[%lud %s] syscall %lud: %s\n",up->pid, up->text, scallnr, up->errstr);
	}

	if(up->nerrlab){
		iprint("bad errstack [%lud]: %d extra\n", scallnr, up->nerrlab);
		for(i = 0; i < NERR; i++)
			iprint("sp=%#p pc=%#p\n",
				up->errlab[i].sp, up->errlab[i].pc);
		panic("error stack");
	}

	/*
	 *  Put return value in frame.  On the x86 the syscall is
	 *  just another trap and the return value from syscall is
	 *  ignored.  On other machines the return value is put into
	 *  the results register by caller of syscall.
	 */
	ureg->pc += 4;
	ureg->r1 = ret;

	if(up->procctl == Proc_tracesyscall){
		stopns = todget(nil);
		sysretfmt(scallnr, (va_list)up->s.args, ret, startns, stopns);
		s = splhi();
		up->procctl = Proc_stopme;
		procctl();
		splx(s);
	}

	up->insyscall = 0;
	up->psstate = 0;

	if(scallnr == NOTED)
		noted(ureg, *((ulong*)up->s.args));

	splhi();
	if(scallnr != RFORK && (up->procctl || up->nnote))
		notify(ureg);

	/* if we delayed sched because we held a lock, sched now */
	if(up->delaysched){
		sched();
		splhi();
	}

	kexit(ureg);

	/* restore EXL in status */
	setstatus(getstatus() | EXL);

}


int
notify(Ureg *ur)
{
	int s;
	ulong sp;
	char *msg;

	if(up->procctl)
		procctl();
	if(up->nnote == 0)
		return 0;

	s = spllo();
	qlock(&up->debug);
	up->fpstate |= FPillegal;
	msg = popnote(ur);
	if(msg == nil){
		qunlock(&up->debug);
		splx(s);
		return 0;
	}


	sp = ur->usp - sizeof(Ureg) - BY2WD; /* spim libc */

	if(!okaddr((ulong)up->notify, BY2WD, 0) ||
	   !okaddr(sp-ERRMAX-4*BY2WD, sizeof(Ureg)+ERRMAX+4*BY2WD, 1)) {
		iprint("suicide: bad address or sp in notify\n");
		qunlock(&up->debug);
		pexit("Suicide", 0);
	}

	memmove((Ureg*)sp, ur, sizeof(Ureg));	/* push user regs */
	*(Ureg**)(sp-BY2WD) = up->ureg;	/* word under Ureg is old up->ureg */
	up->ureg = (void*)sp;

	sp -= BY2WD+ERRMAX;
	memmove((char*)sp, msg, ERRMAX);	/* push err string */

	sp -= 3*BY2WD;
	*(ulong*)(sp+2*BY2WD) = sp+3*BY2WD;	/* arg 2 is string */
	ur->r1 = (long)up->ureg;		/* arg 1 is ureg* */
	((ulong*)sp)[1] = (ulong)up->ureg;	/* arg 1 0(FP) is ureg* */
	((ulong*)sp)[0] = 0;			/* arg 0 is pc */
	ur->usp = sp;
	/*
	 * arrange to resume at user's handler as if handler(ureg, errstr)
	 * were being called.
	 */
	ur->pc = (ulong)up->notify;

	qunlock(&up->debug);
	splx(s);
	return 1;
}


/*
 * Return user to state before notify(); called from user's handler.
 */
void
noted(Ureg *kur, ulong arg0)
{
	Ureg *nur;
	ulong oureg, sp;

	qlock(&up->debug);
	if(arg0!=NRSTR && !up->notified) {
		qunlock(&up->debug);
		pprint("call to noted() when not notified\n");
		pexit("Suicide", 0);
	}
	up->notified = 0;

	up->fpstate &= ~FPillegal;

	nur = up->ureg;

	oureg = (ulong)nur;
	if((oureg & (BY2WD-1)) || !okaddr((ulong)oureg-BY2WD, BY2WD+sizeof(Ureg), 0)){
		pprint("bad up->ureg in noted or call to noted() when not notified\n");
		qunlock(&up->debug);
		pexit("Suicide", 0);
	}

	setregisters(kur, (char*)kur, (char*)up->ureg, sizeof(Ureg));
	switch(arg0) {
	case NCONT:
	case NRSTR:				/* only used by APE */
		if(!okaddr(nur->pc, BY2WD, 0) || !okaddr(nur->usp, BY2WD, 0)){
			pprint("suicide: trap in noted\n");
			qunlock(&up->debug);
			pexit("Suicide", 0);
		}
		up->ureg = (Ureg*)(*(ulong*)(oureg-BY2WD));
		qunlock(&up->debug);
		splhi();
		break;

	case NSAVE:				/* only used by APE */
		if(!okaddr(nur->pc, BY2WD, 0) || !okaddr(nur->usp, BY2WD, 0)){
			pprint("suicide: trap in noted\n");
			qunlock(&up->debug);
			pexit("Suicide", 0);
		}
		qunlock(&up->debug);
		sp = oureg-4*BY2WD-ERRMAX;
		splhi();
		kur->sp = sp;
		kur->r1 = oureg;		/* arg 1 is ureg* */
		((ulong*)sp)[1] = oureg;	/* arg 1 0(FP) is ureg* */
		((ulong*)sp)[0] = 0;		/* arg 0 is pc */
		break;

	default:
		pprint("unknown noted arg %#lux\n", arg0);
		up->lastnote->flag = NDebug;
		/* fall through */

	case NDFLT:
		if(up->lastnote->flag == NDebug)
			pprint("suicide: %s\n", up->lastnote->msg);
		qunlock(&up->debug);
		pexit(up->lastnote->msg, up->lastnote->flag!=NDebug);
	}
}


void
forkchild(Proc *p, Ureg *ur)
{
	Ureg *cur;

//	iprint("%lud setting up for forking child %lud\n", up->pid, p->pid);
	p->sched.sp = (ulong)p - UREGSIZE;
	p->sched.pc = (ulong)forkret;

	cur = (Ureg*)(p->sched.sp+2*BY2WD);
	memmove(cur, ur, sizeof(Ureg));

	cur->r1 = 0;
	cur->pc += 4;
}


/* set up user registers before return from exec() */
uintptr
execregs(ulong entry, ulong ssize, ulong nargs)
{
	Ureg *ur;
	ulong *sp;

	sp = (ulong*)(USTKTOP - ssize);
	*--sp = nargs;

	ur = (Ureg*)up->dbgreg;
	ur->usp = (ulong)sp;
	ur->pc = entry - 4;		/* syscall advances it */

//	iprint("%lud: %s EXECREGS pc %#luX sp %#luX nargs %ld", up->pid, up->text, ur->pc, ur->usp, nargs);
//	delay(20);

	return USTKTOP-sizeof(Tos);	/* address of kernel/user shared data */
}
