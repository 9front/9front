/*
 * traps, exceptions, faults and interrupts on ar7161
 */
#include	"u.h"
#include	"tos.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"ureg.h"
#include	"io.h"
#include	"../port/error.h"

typedef struct Handler Handler;

struct Handler {
	void	(*handler)(Ureg*, void *);
	void	*arg;
	Handler	*next;			/* at this interrupt level */
};

int	intr(Ureg*);
void	kernfault(Ureg*, int);
void	noted(Ureg*, ulong);
void	rfnote(Ureg**);

char *excname[] =
{
	"trap: external interrupt",
	"trap: TLB modification (store to unwritable)",
	"trap: TLB miss (load or fetch)",
	"trap: TLB miss (store)",
	"trap: address error (load or fetch)",
	"trap: address error (store)",
	"trap: bus error (fetch)",
	"trap: bus error (data load or store)",
	"trap: system call",
	"breakpoint",
	"trap: reserved instruction",
	"trap: coprocessor unusable",
	"trap: arithmetic overflow",
	"trap: TRAP exception",
	"trap: VCE (instruction)",
	"trap: floating-point exception",
	"trap: coprocessor 2 implementation-specific", /* used as sys call for debugger */
	"trap: corextend unusable",
	"trap: precise coprocessor 2 exception",
	"trap: TLB read-inhibit",
	"trap: TLB execute-inhibit",
	"trap: undefined 21",
	"trap: undefined 22",
	"trap: WATCH exception",
	"trap: machine checkcore",
	"trap: undefined 25",
	"trap: undefined 26",
	"trap: undefined 27",
	"trap: undefined 28",
	"trap: undefined 29",
	"trap: cache error",
	"trap: VCE (data)",
};

char *fpcause[] =
{
	"inexact operation",
	"underflow",
	"overflow",
	"division by zero",
	"invalid operation",
};
char	*fpexcname(Ureg*, ulong, char*, uint);
#define FPEXPMASK	(0x3f<<12)	/* Floating exception bits in fcr31 */

struct {
	char	*name;
	uint	off;
} regname[] = {
	"STATUS", offsetof(Ureg, status),
	"PC",	offsetof(Ureg, pc),
	"SP",	offsetof(Ureg, sp),
	"CAUSE",offsetof(Ureg, cause),
	"BADADDR", offsetof(Ureg, badvaddr),
	"TLBVIRT", offsetof(Ureg, tlbvirt),
	"HI",	offsetof(Ureg, hi),
	"LO",	offsetof(Ureg, lo),
	"R31",	offsetof(Ureg, r31),
	"R30",	offsetof(Ureg, r30),
	"R28",	offsetof(Ureg, r28),
	"R27",	offsetof(Ureg, r27),
	"R26",	offsetof(Ureg, r26),
	"R25",	offsetof(Ureg, r25),
	"R24",	offsetof(Ureg, r24),
	"R23",	offsetof(Ureg, r23),
	"R22",	offsetof(Ureg, r22),
	"R21",	offsetof(Ureg, r21),
	"R20",	offsetof(Ureg, r20),
	"R19",	offsetof(Ureg, r19),
	"R18",	offsetof(Ureg, r18),
	"R17",	offsetof(Ureg, r17),
	"R16",	offsetof(Ureg, r16),
	"R15",	offsetof(Ureg, r15),
	"R14",	offsetof(Ureg, r14),
	"R13",	offsetof(Ureg, r13),
	"R12",	offsetof(Ureg, r12),
	"R11",	offsetof(Ureg, r11),
	"R10",	offsetof(Ureg, r10),
	"R9",	offsetof(Ureg, r9),
	"R8",	offsetof(Ureg, r8),
	"R7",	offsetof(Ureg, r7),
	"R6",	offsetof(Ureg, r6),
	"R5",	offsetof(Ureg, r5),
	"R4",	offsetof(Ureg, r4),
	"R3",	offsetof(Ureg, r3),
	"R2",	offsetof(Ureg, r2),
	"R1",	offsetof(Ureg, r1),
};

static Handler handlers[8];

