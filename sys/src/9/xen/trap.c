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

#define INTRLOG(a)  
#define SETUPLOG(a)
#define SYSCALLLOG(a)
#define FAULTLOG(a) 
#define FAULTLOGFAST(a)
#define POSTNOTELOG(a)
#define TRAPLOG(a)

int faultpanic = 0;

enum {
	/* trap_info_t flags */
	SPL0 = 0,
	SPL3 = 3,
	EvDisable = 4,
};
  
void	noted(Ureg*, ulong);

static void debugbpt(Ureg*, void*);
static void fault386(Ureg*, void*);
static void safe_fault386(Ureg*, void*);
static void doublefault(Ureg*, void*);
static void unexpected(Ureg*, void*);
static void _dumpstack(Ureg*);

static Lock vctllock;
static Vctl *vctl[256];

enum
{
	Ntimevec = 20		/* number of time buckets for each intr */
};
ulong intrtimes[256][Ntimevec];

void
intrenable(int irq, void (*f)(Ureg*, void*), void* a, int tbdf, char *name)
{
	int vno;
	Vctl *v;

/**/
	SETUPLOG(dprint("intrenable: irq %d, f %p, a %p, tbdf 0x%x, name %s\n", 
			irq, f, a, tbdf, name);)
/**/
	if(f == nil){
		print("intrenable: nil handler for %d, tbdf 0x%uX for %s\n",
			irq, tbdf, name);
		return;
	}

	v = xalloc(sizeof(Vctl));
	v->isintr = 1;
	v->irq = irq;
	v->tbdf = tbdf;
	v->f = f;
	v->a = a;
	strncpy(v->name, name, KNAMELEN-1);
	v->name[KNAMELEN-1] = 0;

	ilock(&vctllock);
	vno = arch->intrenable(v);
	if(vno == -1){
		iunlock(&vctllock);
		print("intrenable: couldn't enable irq %d, tbdf 0x%uX for %s\n",
			irq, tbdf, v->name);
		xfree(v);
		return;
	}
	if(vctl[vno]){
		if(vctl[vno]->isr != v->isr || vctl[vno]->eoi != v->eoi)
			panic("intrenable: handler: %s %s %p %p %p %p\n",
				vctl[vno]->name, v->name,
				vctl[vno]->isr, v->isr, vctl[vno]->eoi, v->eoi);
		v->next = vctl[vno];
	}
	vctl[vno] = v;
	SETUPLOG(dprint("INTRENABLE: vctl[%d] is %p\n", vno, vctl[vno]);)
	iunlock(&vctllock);
}

void
intrdisable(int irq, void (*f)(Ureg *, void *), void *a, int tbdf, char *name)
{
	Vctl **pv, *v;
	int vno;

	vno = arch->intrvecno(irq);
	ilock(&vctllock);
	for(pv = &vctl[vno]; (v = *pv) != nil; pv = &v->next){
		if(v->isintr && v->irq == irq
		&& v->tbdf == tbdf && v->f == f && v->a == a
		&& strcmp(v->name, name) == 0){
			*pv = v->next;
			xfree(v);

			if(vctl[vno] == nil && arch->intrdisable != nil)
				arch->intrdisable(irq);
			break;
		}
	}
	iunlock(&vctllock);
}

static long
irqallocread(Chan*, void *a, long n, vlong offset)
{
	char buf[2*(11+1)+KNAMELEN+1+1];
	int vno, m;
	Vctl *v;

	if(n < 0 || offset < 0)
		error(Ebadarg);

	for(vno=0; vno<nelem(vctl); vno++){
		for(v=vctl[vno]; v; v=v->next){
			m = snprint(buf, sizeof(buf), "%11d %11d %.*s\n", vno, v->irq, KNAMELEN, v->name);
			offset -= m;
			if(offset >= 0)
				continue;
			if(n > -offset)
				n = -offset;
			offset += m;
			memmove(a, buf+offset, n);
			return n;
		}
	}
	return 0;
}

