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

static int trapinited;

void	noted(Ureg*, ulong);

static void debugexc(Ureg*, void*);
static void debugbpt(Ureg*, void*);
static void faultamd64(Ureg*, void*);
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

	if(f == nil){
		print("intrenable: nil handler for %d, tbdf 0x%uX for %s\n",
			irq, tbdf, name);
		return;
	}

	if(tbdf != BUSUNKNOWN && (irq == 0xff || irq == 0)){
		print("intrenable: got unassigned irq %d, tbdf 0x%uX for %s\n",
			irq, tbdf, name);
		irq = -1;
	}


	/*
	 * IRQ2 doesn't really exist, it's used to gang the interrupt
	 * controllers together. A device set to IRQ2 will appear on
	 * the second interrupt controller as IRQ9.
	 */
	if(irq == 2)
		irq = 9;

	if((v = xalloc(sizeof(Vctl))) == nil)
		panic("intrenable: out of memory");
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
			panic("intrenable: handler: %s %s %#p %#p %#p %#p",
				vctl[vno]->name, v->name,
				vctl[vno]->isr, v->isr, vctl[vno]->eoi, v->eoi);
		v->next = vctl[vno];
	}
	vctl[vno] = v;
	iunlock(&vctllock);
}

void
intrdisable(int irq, void (*f)(Ureg *, void *), void *a, int tbdf, char *name)
{
	Vctl **pv, *v;
	int vno;

	if(irq == 2)
		irq = 9;
	if(arch->intrvecno == nil || (tbdf != BUSUNKNOWN && (irq == 0xff || irq == 0))){
		/*
		 * on APIC machine, irq is pretty meaningless
		 * and disabling a the vector is not implemented.
		 * however, we still want to remove the matching
		 * Vctl entry to prevent calling Vctl.f() with a
		 * stale Vctl.a pointer.
		 */
		irq = -1;
		vno = VectorPIC;
	} else {
		vno = arch->intrvecno(irq);
	}
	ilock(&vctllock);
	do {
		for(pv = &vctl[vno]; (v = *pv) != nil; pv = &v->next){
			if(v->isintr && (v->irq == irq || irq == -1)
			&& v->tbdf == tbdf && v->f == f && v->a == a
			&& strcmp(v->name, name) == 0)
				break;
		}
		if(v != nil){
			*pv = v->next;
			xfree(v);

			if(irq != -1 && vctl[vno] == nil && arch->intrdisable != nil)
				arch->intrdisable(irq);
			break;
		}
	} while(irq == -1 && ++vno <= MaxVectorAPIC);
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
		panic("trapenable: vno %d", vno);
	if((v = xalloc(sizeof(Vctl))) == nil)
		panic("trapenable: out of memory");
	v->tbdf = BUSUNKNOWN;
	v->f = f;
	v->a = a;
	strncpy(v->name, name, KNAMELEN-1);
	v->name[KNAMELEN-1] = 0;

	ilock(&vctllock);
	if(vctl[vno])
		v->next = vctl[vno]->next;
	vctl[vno] = v;
	iunlock(&vctllock);
}

static void
nmienable(void)
{
	int x;

	/*
	 * Hack: should be locked with NVRAM access.
	 */
	outb(0x70, 0x80);		/* NMI latch clear */
	outb(0x70, 0);

	x = inb(0x61) & 0x07;		/* Enable NMI */
	outb(0x61, 0x0C|x);
	outb(0x61, x);
}

void
trapinit0(void)
{
	u32int d1, v;
	uintptr vaddr;
	Segdesc *idt;

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
}

void
trapinit(void)
{
	/*
	 * Special traps.
	 * Syscall() is called directly without going through trap().
	 */
	trapenable(VectorDE, debugexc, 0, "debugexc");
	trapenable(VectorBPT, debugbpt, 0, "debugpt");
	trapenable(VectorPF, faultamd64, 0, "faultamd64");
	trapenable(Vector2F, doublefault, 0, "doublefault");
	trapenable(Vector15, unexpected, 0, "unexpected");
	nmienable();
	addarchfile("irqalloc", 0444, irqallocread, nil);
	trapinited = 1;
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

	diff /= m->cpumhz*100;		/* quantum = 100µsec */
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
	tos = (Tos*)((uintptr)USTKTOP-sizeof(Tos));
	cycles(&t);
	tos->kcycles += t - up->kentry;
	tos->pcycles = t + up->pcycles;
	tos->pid = up->pid;
}