void
kvce(Ureg *ur, int ecode)
{
	char c;
	Pte **p;
	Page **pg;
	Segment *s;
	ulong addr, soff;

	c = 'D';
	if(ecode == CVCEI)
		c = 'I';
	print("Trap: VCE%c: addr=%#lux\n", c, ur->badvaddr);
	if(up && !(ur->badvaddr & KSEGM)) {
		addr = ur->badvaddr;
		s = seg(up, addr, 0);
		if(s == nil){
			print("kvce: no seg for %#lux\n", addr);
			for(;;);
		}
		addr &= ~(BY2PG-1);
		soff = addr - s->base;
		p = &s->map[soff/PTEMAPMEM];
		if(*p){
			pg = &(*p)->pages[(soff&(PTEMAPMEM-1))/BY2PG];
			if(*pg)
				print("kvce: pa=%#lux, va=%#lux\n",
					(*pg)->pa, (*pg)->va);
			else
				print("kvce: no *pg\n");
		}else
			print("kvce: no *p\n");
	}
}

void
trap(Ureg *ur)
{
	int ecode, clockintr, user, cop, x, fpchk;
	ulong fpfcr31;
	char buf[2*ERRMAX], buf1[ERRMAX], *fpexcep;
	static int dumps;

	if (up && (char *)(ur) - up->kstack < 1024 && dumps++ == 0) {
		iprint("trap: proc %ld kernel stack getting full\n", up->pid);
		dumpregs(ur);
		dumpstack();
		for(;;);
	}
	if (up == nil &&
	    (char *)(ur) - (char *)m->stack < 1024 && dumps++ == 0) {
		iprint("trap: cpu%d kernel stack getting full\n", m->machno);
		dumpregs(ur);
		dumpstack();
		for(;;);
	}
	user = kenter(ur);
	if (ur->cause & TS)
		panic("trap: tlb shutdown");
	ecode = (ur->cause>>2)&EXCMASK;
	fpchk = 0;
	clockintr = 0;
	switch(ecode){
	case CINT:
		clockintr = intr(ur);
		break;

	case CFPE:
		if(!user)
			goto Default;
		if(up->fpstate == FPactive){
			savefpregs(up->fpsave);
			up->fpstate = FPinactive;
		}
		clrfpintr();
		fptrap(ur);
		fpchk = 1;
		break;

	case CTLBM:
	case CTLBL:
	case CTLBS:
		if(up == nil || !user && (ur->badvaddr & KSEGM) == KSEG3) {
			kfault(ur);
			break;
		}
		x = up->insyscall;
		up->insyscall = 1;
		spllo();
		faultmips(ur, user, ecode);
		up->insyscall = x;
		break;

	case CVCEI:
	case CVCED:
		kvce(ur, ecode);
		goto Default;

	case CWATCH:
		if(!user)
			panic("watchpoint trap from kernel mode pc=%#p",
				ur->pc);
		// fpwatch(ur);
		break;

	case CCPU:
		cop = (ur->cause>>28)&3;
		if(user && up && cop == 1) {
			if(up->fpstate & FPillegal) {
				/* someone used floating point in a note handler */
				postnote(up, 1,
					"sys: floating point in note handler",
					NDebug);
				break;
			}
			if(up->fpstate == FPinit || up->fpstate == FPinactive){
				restfpregs(up->fpsave, up->fpsave->fpstatus&~FPEXPMASK);
				up->fpstate = FPactive;
				ur->status |= CU1;
				break;
			}
			fpchk = 1;
			break;
		}
		/* Fallthrough */

	Default:
	default:
		if(user) {
			spllo();
			snprint(buf, sizeof buf, "sys: %s", excname[ecode]);
			postnote(up, 1, buf, NDebug);
			break;
		}
		if (ecode == CADREL || ecode == CADRES)
			iprint("kernel addr exception for va %#p pid %#ld %s\n",
				ur->badvaddr, (up? up->pid: 0),
				(up? up->text: ""));
		print("cpu%d: kernel %s pc=%#lux\n",
			m->machno, excname[ecode], ur->pc);
		dumpregs(ur);
		dumpstack();
		if(m->machno == 0)
			spllo();
		exit(1);
	}

	if(fpchk) {
		fpfcr31 = up->fpsave->fpstatus;
		if((fpfcr31>>12) & ((fpfcr31>>7)|0x20) & 0x3f) {
			spllo();
			fpexcep	= fpexcname(ur, fpfcr31, buf1, sizeof buf1);
			snprint(buf, sizeof buf, "sys: fp: %s", fpexcep);
			postnote(up, 1, buf, NDebug);
		}
	}

	splhi();

	/* delaysched set because we held a lock or because our quantum ended */
	if(up && up->delaysched && clockintr){
		sched();
		splhi();
	}

	if(user){
		notify(ur);
		/* replicate fpstate to ureg status */
		if(up->fpstate != FPactive)
			ur->status &= ~CU1;
		kexit(ur);
	}
}