void
trapenable(int vno, void (*f)(Ureg*, void*), void* a, char *name)
{
	Vctl *v;

	if(vno < 0 || vno >= VectorPIC)
		panic("trapenable: vno %d\n", vno);
	v = xalloc(sizeof(Vctl));
	v->tbdf = BUSUNKNOWN;
	v->f = f;
	v->a = a;
	strncpy(v->name, name, KNAMELEN);
	v->name[KNAMELEN-1] = 0;

	lock(&vctllock);
	if(vctl[vno])
		v->next = vctl[vno]->next;
	vctl[vno] = v;
	unlock(&vctllock);
}

static void
nmienable(void)
{
	/* leave this here in case plan 9 ever makes it to dom0 */
#ifdef NOWAY
	/*
	 * Hack: should be locked with NVRAM access.
	 */
	outb(0x70, 0x80);		/* NMI latch clear */
	outb(0x70, 0);

	x = inb(0x61) & 0x07;		/* Enable NMI */
	outb(0x61, 0x08|x);
	outb(0x61, x);
#endif
}

/* we started out doing the 'giant bulk init' for all traps. 
  * we're going to do them one-by-one since error analysis is 
  * so much easier that way.
  */
void
trapinit(void)
{
	trap_info_t t[2];
	ulong vaddr;
	int v, flag;

	HYPERVISOR_set_callbacks(
		KESEL, (ulong)hypervisor_callback,
		KESEL, (ulong)failsafe_callback);

	/* XXX rework as single hypercall once debugged */
	t[1].address = 0;
	vaddr = (ulong)vectortable;
	for(v = 0; v < 256; v++){
		switch(v){
		case VectorBPT:
		case VectorSYSCALL:
			flag = SPL3 | EvDisable;
			break;
		default:
			flag = SPL0 | EvDisable;
			break;
		}
		t[0] = (trap_info_t){ v, flag, KESEL, vaddr };
		if(HYPERVISOR_set_trap_table(t) < 0)
			panic("trapinit: FAIL: try to set: 0x%x, 0x%x, 0x%x, 0x%ulx\n", 
				t[0].vector, t[0].flags, t[0].cs, t[0].address);
		vaddr += 6;
	}

	/*
	 * Special traps.
	 * Syscall() is called directly without going through trap().
	 */
	trapenable(VectorBPT, debugbpt, 0, "debugpt");
	trapenable(VectorPF, fault386, 0, "fault386");
	trapenable(Vector2F, doublefault, 0, "doublefault");
	trapenable(Vector15, unexpected, 0, "unexpected");

	nmienable();
	addarchfile("irqalloc", 0444, irqallocread, nil);
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
	"19 (reserved)",
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

/*
 *  keep histogram of interrupt service times
 */
void
intrtime(Mach*, int vno)
{
	ulong diff;
	ulong x;

	x = perfticks();
	diff = x - m->perf.intrts;
	m->perf.intrts = x;

	m->perf.inintr += diff;
	if(up == nil && m->perf.inidle > diff)
		m->perf.inidle -= diff;

	diff /= m->cpumhz*100;	// quantum = 100µsec
	if(diff >= Ntimevec)
		diff = Ntimevec-1;
	intrtimes[vno][diff]++;
}

/* go to user space */
void
kexit(Ureg*)
{
	uvlong t;
	Tos *tos;

	/* precise time accounting, kernel exit */
	tos = (Tos*)(USTKTOP-sizeof(Tos));
	cycles(&t);
	tos->kcycles += t - up->kentry;
	tos->pcycles = up->pcycles;
	tos->pid = up->pid;
	INTRLOG(dprint("leave kexit, TOS %p\n", tos);)
}

/*
 *  All traps come here.  It is slower to have all traps call trap()
 *  rather than directly vectoring the handler.  However, this avoids a
 *  lot of code duplication and possible bugs.  The only exception is
 *  VectorSYSCALL.
 *  Trap is called with interrupts (and events) disabled via interrupt-gates.
 */
void
trap(Ureg* ureg)
{
	int clockintr, i, vno, user;
	char buf[ERRMAX];
	Vctl *ctl, *v;
	Mach *mach;

	TRAPLOG(dprint("trap ureg %lux %lux\n", (ulong*)ureg, ureg->trap);)
	m->perf.intrts = perfticks();
	user = (ureg->cs & 0xFFFF) == UESEL;
	if(user){
		up->dbgreg = ureg;
		cycles(&up->kentry);
	}

	clockintr = 0;

	vno = ureg->trap;
	if(vno < 0 || vno >= 256)
		panic("bad interrupt number %d\n", vno);
	TRAPLOG(dprint("trap: vno is 0x%x, vctl[%d] is %p\n", vno, vno, vctl[vno]);)
	if(ctl = vctl[vno]){
		INTRLOG(dprint("ctl is %p, isintr is %d\n", ctl, ctl->isintr);)
		if(ctl->isintr){
			m->intr++;
			if(vno >= VectorPIC && vno != VectorSYSCALL)
				m->lastintr = ctl->irq;
		}

		INTRLOG(dprint("ctl %p, isr %p\n", ctl, ctl->isr);)
		if(ctl->isr)
			ctl->isr(vno);
		for(v = ctl; v != nil; v = v->next){
			INTRLOG(dprint("ctl %p, f is %p\n", v, v->f);)
			if(v->f)
				v->f(ureg, v->a);
		}
		INTRLOG(dprint("ctl %p, eoi %p\n", ctl, ctl->eoi);)
		if(ctl->eoi)
			ctl->eoi(vno);

		if(ctl->isintr){
			intrtime(m, vno);

			//if(ctl->irq == IrqCLOCK || ctl->irq == IrqTIMER)
			if (ctl->tbdf != BUSUNKNOWN && ctl->irq == VIRQ_TIMER)
				clockintr = 1;

			if(up && !clockintr)
				preempted();
		}
	}
	else if(vno <= nelem(excname) && user){
		spllo();
		sprint(buf, "sys: trap: %s", excname[vno]);
		postnote(up, 1, buf, NDebug);
	}
	else if(vno >= VectorPIC && vno != VectorSYSCALL){
		/*
		 * An unknown interrupt.
		 * Check for a default IRQ7. This can happen when
		 * the IRQ input goes away before the acknowledge.
		 * In this case, a 'default IRQ7' is generated, but
		 * the corresponding bit in the ISR isn't set.
		 * In fact, just ignore all such interrupts.
		 */

		/* call all interrupt routines, just in case */
		for(i = VectorPIC; i <= MaxIrqLAPIC; i++){
			ctl = vctl[i];
			if(ctl == nil)
				continue;
			if(!ctl->isintr)
				continue;
			for(v = ctl; v != nil; v = v->next){
				if(v->f)
					v->f(ureg, v->a);
			}
			/* should we do this? */
			if(ctl->eoi)
				ctl->eoi(i);
		}

		iprint("cpu%d: spurious interrupt %d, last %d\n",
			m->machno, vno, m->lastintr);
		if(0)if(conf.nmach > 1){
			for(i = 0; i < MAXMACH; i++){
				if(active.machs[i] == 0)
					continue;
				mach = MACHP(i);
				if(m->machno == mach->machno)
					continue;
				print(" cpu%d: last %d",
					mach->machno, mach->lastintr);
			}
			print("\n");
		}
		m->spuriousintr++;
		if(user)
			kexit(ureg);
		return;
	}
	else{
		if(vno == VectorNMI){
			nmienable();
			if(m->machno != 0){
				print("cpu%d: PC %8.8luX\n",
					m->machno, ureg->pc);
				for(;;);
			}
		}
		dumpregs(ureg);
		if(!user){
			ureg->sp = (ulong)&ureg->sp;
			_dumpstack(ureg);
		}
		if(vno < nelem(excname))
			panic("%s", excname[vno]);
		panic("unknown trap/intr: %d\n", vno);
	}
	splhi();

	/* delaysched set because we held a lock or because our quantum ended */
	if(up && up->delaysched && clockintr){
		INTRLOG(dprint("calling sched in trap? \n");)
		sched();
		INTRLOG(dprint("Back from calling sched in trap?\n");)
		splhi();
	}

	if(user){
		if(up->procctl || up->nnote)
			notify(ureg);
		kexit(ureg);
	}

	if (ureg->trap == 0xe) {
		/*
		  * on page fault, we need to restore the old spl
		  * Xen won't do it for us.
		  * XXX verify this.
		  */
		if (ureg->flags & 0x200)
			spllo();
	}
}

void
dumpregs2(Ureg* ureg)
{
	if(up)
		print("cpu%d: registers for %s %lud\n",
			m->machno, up->text, up->pid);
	else
		print("cpu%d: registers for kernel\n", m->machno);
	print("FLAGS=%luX TRAP=%luX ECODE=%luX PC=%luX",
		ureg->flags, ureg->trap, ureg->ecode, ureg->pc);
	print(" SS=%4.4luX USP=%luX\n", ureg->ss & 0xFFFF, ureg->usp);
	print("  AX %8.8luX  BX %8.8luX  CX %8.8luX  DX %8.8luX\n",
		ureg->ax, ureg->bx, ureg->cx, ureg->dx);
	print("  SI %8.8luX  DI %8.8luX  BP %8.8luX\n",
		ureg->si, ureg->di, ureg->bp);
	print("  CS %4.4luX  DS %4.4luX  ES %4.4luX  FS %4.4luX  GS %4.4luX\n",
		ureg->cs & 0xFFFF, ureg->ds & 0xFFFF, ureg->es & 0xFFFF,
		ureg->fs & 0xFFFF, ureg->gs & 0xFFFF);
}

void
dumpregs(Ureg* ureg)
{
	extern ulong etext;

	dumpregs2(ureg);

	/*
	 * Processor control registers.
	 * If machine check exception, time stamp counter, page size extensions
	 * or enhanced virtual 8086 mode extensions are supported, there is a
	 * CR4. If there is a CR4 and machine check extensions, read the machine
	 * check address and machine check type registers if RDMSR supported.
	 */
	print("SKIPPING get of crx and other such stuff.\n");/* */
#ifdef NOT
	print("  CR0 %8.8lux CR2 %8.8lux CR3 %8.8lux",
		getcr0(), getcr2(), getcr3());
	if(m->cpuiddx & 0x9A){
		print(" CR4 %8.8lux", getcr4());
		if((m->cpuiddx & 0xA0) == 0xA0){
			rdmsr(0x00, &mca);
			rdmsr(0x01, &mct);
			print("\n  MCA %8.8llux MCT %8.8llux", mca, mct);
		}
	}
#endif
	print("\n  ur %lux up %lux\n", (ulong)ureg, (ulong)up);
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
	ulong l, v, i, estack;
	extern ulong etext;
	int x;

	if(getconf("*nodumpstack")){
		iprint("dumpstack disabled\n");
		return;
	}
	iprint("dumpstack\n");
	x = 0;
	x += print("ktrace /kernel/path %.8lux %.8lux <<EOF\n", ureg->pc, ureg->sp);
	i = 0;
	if(up
	&& (ulong)&l >= (ulong)up->kstack
	&& (ulong)&l <= (ulong)up->kstack+KSTACK)
		estack = (ulong)up->kstack+KSTACK;
	else if((ulong)&l >= (ulong)m->stack
	&& (ulong)&l <= (ulong)m+BY2PG)
		estack = (ulong)m+MACHSIZE;
	else
		return;
	x += print("estackx %.8lux\n", estack);

	for(l=(ulong)&l; l<estack; l+=4){
		v = *(ulong*)l;
		if((KTZERO < v && v < (ulong)&etext) || estack-l<32){
			/*
			 * we could Pick off general CALL (((uchar*)v)[-5] == 0xE8)
			 * and CALL indirect through AX (((uchar*)v)[-2] == 0xFF && ((uchar*)v)[-2] == 0xD0),
			 * but this is too clever and misses faulting address.
			 */
			x += print("%.8lux=%.8lux ", l, v);
			i++;
		}
		if(i == 4){
			i = 0;
			x += print("\n");
		}
	}
	if(i)
		print("\n");
	print("EOF\n");
}

void
dumpstack(void)
{
	callwithureg(_dumpstack);
}

static void
debugbpt(Ureg* ureg, void*)
{
	char buf[ERRMAX];
	print("debugbpt\n");
	if(up == 0)
		panic("kernel bpt");
	/* restore pc to instruction that caused the trap */
	ureg->pc--;
	sprint(buf, "sys: breakpoint");
	postnote(up, 1, buf, NDebug);
	print("debugbpt for proc %lud\n", up->pid);
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
	int read, user, n, insyscall;
	char buf[ERRMAX];

	addr = HYPERVISOR_shared_info->vcpu_info[m->machno].arch.cr2;
	if (faultpanic) {
		dprint("cr2 is 0x%lx\n", addr);
		//dumpregs(ureg);
		dumpstack();
		panic("fault386");
		exit(1);
	}
	
	user = (ureg->cs & 0xFFFF) == UESEL;
	if(!user && mmukmapsync(addr))
		return;
	read = !(ureg->ecode & 2);
	if(up == nil)
		panic("fault but up is zero; pc 0x%8.8lux addr 0x%8.8lux\n", ureg->pc, addr);
	insyscall = up->insyscall;
	up->insyscall = 1;
	n = fault(addr, ureg->pc, read);
	if(n < 0){
		if(!user){
			dumpregs(ureg);
			panic("fault: 0x%lux\n", addr);
		}
		sprint(buf, "sys: trap: fault %s addr=0x%lux",
			read? "read" : "write", addr);
		dprint("Posting %s to %lud\n", buf, up->pid);
		postnote(up, 1, buf, NDebug);
	}
	up->insyscall = insyscall;
	FAULTLOG(dprint("fault386: all done\n");)
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
	ulong	sp;
	long	ret;
	int	i, s;
	ulong scallnr;

	SYSCALLLOG(dprint("%d: syscall ...#%ld(%s)\n", 
			up->pid, ureg->ax, sysctab[ureg->ax]);)
	
	if((ureg->cs & 0xFFFF) != UESEL)
		panic("syscall: cs 0x%4.4luX\n", ureg->cs);

	cycles(&up->kentry);

	m->syscall++;
	up->insyscall = 1;
	up->pc = ureg->pc;
	up->dbgreg = ureg;

	if(up->procctl == Proc_tracesyscall){
		up->procctl = Proc_stopme;
		procctl();
	}

	scallnr = ureg->ax;
	up->scallnr = scallnr;
	if(scallnr == RFORK && up->fpstate == FPactive){
		fpsave(up->fpsave);
		up->fpstate = FPinactive;
	}
	spllo();

	sp = ureg->usp;
	up->nerrlab = 0;
	ret = -1;
	if(!waserror()){
		if(scallnr >= nsyscall || systab[scallnr] == 0){
			pprint("bad sys call number %lud pc %lux\n",
				scallnr, ureg->pc);
			postnote(up, 1, "sys: bad sys call", NDebug);
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
			print("syscall %lud error %s\n", scallnr, up->syserrstr);
	}
	if(up->nerrlab){
		print("bad errstack [%lud]: %d extra\n", scallnr, up->nerrlab);
		for(i = 0; i < NERR; i++)
			print("sp=%lux pc=%lux\n",
				up->errlab[i].sp, up->errlab[i].pc);
		panic("error stack");
	}

	SYSCALLLOG(dprint("%d: Syscall %d returns %d, ureg %p\n", up->pid, scallnr, ret, ureg);)
	/*
	 *  Put return value in frame.  On the x86 the syscall is
	 *  just another trap and the return value from syscall is
	 *  ignored.  On other machines the return value is put into
	 *  the results register by caller of syscall.
	 */
	ureg->ax = ret;

	if(up->procctl == Proc_tracesyscall){
		s = splhi();
		up->procctl = Proc_stopme;
		procctl();
		splx(s);
	}

	up->insyscall = 0;
	up->psstate = 0;
	INTRLOG(dprint("cleared insyscall\n");)
	if(scallnr == NOTED)
		noted(ureg, *(ulong*)(sp+BY2WD));

	if(scallnr!=RFORK && (up->procctl || up->nnote)){
		splhi();
		notify(ureg);
	}
	/* if we delayed sched because we held a lock, sched now */
	if(up->delaysched)
		sched();
	INTRLOG(dprint("before kexit\n");)
	kexit(ureg);
}

/*
 *  Call user, if necessary, with note.
 *  Pass user the Ureg struct and the note on his stack.
 */
int
notify(Ureg* ureg)
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
		if(n->flag == NDebug)
			pprint("suicide: %s\n", n->msg);
		qunlock(&up->debug);
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
	sp = ureg->usp;
	sp -= sizeof(Ureg);

	if(!okaddr((ulong)up->notify, 1, 0)
	|| !okaddr(sp-ERRMAX-4*BY2WD, sizeof(Ureg)+ERRMAX+4*BY2WD, 1)){
		pprint("suicide: bad address in notify\n");
		qunlock(&up->debug);
		pexit("Suicide", 0);
	}

	up->ureg = (void*)sp;
	memmove((Ureg*)sp, ureg, sizeof(Ureg));
	*(Ureg**)(sp-BY2WD) = up->ureg;	/* word under Ureg is old up->ureg */
	up->ureg = (void*)sp;
	sp -= BY2WD+ERRMAX;
	memmove((char*)sp, up->note[0].msg, ERRMAX);
	sp -= 3*BY2WD;
	*(ulong*)(sp+2*BY2WD) = sp+3*BY2WD;		/* arg 2 is string */
	*(ulong*)(sp+1*BY2WD) = (ulong)up->ureg;	/* arg 1 is ureg* */
	*(ulong*)(sp+0*BY2WD) = 0;			/* arg 0 is pc */
	ureg->usp = sp;
	ureg->pc = (ulong)up->notify;
	up->notified = 1;
	up->nnote--;
	memmove(&up->lastnote, &up->note[0], sizeof(Note));
	memmove(&up->note[0], &up->note[1], up->nnote*sizeof(Note));

	qunlock(&up->debug);
	splx(s);
	return 1;
}

/*
 *   Return user to state before notify()
 */
void
noted(Ureg* ureg, ulong arg0)
{
	Ureg *nureg;
	ulong oureg, sp;

	qlock(&up->debug);
	if(arg0!=NRSTR && !up->notified) {
		qunlock(&up->debug);
		pprint("call to noted() when not notified\n");
		pexit("Suicide", 0);
	}
	up->notified = 0;

	nureg = up->ureg;	/* pointer to user returned Ureg struct */

	up->fpstate &= ~FPillegal;

	/* sanity clause */
	oureg = (ulong)nureg;
	if(!okaddr((ulong)oureg-BY2WD, BY2WD+sizeof(Ureg), 0)){
		pprint("bad ureg in noted or call to noted when not notified\n");
		qunlock(&up->debug);
		pexit("Suicide", 0);
	}

	/*
	 * Check the segment selectors are all valid, otherwise
	 * a fault will be taken on attempting to return to the
	 * user process.
	 * Take care with the comparisons as different processor
	 * generations push segment descriptors in different ways.
	 */
	if((nureg->cs & 0xFFFF) != UESEL || (nureg->ss & 0xFFFF) != UDSEL
	  || (nureg->ds & 0xFFFF) != UDSEL || (nureg->es & 0xFFFF) != UDSEL
	  || (nureg->fs & 0xFFFF) != UDSEL || (nureg->gs & 0xFFFF) != UDSEL){
		pprint("bad segment selector in noted\n");
		pprint("cs is %#lux, wanted %#ux\n", nureg->cs, UESEL);
		pprint("ds is %#lux, wanted %#ux\n", nureg->ds, UDSEL);
		pprint("es is %#lux, fs is %#lux, gs %#lux, wanted %#ux\n", 
			ureg->es, ureg->fs, ureg->gs, UDSEL);
		pprint("ss is %#lux, wanted %#ux\n", nureg->ss, UDSEL);
		qunlock(&up->debug);
		pexit("Suicide", 0);
	}

	/* don't let user change system flags */
	nureg->flags = (ureg->flags & ~0xCD5) | (nureg->flags & 0xCD5);

	memmove(ureg, nureg, sizeof(Ureg));

	switch(arg0){
	case NCONT:
	case NRSTR:
		if(!okaddr(nureg->pc, 1, 0) || !okaddr(nureg->usp, BY2WD, 0)){
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		up->ureg = (Ureg*)(*(ulong*)(oureg-BY2WD));
		qunlock(&up->debug);
		break;

	case NSAVE:
		if(!okaddr(nureg->pc, BY2WD, 0)
		|| !okaddr(nureg->usp, BY2WD, 0)){
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		qunlock(&up->debug);
		sp = oureg-4*BY2WD-ERRMAX;
		splhi();
		ureg->sp = sp;
		((ulong*)sp)[1] = oureg;	/* arg 1 0(FP) is ureg* */
		((ulong*)sp)[0] = 0;		/* arg 0 is pc */
		break;

	default:
		pprint("unknown noted arg 0x%lux\n", arg0);
		up->lastnote.flag = NDebug;
		/* fall through */
		
	case NDFLT:
		if(up->lastnote.flag == NDebug){ 
			qunlock(&up->debug);
			pprint("suicide: %s\n", up->lastnote.msg);
		} else
			qunlock(&up->debug);
		pexit(up->lastnote.msg, up->lastnote.flag!=NDebug);
	}
}

uintptr
execregs(uintptr entry, ulong ssize, ulong nargs)
{
	ulong *sp;
	Ureg *ureg;

	up->fpstate = FPinit;
	fpoff();

	sp = (ulong*)(USTKTOP - ssize);
	*--sp = nargs;

	ureg = up->dbgreg;
	ureg->usp = (ulong)sp;
	ureg->pc = entry;
//	print("execregs returns 0x%x\n", USTKTOP-sizeof(Tos));
	return USTKTOP-sizeof(Tos);		/* address of kernel/user shared data */
}

/*
 *  return the userpc the last exception happened at
 */
ulong
userpc(void)
{
	Ureg *ureg;

	ureg = (Ureg*)up->dbgreg;
	return ureg->pc;
}

/* This routine must save the values of registers the user is not permitted
 * to write from devproc and then restore the saved values before returning.
 */
void
setregisters(Ureg* ureg, char* pureg, char* uva, int n)
{
	ulong flags;
	ulong cs;
	ulong ss;

	flags = ureg->flags;
	cs = ureg->cs;
	ss = ureg->ss;
	memmove(pureg, uva, n);
	ureg->flags = (ureg->flags & 0x00FF) | (flags & 0xFF00);
	ureg->cs = cs;
	ureg->ss = ss;
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
	/*
	 * gotolabel() needs a word on the stack in
	 * which to place the return PC used to jump
	 * to linkproc().
	 */
	p->sched.pc = (ulong)linkproc;
	p->sched.sp = (ulong)p->kstack+KSTACK-BY2WD;

	p->kpfun = func;
	p->kparg = arg;
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
	p->sched.sp = (ulong)p->kstack+KSTACK-(sizeof(Ureg)+2*BY2WD);
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

/*
 * install_safe_pf_handler / install_normal_pf_handler:
 * 
 * These are used within the failsafe_callback handler in entry.S to avoid
 * taking a full page fault when reloading FS and GS. This is because FS and 
 * GS could be invalid at pretty much any point while Xenolinux executes (we 
 * don't set them to safe values on entry to the kernel). At *any* point Xen 
 * may be entered due to a hardware interrupt --- on exit from Xen an invalid 
 * FS/GS will cause our failsafe_callback to be executed. This could occur, 
 * for example, while the mmu_update_queue is in an inconsistent state. This
 * is disastrous because the normal page-fault handler touches the update
 * queue!
 * 
 * Fortunately, within the failsafe handler it is safe to force DS/ES/FS/GS
 * to zero if they cannot be reloaded -- at this point executing a normal
 * page fault would not change this effect. The safe page-fault handler
 * ensures this end result (blow away the selector value) without the dangers
 * of the normal page-fault handler.
 * 
 * NB. Perhaps this can all go away after we have implemented writeable
 * page tables. :-)
 */
static void
safe_fault386(Ureg* , void* ) {
	panic("DO SAFE PAGE FAULT!\n");


   
}

unsigned long install_safe_pf_handler(void)
{
	dprint("called from failsafe callback\n");
	trapenable(VectorPF, safe_fault386, 0, "safe_fault386");
	return 0;
}

void install_normal_pf_handler(unsigned long)
{
	trapenable(VectorPF, fault386, 0, "fault386");
}
