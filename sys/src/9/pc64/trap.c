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

void	noted(Ureg*, ulong);

static void debugexc(Ureg*, void*);
static void debugbpt(Ureg*, void*);
static void faultamd64(Ureg*, void*);
static void doublefault(Ureg*, void*);
static void unexpected(Ureg*, void*);
static void _dumpstack(Ureg*);

void
trapinit0(void)
{
	u32int d1, v;
	uintptr vaddr;
	Segdesc *idt;
	uintptr ptr[2];

	idt = (Segdesc*)IDTADDR;
	vaddr = (uintptr)vectortable;
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

		idt->d0 = (vaddr & 0xFFFF)|(KESEL<<16);
		idt->d1 = d1;
		idt++;

		idt->d0 = (vaddr >> 32);
		idt->d1 = 0;
		idt++;

		vaddr += 6;
	}
	((ushort*)&ptr[1])[-1] = sizeof(Segdesc)*512-1;
	ptr[1] = IDTADDR;
	lidt(&((ushort*)&ptr[1])[-1]);
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
	trapenable(VectorPF, faultamd64, 0, "faultamd64");
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
		snprint(buf, sizeof(buf), "sys: trap: %s", excname[vno]);
		postnote(up, 1, buf, NDebug);
		return 1;
	}
	return 0;
}

void
trap(Ureg *ureg)
{
	int vno, user;
	FPsave *f = nil;

	vno = ureg->type;
	user = kenter(ureg);
	if(vno != VectorCNA)
		f = fpukenter(ureg);

	if(!irqhandled(ureg, vno) && (!user || !usertrap(vno))){
		if(!user){
			void (*pc)(void);

			extern void _rdmsrinst(void);
			extern void _wrmsrinst(void);
			extern void _peekinst(void);

			pc = (void*)ureg->pc;
			if(pc == _rdmsrinst || pc == _wrmsrinst){
				if(vno == VectorGPF){
					ureg->bp = -1;
					ureg->pc += 2;
					goto out;
				}
			} else if(pc == _peekinst){
				if(vno == VectorGPF || vno == VectorPF){
					ureg->pc += 2;
					goto out;
				}
			}

			/* early fault before trapinit() */
			if(vno == VectorPF)
				faultamd64(ureg, 0);
		}

		dumpregs(ureg);
		if(!user){
			ureg->sp = (uintptr)&ureg->sp;
			_dumpstack(ureg);
		}
		if(vno < nelem(excname))
			panic("%s", excname[vno]);
		panic("unknown trap/intr: %d", vno);
	}
out:
	splhi();
	if(user){
		if(up->procctl || up->nnote)
			notify(ureg);
		kexit(ureg);
	}
	if(vno != VectorCNA)
		fpukexit(ureg, f);
}