/* map HPC3 irq to INTR2 */
int
hpc3irqlevel(int irq)
{
	*IO(uchar, LIO_0_MASK) |= 1 << (irq & 7);
	return 2 + irq/8;
}

/*
 *  set handlers
 */
void
intrenable(int level, void (*h)(Ureg*, void *), void *arg)
{
	Handler *hp;

	hp = &handlers[level];
	if (hp->handler != nil) {		/* occupied? */
		/* add a new one at the end of the chain */
		for (; hp->next != nil; hp = hp->next)
			;
		if((hp->next = xalloc(sizeof *hp)) == nil)
			panic("intrenable: out of memory");
		hp = hp->next;
	}
	hp->arg = arg;
	hp->handler = h;

	intron(INTR0 << level);
}

int
intr(Ureg *ur)
{
	ulong cause, mask;
	int clockintr;
	Handler *hh, *hp;

	m->intr++;
	clockintr = 0;
	/*
	 * ignore interrupts that we have disabled, even if their cause bits
	 * are set.
	 */
	cause = ur->cause & ur->status & INTMASK;
	cause &= ~(INTR1|INTR0);		/* ignore sw interrupts */
	if(cause & INTR7){
		clock(ur);
		cause &= ~INTR7;
		clockintr = 1;
	}
	hh = &handlers[2];
	for(mask = INTR2; cause != 0 && mask < INTR7; mask <<= 1){
		if(cause & mask){
			for(hp = hh; hp != nil; hp = hp->next){
				if(hp->handler != nil){
					(*hp->handler)(ur, hp->arg);
					cause &= ~mask;
				}
			}
		}
		hh++;
	}
	if(cause != 0)
		iprint("unhandled interrupts %lux\n", cause);

	/* preemptive scheduling */
	if(up != nil && !clockintr)
		preempted();
	/* if it was a clockintr, sched will be called at end of trap() */
	return clockintr;
}

char*
fpexcname(Ureg *ur, ulong fcr31, char *buf, uint size)
{
	int i;
	char *s;
	ulong fppc;

	fppc = ur->pc;
	if(ur->cause & BD)	/* branch delay */
		fppc += 4;
	s = 0;
	if(fcr31 & (1<<17))
		s = "unimplemented operation";
	else{
		fcr31 >>= 7;		/* trap enable bits */
		fcr31 &= (fcr31>>5);	/* anded with exceptions */
		for(i=0; i<5; i++)
			if(fcr31 & (1<<i))
				s = fpcause[i];
	}

	if(s == 0)
		return "no floating point exception";

	snprint(buf, size, "%s fppc=%#lux", s, fppc);
	return buf;
}

static void
getpcsp(ulong *pc, ulong *sp)
{
	*pc = getcallerpc(&pc);
	*sp = (ulong)&pc-4;
}

void
callwithureg(void (*fn)(Ureg*))
{
	Ureg ureg;

	memset(&ureg, 0, sizeof ureg);
	getpcsp((ulong*)&ureg.pc, (ulong*)&ureg.sp);
	ureg.r31 = getcallerpc(&fn);
	fn(&ureg);
}

static void
_dumpstack(Ureg *ureg)
{
	ulong l, v, top, i;
	extern ulong etext;

	print("ktrace /kernel/path %.8lux %.8lux %.8lux\n",
		ureg->pc, ureg->sp, ureg->r31);
	if(up == nil)
		top = (ulong)MACHADDR + MACHSIZE;
	else
		top = (ulong)up->kstack + KSTACK;
	i = 0;
	for(l=ureg->sp; l < top; l += BY2WD) {
		v = *(ulong*)l;
		if(KTZERO < v && v < (ulong)&etext) {
			print("%.8lux=%.8lux ", l, v);
			if((++i%4) == 0){
				print("\n");
				delay(200);
			}
		}
	}
	print("\n");
}

void
dumpstack(void)
{
	callwithureg(_dumpstack);
}

static ulong
R(Ureg *ur, int i)
{
	uchar *s;

	s = (uchar*)ur;
	return *(ulong*)(s + regname[i].off);
}

void
dumpregs(Ureg *ur)
{
	int i;

	if(up)
		print("registers for %s %lud\n", up->text, up->pid);
	else
		print("registers for kernel\n");

	for(i = 0; i < nelem(regname); i += 2)
		print("%s\t%#.8lux\t%s\t%#.8lux\n",
			regname[i].name,   R(ur, i),
			regname[i+1].name, R(ur, i+1));
}