void
trap(Ureg *ureg)
{
	int clockintr, i, vno, user;
	char buf[ERRMAX];
	Vctl *ctl, *v;
	Mach *mach;

	if(!trapinited){
		/* faultamd64 can give a better error message */
		if(ureg->type == VectorPF)
			faultamd64(ureg, nil);
		panic("trap %llud: not ready", ureg->type);
	}

	m->perf.intrts = perfticks();
	user = userureg(ureg);
	if(user){
		up->dbgreg = ureg;
		cycles(&up->kentry);
	}

	clockintr = 0;

	vno = ureg->type;

	if(ctl = vctl[vno]){
		if(ctl->isintr){
			m->intr++;
			if(vno >= VectorPIC)
				m->lastintr = ctl->irq;
		}
		if(ctl->isr)
			ctl->isr(vno);
		for(v = ctl; v != nil; v = v->next){
			if(v->f)
				v->f(ureg, v->a);
		}
		if(ctl->eoi)
			ctl->eoi(vno);

		if(ctl->isintr){
			intrtime(m, vno);

			if(ctl->irq == IrqCLOCK || ctl->irq == IrqTIMER)
				clockintr = 1;

			if(up && !clockintr)
				preempted();
		}
	}
	else if(vno < nelem(excname) && user){
		spllo();
		sprint(buf, "sys: trap: %s", excname[vno]);
		postnote(up, 1, buf, NDebug);
	}
	else if(vno >= VectorPIC){
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

		/* clear the interrupt */
		i8259isr(vno);

		if(0)print("cpu%d: spurious interrupt %d, last %d\n",
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
			/*
			 * Don't re-enable, it confuses the crash dumps.
			nmienable();
			 */
			iprint("cpu%d: nmi PC %#p, status %ux\n",
				m->machno, ureg->pc, inb(0x61));
			while(m->machno != 0)
				;
		}

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
					return;
				}
			} else if(pc == _peekinst){
				if(vno == VectorGPF){
					ureg->pc += 2;
					return;
				}
			}
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
	splhi();

	/* delaysched set because we held a lock or because our quantum ended */
	if(up && up->delaysched && clockintr){
		sched();
		splhi();
	}

	if(user){
		if(up->procctl || up->nnote)
			notify(ureg);
		kexit(ureg);
	}
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
		sprint(buf, "sys: debug exception dr6=%#.8ullx", dr6);
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
	char buf[ERRMAX];

	if(up == 0)
		panic("kernel bpt");
	/* restore pc to instruction that caused the trap */
	ureg->pc--;
	sprint(buf, "sys: breakpoint");
	postnote(up, 1, buf, NDebug);
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

extern void checkpages(void);