void
dumpregs(Ureg* ureg)
{
	if(up)
		iprint("cpu%d: registers for %s %lud\n",
			m->machno, up->text, up->pid);
	else
		iprint("cpu%d: registers for kernel\n", m->machno);

	iprint("  AX %.16lluX  BX %.16lluX  CX %.16lluX\n",
		ureg->ax, ureg->bx, ureg->cx);
	iprint("  DX %.16lluX  SI %.16lluX  DI %.16lluX\n",
		ureg->dx, ureg->si, ureg->di);
	iprint("  BP %.16lluX  R8 %.16lluX  R9 %.16lluX\n",
		ureg->bp, ureg->r8, ureg->r9);
	iprint(" R10 %.16lluX R11 %.16lluX R12 %.16lluX\n",
		ureg->r10, ureg->r11, ureg->r12);
	iprint(" R13 %.16lluX R14 %.16lluX R15 %.16lluX\n",
		ureg->r13, ureg->r14, ureg->r15);
	iprint("  CS %.4lluX   SS %.4lluX    PC %.16lluX  SP %.16lluX\n",
		ureg->cs & 0xffff, ureg->ss & 0xffff, ureg->pc, ureg->sp);
	iprint("TYPE %.2lluX  ERROR %.4lluX FLAGS %.8lluX\n",
		ureg->type & 0xff, ureg->error & 0xffff, ureg->flags & 0xffffffff);

	/*
	 * Processor control registers.
	 * If machine check exception, time stamp counter, page size extensions
	 * or enhanced virtual 8086 mode extensions are supported, there is a
	 * CR4. If there is a CR4 and machine check extensions, read the machine
	 * check address and machine check type registers if RDMSR supported.
	 */
	iprint(" CR0 %8.8llux CR2 %16.16llux CR3 %16.16llux",
		getcr0(), getcr2(), getcr3());
	if(m->cpuiddx & (Mce|Tsc|Pse|Vmex)){
		iprint(" CR4 %16.16llux\n", getcr4());
		if(ureg->type == 18)
			dumpmcregs();
	}
	iprint("  ur %#p up %#p\n", ureg, up);
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
	ureg.sp = (uintptr)&fn;
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
	x += iprint("ktrace /kernel/path %#p %#p <<EOF\n", ureg->pc, ureg->sp);
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
			x += iprint("%.8lux=%.8lux ", (ulong)l, (ulong)v);
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

	if(ureg->type != VectorNMI)
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
	u64int dr6, m;
	char buf[ERRMAX];
	char *p, *e;
	int i;

	dr6 = getdr6();
	if(up == nil)
		panic("kernel debug exception dr6=%#.8ullx", dr6);
	putdr6(up->dr[6]);
	if(userureg(ureg))
		qlock(&up->debug);
	else if(!canqlock(&up->debug))
		return;
	m = up->dr[7];
	m = (m >> 4 | m >> 3) & 8 | (m >> 3 | m >> 2) & 4 | (m >> 2 | m >> 1) & 2 | (m >> 1 | m) & 1;
	m &= dr6;
	if(m == 0){
		snprint(buf, sizeof(buf), "sys: debug exception dr6=%#.8ullx", dr6);
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
	print("unexpected trap %llud; ignoring\n", ureg->type);
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

static void
faultamd64(Ureg* ureg, void*)
{
	uintptr addr;
	int read, user;

	addr = getcr2();
	read = !(ureg->error & 2);
	user = userureg(ureg);
	if(user)
		up->insyscall = 1;
	else {
		extern void _peekinst(void);

		if((void(*)(void))ureg->pc == _peekinst){
			ureg->pc += 2;
			return;
		}
		if(addr >= USTKTOP)
			panic("kernel fault: bad address pc=%#p addr=%#p", ureg->pc, addr);
		if(up == nil)
			panic("kernel fault: no user process pc=%#p addr=%#p", ureg->pc, addr);
		if(waserror()){
			if(up->nerrlab == 0){
				pprint("suicide: sys: %s\n", up->errstr);
				pexit(up->errstr, 1);
			}
			nexterror();
		}
	}

	if(fault(addr, ureg->pc, read))
		faultnote(ureg, read? "read": "write", addr);

	if(user)
		up->insyscall = 0;
	else
		poperror();
}

/*
 *  system calls
 */
#include "../port/systab.h"

/*
 *  Syscall is called directly from assembler without going through trap().
 */
void
syscall(Ureg* ureg)
{
	char *e;
	uintptr	sp;
	long long ret;
	int i, s;
	ulong scallnr;
	vlong startns, stopns;

	if(!kenter(ureg))
		panic("syscall: cs 0x%4.4lluX", ureg->cs);
	fpukenter(ureg);

	m->syscall++;
	up->insyscall = 1;

	up->pc = ureg->pc;
	sp = ureg->sp;
	scallnr = ureg->bp;	/* RARG */
	up->scallnr = scallnr;
	spllo();

	ret = -1;
	startns = 0;
	up->nerrlab = 0;
	if(!waserror()){
		if(sp<(USTKTOP-BY2PG) || sp>(USTKTOP-sizeof(Sargs)-BY2WD))
			validaddr(sp, sizeof(Sargs)+BY2WD, 0);

		up->s = *((Sargs*)(sp+BY2WD));
		if(0){
			syscallfmt(scallnr, ureg->pc, (va_list)up->s.args);
			print("syscall: %s\n", up->syscalltrace);
		}

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
		if(0 && up->pid == 1)
			print("syscall %lud error %s\n", scallnr, up->syserrstr);
	}
	if(up->nerrlab){
		print("bad errstack [%lud]: %d extra\n", scallnr, up->nerrlab);
		for(i = 0; i < NERR; i++)
			print("sp=%#p pc=%#p\n",
				up->errlab[i].sp, up->errlab[i].pc);
		panic("error stack");
	}
	ureg->ax = ret;

	if(0){
		print("syscallret: %lud %s %s ret=%lld\n", 
			up->pid, up->text, sysctab[scallnr], ret);
	}

	if(up->procctl == Proc_tracesyscall){
		stopns = todget(nil);
		sysretfmt(scallnr, (va_list)up->s.args, ret, startns, stopns);
		s = splhi();
		up->procctl = Proc_stopme;
		procctl();
		splx(s);
	}

	if(scallnr == NOTED){
		/*
		 * normally, syscall() returns to forkret()
		 * not restoring general registers when going
		 * to userspace. to completely restore the
		 * interrupted context, we have to return thru
		 * noteret(). we override return pc to jump to
		 * to it when returning form syscall()
		 */
		((void**)&ureg)[-1] = (void*)noteret;

		noted(ureg, *((ulong*)up->s.args));
		splhi();
		up->fpstate &= ~FPillegal;
	}
	else
		splhi();

	if(scallnr != RFORK && (up->procctl || up->nnote) && notify(ureg))
		((void**)&ureg)[-1] = (void*)noteret;	/* loads RARG */

	up->insyscall = 0;
	up->psstate = nil;

	/* if we delayed sched because we held a lock, sched now */
	if(up->delaysched){
		sched();
		splhi();
	}

	kexit(ureg);
	fpukexit(ureg, nil);
}

/*
 *  Call user, if necessary, with note.
 *  Pass user the Ureg struct and the note on his stack.
 */
int
notify(Ureg* ureg)
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
if(0) print("%s %lud: notify %#p %#p %#p %s\n",
	up->text, up->pid, ureg->pc, ureg->sp, sp, msg);

	if(!okaddr((uintptr)up->notify, 1, 0)
	|| !okaddr(sp-ERRMAX-4*BY2WD, sizeof(Ureg)+ERRMAX+4*BY2WD, 1)){
		qunlock(&up->debug);
		pprint("suicide: bad address in notify\n");
		pexit("Suicide", 0);
	}

	memmove((Ureg*)sp, ureg, sizeof(Ureg));
	*(Ureg**)(sp-BY2WD) = up->ureg;	/* word under Ureg is old up->ureg */
	up->ureg = (void*)sp;
	sp -= BY2WD+ERRMAX;
	memmove((char*)sp, msg, ERRMAX);
	sp -= 3*BY2WD;
	((uintptr*)sp)[2] = sp + 3*BY2WD;	/* arg2 string */
	((uintptr*)sp)[1] = (uintptr)up->ureg;	/* arg1 is ureg* */
	((uintptr*)sp)[0] = 0;			/* arg0 is pc */
	ureg->sp = sp;
	ureg->pc = (uintptr)up->notify;
	ureg->bp = (uintptr)up->ureg;		/* arg1 passed in RARG */
	ureg->cs = UESEL;
	ureg->ss = UDSEL;
	qunlock(&up->debug);
	splhi();
	fpuprocsave(up);
	up->fpstate |= FPillegal;
	return 1;
}

/*
 *   Return user to state before notify()
 */
void
noted(Ureg* ureg, ulong arg0)
{
	Ureg *nureg;
	uintptr oureg, sp;

	qlock(&up->debug);
	if(arg0!=NRSTR && !up->notified) {
		qunlock(&up->debug);
		pprint("call to noted() when not notified\n");
		pexit("Suicide", 0);
	}
	up->notified = 0;

	nureg = up->ureg;	/* pointer to user returned Ureg struct */

	/* sanity clause */
	oureg = (uintptr)nureg;
	if(!okaddr(oureg-BY2WD, BY2WD+sizeof(Ureg), 0)){
		qunlock(&up->debug);
		pprint("bad ureg in noted or call to noted when not notified\n");
		pexit("Suicide", 0);
	}

	/* don't let user change system flags or segment registers */
	setregisters(ureg, (char*)ureg, (char*)nureg, sizeof(Ureg));

	switch(arg0){
	case NCONT:
	case NRSTR:
if(0) print("%s %lud: noted %#p %#p\n",
	up->text, up->pid, nureg->pc, nureg->sp);
		if(!okaddr(nureg->pc, 1, 0) || !okaddr(nureg->sp, BY2WD, 0)){
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		up->ureg = (Ureg*)(*(uintptr*)(oureg-BY2WD));
		qunlock(&up->debug);
		break;

	case NSAVE:
		if(!okaddr(nureg->pc, 1, 0)
		|| !okaddr(nureg->sp, BY2WD, 0)){
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		qunlock(&up->debug);
		sp = oureg-4*BY2WD-ERRMAX;
		ureg->sp = sp;
		ureg->bp = oureg;		/* arg 1 passed in RARG */
		((uintptr*)sp)[1] = oureg;	/* arg 1 0(FP) is ureg* */
		((uintptr*)sp)[0] = 0;		/* arg 0 is pc */
		break;

	default:
		up->lastnote->flag = NDebug;
		/* fall through */

	case NDFLT:
		qunlock(&up->debug);
		if(up->lastnote->flag == NDebug)
			pprint("suicide: %s\n", up->lastnote->msg);
		pexit(up->lastnote->msg, up->lastnote->flag!=NDebug);
	}
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
	ureg->cs = UESEL;
	ureg->ss = UDSEL;
	ureg->r14 = ureg->r15 = 0;	/* extern user registers */
	return (uintptr)USTKTOP-sizeof(Tos);		/* address of kernel/user shared data */
}

/*
 *  return the userpc the last exception happened at
 */
uintptr
userpc(void)
{
	Ureg *ureg;

	ureg = (Ureg*)up->dbgreg;
	return ureg->pc;
}

/* This routine must save the values of registers the user is not permitted
 * to write from devproc and noted() and then restore the saved values before returning.
 */
void
setregisters(Ureg* ureg, char* pureg, char* uva, int n)
{
	u64int flags;

	flags = ureg->flags;
	memmove(pureg, uva, n);
	ureg->cs = UESEL;
	ureg->ss = UDSEL;
	ureg->flags = (ureg->flags & 0x00ff) | (flags & 0xff00);
	ureg->pc &= UADDRMASK;
}

void
kprocchild(Proc *p, void (*entry)(void))
{
	/*
	 * gotolabel() needs a word on the stack in
	 * which to place the return PC used to jump
	 * to linkproc().
	 */
	p->sched.pc = (uintptr)entry;
	p->sched.sp = (uintptr)p - BY2WD;
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
	p->sched.sp = (uintptr)p - (sizeof(Ureg)+2*BY2WD);
	p->sched.pc = (uintptr)forkret;

	cureg = (Ureg*)(p->sched.sp+2*BY2WD);
	memmove(cureg, ureg, sizeof(Ureg));

	cureg->ax = 0;
}

/* Give enough context in the ureg to produce a kernel stack for
 * a sleeping process
 */
void
setkernur(Ureg* ureg, Proc* p)
{
	ureg->pc = p->sched.pc;
	ureg->sp = p->sched.sp+8;
	ureg->r14 = (uintptr)p;
}

uintptr
dbgpc(Proc *p)
{
	Ureg *ureg;

	ureg = p->dbgreg;
	if(ureg == nil)
		return 0;
	return ureg->pc;
}