int
notify(Ureg *ur)
{
	int l, s;
	ulong sp;
	Note *n;

	if(up->procctl)
		procctl();
	if(up->nnote == 0)
		return 0;

	if(up->fpstate == FPactive){
		savefpregs(up->fpsave);
		up->fpstate = FPinactive;
	}
	up->fpstate |= FPillegal;

	s = spllo();
	qlock(&up->debug);
	up->notepending = 0;
	n = &up->note[0];
	if(strncmp(n->msg, "sys:", 4) == 0) {
		l = strlen(n->msg);
		if(l > ERRMAX-15)	/* " pc=0x12345678\0" */
			l = ERRMAX-15;

		seprint(n->msg+l, &n->msg[sizeof n->msg], " pc=%#lux", ur->pc);
	}

	if(n->flag != NUser && (up->notified || up->notify==0)) {
		if(n->flag == NDebug)
			pprint("suicide: %s\n", n->msg);

		qunlock(&up->debug);
		pexit(n->msg, n->flag!=NDebug);
	}

	if(up->notified) {
		qunlock(&up->debug);
		splx(s);
		return 0;
	}

	if(!up->notify) {
		qunlock(&up->debug);
		pexit(n->msg, n->flag!=NDebug);
	}
	sp = ur->usp & ~(BY2V-1);
	sp -= sizeof(Ureg);

	if(!okaddr((ulong)up->notify, BY2WD, 0) ||
	   !okaddr(sp-ERRMAX-4*BY2WD, sizeof(Ureg)+ERRMAX+4*BY2WD, 1)) {
		pprint("suicide: bad address or sp in notify\n");
		qunlock(&up->debug);
		pexit("Suicide", 0);
	}

	memmove((Ureg*)sp, ur, sizeof(Ureg));	/* push user regs */
	*(Ureg**)(sp-BY2WD) = up->ureg;	/* word under Ureg is old up->ureg */
	up->ureg = (void*)sp;

	sp -= BY2WD+ERRMAX;
	memmove((char*)sp, up->note[0].msg, ERRMAX);	/* push err string */

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

	up->notified = 1;
	up->nnote--;
	memmove(&up->lastnote, &up->note[0], sizeof(Note));
	memmove(&up->note[0], &up->note[1], up->nnote*sizeof(Note));

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
		up->lastnote.flag = NDebug;
		/* fall through */

	case NDFLT:
		if(up->lastnote.flag == NDebug)
			pprint("suicide: %s\n", up->lastnote.msg);
		qunlock(&up->debug);
		pexit(up->lastnote.msg, up->lastnote.flag!=NDebug);
	}
}

#include "../port/systab.h"

static void
sctracesetup(ulong scallnr, ulong sp, uintptr pc, vlong *startnsp)
{
	if(up->procctl == Proc_tracesyscall){
		/*
		 * Redundant validaddr.  Do we care?
		 * Tracing syscalls is not exactly a fast path...
		 * Beware, validaddr currently does a pexit rather
		 * than an error if there's a problem; that might
		 * change in the future.
		 */
		if(sp < (USTKTOP-BY2PG) || sp > (USTKTOP-sizeof(Sargs)-BY2WD))
			validaddr(sp, sizeof(Sargs)+BY2WD, 0);

		syscallfmt(scallnr, pc, (va_list)(sp+BY2WD));
		up->procctl = Proc_stopme;
		procctl();
		if(up->syscalltrace)
			free(up->syscalltrace);
		up->syscalltrace = nil;
		*startnsp = todget(nil);
	}
}

static void
sctracefinish(ulong scallnr, ulong sp, int ret, vlong startns)
{
	int s;

	if(up->procctl == Proc_tracesyscall){
		up->procctl = Proc_stopme;
		sysretfmt(scallnr, (va_list)(sp+BY2WD), ret,
			startns, todget(nil));
		s = splhi();
		procctl();
		splx(s);
		if(up->syscalltrace)
			free(up->syscalltrace);
		up->syscalltrace = nil;
	}
}

/*
 * called directly from assembler, not via trap()
 */
