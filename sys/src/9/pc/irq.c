#include	"u.h"
#include	"tos.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"ureg.h"
#include	"../port/error.h"

static Lock vctllock;
static Vctl *vctl[256];

enum
{
	Ntimevec = 20		/* number of time buckets for each intr */
};
ulong intrtimes[256][Ntimevec];

/*
 *  keep histogram of interrupt service times
 */
static void
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

int
irqhandled(Ureg *ureg, int vno)
{
	Vctl *ctl, *v;
	int i;

	if(ctl = vctl[vno]){
		if(ctl->isintr){
			m->perf.intrts = perfticks();
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

			if(up){
				if(ctl->irq == IrqCLOCK || ctl->irq == IrqTIMER){
					/* delaysched set because we held a lock or because our quantum ended */
					if(up->delaysched)
						sched();
				} else {
					preempted();
				}
			}
		}
		return 1;
	}

	if(vno < VectorPIC)
		return 0;

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
			Mach *mach;

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
	return -1;
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

	if(tbdf != BUSUNKNOWN && (irq == 0xff || irq == 0))
		irq = -1;


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
			if(v->disable != nil)
				(*v->disable)(v);
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

static void
nmihandler(Ureg *ureg, void*)
{
	/*
	 * Don't re-enable, it confuses the crash dumps.
	nmienable();
	 */
	iprint("cpu%d: nmi PC %#p, status %ux\n",
		m->machno, ureg->pc, inb(0x61));
	while(m->machno != 0)
		;
}

void
irqinit(void)
{
	addarchfile("irqalloc", 0444, irqallocread, nil);

	trapenable(VectorNMI, nmihandler, nil, "nmi");
	nmienable();
}