static void
faultamd64(Ureg* ureg, void*)
{
	uintptr addr;
	int read, user, n, insyscall, f;
	char buf[ERRMAX];

	addr = getcr2();
	read = !(ureg->error & 2);
	user = userureg(ureg);
	if(!user){
		{
			extern void _peekinst(void);
			
			if((void(*)(void))ureg->pc == _peekinst){
				ureg->pc += 2;
				return;
			}
		}
		if(addr >= USTKTOP)
			panic("kernel fault: bad address pc=%#p addr=%#p", ureg->pc, addr);
		if(up == nil)
			panic("kernel fault: no user process pc=%#p addr=%#p", ureg->pc, addr);
	}
	if(up == nil)
		panic("user fault: up=0 pc=%#p addr=%#p", ureg->pc, addr);

	insyscall = up->insyscall;
	up->insyscall = 1;
	f = fpusave();
	if(!user && waserror()){
		if(up->nerrlab == 0){
			pprint("suicide: sys: %s\n", up->errstr);
			pexit(up->errstr, 1);
		}
		int s = splhi();
		fpurestore(f);
		up->insyscall = insyscall;
		splx(s);
		nexterror();
	}
	n = fault(addr, read);
	if(n < 0){
		if(!user){
			dumpregs(ureg);
			panic("fault: %#p", addr);
		}
		checkpages();
		sprint(buf, "sys: trap: fault %s addr=%#p",
			read ? "read" : "write", addr);
		postnote(up, 1, buf, NDebug);
	}
	if(!user) poperror();
	splhi();
	fpurestore(f);
	up->insyscall = insyscall;
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
	int	i, s, f;
	ulong scallnr;
	vlong startns, stopns;

	if(!userureg(ureg))
		panic("syscall: cs 0x%4.4lluX", ureg->cs);

	cycles(&up->kentry);

	m->syscall++;
	up->insyscall = 1;
	up->pc = ureg->pc;
	up->dbgreg = ureg;

	sp = ureg->sp;
	scallnr = ureg->bp;	/* RARG */
	up->scallnr = scallnr;
	f = fpusave();
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
		if(scallnr >= nsyscall || systab[scallnr] == 0){
			pprint("bad sys call number %lud pc %#p\n",
				scallnr, ureg->pc);
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

	splhi();
	fpurestore(f);
	up->insyscall = 0;
	up->psstate = 0;

	if(scallnr == NOTED){
		noted(ureg, *((ulong*)up->s.args));
		/*
		 * normally, syscall() returns to forkret()
		 * not restoring general registers when going
		 * to userspace. to completely restore the
		 * interrupted context, we have to return thru
		 * noteret(). we override return pc to jump to
		 * to it when returning form syscall()
		 */
		((void**)&ureg)[-1] = (void*)noteret;
	}

	if(scallnr!=RFORK && (up->procctl || up->nnote)){
		notify(ureg);
		((void**)&ureg)[-1] = (void*)noteret;	/* loads RARG */
	}

	/* if we delayed sched because we held a lock, sched now */
	if(up->delaysched)
		sched();
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
	uintptr sp;
	Note *n;

	if(up->procctl)
		procctl();
	if(up->nnote == 0)
		return 0;
	spllo();
	qlock(&up->debug);
	up->notepending = 0;
	n = &up->note[0];
	if(strncmp(n->msg, "sys:", 4) == 0){
		l = strlen(n->msg);
		if(l > ERRMAX-15)	/* " pc=0x12345678\0" */
			l = ERRMAX-15;
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
if(0) print("%s %lud: notify %#p %#p %#p %s\n",
	up->text, up->pid, ureg->pc, ureg->sp, sp, n->msg);

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
	memmove((char*)sp, up->note[0].msg, ERRMAX);
	sp -= 3*BY2WD;
	((uintptr*)sp)[2] = sp + 3*BY2WD;	/* arg2 string */
	((uintptr*)sp)[1] = (uintptr)up->ureg;	/* arg1 is ureg* */
	((uintptr*)sp)[0] = 0;			/* arg0 is pc */
	ureg->sp = sp;
	ureg->pc = (uintptr)up->notify;
	ureg->bp = (uintptr)up->ureg;		/* arg1 passed in RARG */
	ureg->cs = UESEL;
	ureg->ss = UDSEL;
	up->notified = 1;
	up->nnote--;
	memmove(&up->lastnote, &up->note[0], sizeof(Note));
	memmove(&up->note[0], &up->note[1], up->nnote*sizeof(Note));
	qunlock(&up->debug);
	splhi();
	if(up->fpstate == FPactive){
		fpsave(up->fpsave);
		up->fpstate = FPinactive;
	}
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

	up->fpstate &= ~FPillegal;
	spllo();
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
		splhi();
		ureg->sp = sp;
		ureg->bp = oureg;		/* arg 1 passed in RARG */
		((uintptr*)sp)[1] = oureg;	/* arg 1 0(FP) is ureg* */
		((uintptr*)sp)[0] = 0;		/* arg 0 is pc */
		break;

	default:
		up->lastnote.flag = NDebug;
		/* fall through */

	case NDFLT:
		qunlock(&up->debug);
		if(up->lastnote.flag == NDebug)
			pprint("suicide: %s\n", up->lastnote.msg);
		pexit(up->lastnote.msg, up->lastnote.flag!=NDebug);
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
	p->sched.pc = (uintptr)linkproc;
	p->sched.sp = (uintptr)p->kstack+KSTACK-BY2WD;

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
	p->sched.sp = (uintptr)p->kstack+KSTACK-(sizeof(Ureg)+2*BY2WD);
	p->sched.pc = (uintptr)forkret;

	cureg = (Ureg*)(p->sched.sp+2*BY2WD);
	memmove(cureg, ureg, sizeof(Ureg));

	cureg->ax = 0;

	/* Things from bottom of syscall which were never executed */
	p->psstate = 0;
	p->insyscall = 0;
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