void
syscall(Ureg *ur)
{
	int i;
	volatile long ret;
	ulong sp, scallnr;
	vlong startns;
	char *e;

	if(!kenter(ur))
		panic("syscall from kernel");

	m->syscall++;
	up->insyscall = 1;
	up->pc = ur->pc;
	ur->cause = 16<<2;	/* for debugging: system call is undef 16 */

	scallnr = ur->r1;
	up->scallnr = ur->r1;
	sp = ur->sp;
	sctracesetup(scallnr, sp, ur->pc, &startns);

	/* no fpu, so no fp state to save */
	spllo();

	up->nerrlab = 0;
	ret = -1;
	if(!waserror()) {
		if(scallnr >= nsyscall || systab[scallnr] == 0){
			pprint("bad sys call number %ld pc %#lux\n",
				scallnr, ur->pc);
			postnote(up, 1, "sys: bad sys call", NDebug);
			error(Ebadarg);
		}

		if(sp & (BY2WD-1)){
			pprint("odd sp in sys call pc %#lux sp %#lux\n",
				ur->pc, ur->sp);
			postnote(up, 1, "sys: odd stack", NDebug);
			error(Ebadarg);
		}

		if(sp<(USTKTOP-BY2PG) || sp>(USTKTOP-sizeof(Sargs)-BY2WD))
			validaddr(sp, sizeof(Sargs)+BY2WD, 0);

		up->s = *((Sargs*)(sp+BY2WD));
		up->psstate = sysctab[scallnr];

		ret = systab[scallnr]((va_list)up->s.args);
		poperror();
	}else{
		/* failure: save the error buffer for errstr */
		e = up->syserrstr;
		up->syserrstr = up->errstr;
		up->errstr = e;
		if(0 && up->pid == 1)
			print("[%lud %s] syscall %lud: %s\n",
				up->pid, up->text, scallnr, up->errstr);
	}
	if(up->nerrlab){
		print("bad errstack [%lud]: %d extra\n", scallnr, up->nerrlab);
		for(i = 0; i < NERR; i++)
			print("sp=%#lux pc=%#lux\n",
				up->errlab[i].sp, up->errlab[i].pc);
		panic("error stack");
	}
	sctracefinish(scallnr, sp, ret, startns);

	ur->pc += 4;
	ur->r1 = ret;

	up->psstate = 0;
	up->insyscall = 0;

	if(scallnr == NOTED)				/* ugly hack */
		noted(ur, *(ulong*)(sp+BY2WD));	/* may return */
	splhi();
	if(scallnr!=RFORK && (up->procctl || up->nnote))
		notify(ur);
	/* if we delayed sched because we held a lock, sched now */
	if(up->delaysched)
		sched();
	/* replicate fpstate to ureg status */
	if(up->fpstate != FPactive)
		ur->status &= ~CU1;
	kexit(ur);
}

void
forkchild(Proc *p, Ureg *ur)
{
	Ureg *cur;

	p->sched.sp = (ulong)p->kstack+KSTACK-UREGSIZE;
	p->sched.pc = (ulong)forkret;

	cur = (Ureg*)(p->sched.sp+2*BY2WD);
	memmove(cur, ur, sizeof(Ureg));

	cur->status &= ~CU1;	/* FPU off when returning */

	cur->r1 = 0;
	cur->pc += 4;
}

void
kprocchild(Proc *p, void (*entry)(void))
{
	p->sched.pc = (ulong)entry;
	p->sched.sp = (ulong)p->kstack+KSTACK;
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
	return USTKTOP-sizeof(Tos);	/* address of kernel/user shared data */
}

ulong
userpc(void)
{
	Ureg *ur;

	ur = (Ureg*)up->dbgreg;
	return ur->pc;
}

/*
 * This routine must save the values of registers the user is not
 * permitted to write from devproc and then restore the saved values
 * before returning
 */
void
setregisters(Ureg *xp, char *pureg, char *uva, int n)
{
	ulong status, r27;

	r27 = xp->r27;			/* return PC for GEVector() */
	status = xp->status;
	memmove(pureg, uva, n);
	xp->r27 = r27;
	xp->status = status;
}

/*
 * Give enough context in the ureg to produce a kernel stack for
 * a sleeping process
 */
void
setkernur(Ureg *xp, Proc *p)
{
	xp->pc = p->sched.pc;
	xp->sp = p->sched.sp;
	xp->r24 = (ulong)p;		/* up */
	xp->r31 = (ulong)sched;
}

ulong
dbgpc(Proc *p)
{
	Ureg *ur;

	ur = p->dbgreg;
	if(ur == 0)
		return 0;

	return ur->pc;
}
