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
static Vctl *vclock, *vctl[256];

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

	ctl = vctl[vno];
	if(ctl != nil){
		if(vno < VectorPIC){
			(*ctl->f)(ureg, ctl->a);
			return 1;
		}

		m->perf.intrts = perfticks();
		m->intr++;
		m->lastintr = ctl->irq;
		if(ctl->isr != nil)
			(*ctl->isr)(vno);
		for(v = ctl; v != nil; v = v->next)
			(*v->f)(ureg, v->a);
		if(ctl->eoi != nil)
			(*ctl->eoi)(vno);
		intrtime(m, vno);
		preempted(ctl == vclock);
		return 1;
	}

	if(vno < VectorPIC || vno == VectorSYSCALL)
		return 0;

	m->spuriousintr++;
	if(arch->intrspurious != nil && (*arch->intrspurious)(vno) == 0)
		return 1;	/* false alarm */

	iprint("cpu%d: spurious interrupt %d, last %d\n",
		m->machno, vno, m->lastintr);

	/* call all non-local interrupt routines, just in case */
	for(i = VectorPIC; i < nelem(vctl); i++){
		ctl = vctl[i];
		if(ctl == nil || ctl == vclock || ctl->local)
			continue;
		for(v = ctl; v != nil; v = v->next)
			(*v->f)(ureg, v->a);
	}

	return -1;
}

void
trapenable(int vno, void (*f)(Ureg*, void*), void* a, char *name)
{
	Vctl *v;

	if(f == nil){
		print("trapenable: nil handler for %d, for %s\n",
			vno, name);
		return;
	}

	if(vno < 0 || vno >= VectorPIC){
		print("trapenable: vno %d out of range", vno);
		return;
	}

	if((v = xalloc(sizeof(Vctl))) == nil)
		panic("trapenable: out of memory");

	v->f = f;
	v->a = a;

	v->tbdf = BUSUNKNOWN;
	v->irq = -1;
	v->vno = vno;
	v->cpu = -1;

	strncpy(v->name, name, KNAMELEN-1);
	v->name[KNAMELEN-1] = 0;

	ilock(&vctllock);
	if(vctl[vno] != nil){
		print("trapenable: vno %d assigned twice: %s %s\n",
			vno, vctl[vno]->name, v->name);
		iunlock(&vctllock);
		xfree(v);
		return;
	}
	vctl[vno] = v;
	iunlock(&vctllock);
}

static Vctl*
delayfree(Vctl *v)
{
	static Vctl *q[4];
	static uint x;
	Vctl *r;

	r = q[x], q[x] = v;
	x = (x+1) % nelem(q);
	return r;
}

void
intrenable(int irq, void (*f)(Ureg*, void*), void* a, int tbdf, char *name)
{
	Vctl **pv, *v;

	if(f == nil){
		print("intrenable: nil handler for %d, tbdf 0x%uX for %s\n",
			irq, tbdf, name);
		return;
	}

	if(arch->intrirqno != nil)
		irq = (*arch->intrirqno)(irq, tbdf);

	if((v = xalloc(sizeof(Vctl))) == nil)
		panic("intrenable: out of memory");

	v->f = f;
	v->a = a;

	v->tbdf = tbdf;
	v->irq = irq;
	v->vno = -1;
	v->cpu = -1;

	strncpy(v->name, name, KNAMELEN-1);
	v->name[KNAMELEN-1] = 0;

	ilock(&vctllock);
	v->vno = (*arch->intrassign)(v);
	if(v->vno < VectorPIC || v->vno >= nelem(vctl)){
		print("intrenable: couldn't assign irq %d, tbdf 0x%uX for %s\n",
			irq, tbdf, v->name);
Unlockandfree:
		iunlock(&vctllock);
		if(v != nil)
			xfree(v);
		return;
	}
	pv = &vctl[v->vno];
	if(*pv != nil){
		if((*pv)->isr != v->isr || (*pv)->eoi != v->eoi){
			print("intrenable: incompatible handler: %s %s %#p %#p %#p %#p\n",
				(*pv)->name, v->name,
				(*pv)->isr, v->isr,
				(*pv)->eoi, v->eoi);
			goto Unlockandfree;
		}
		if(*pv == vclock)
			pv = &vclock->next;
		v->next = *pv;
	}
	if(strcmp(name, "clock") == 0)
		vclock = v;
	*pv = v;
	if(v->enable != nil){
		coherence();
		if((*v->enable)(v, pv != &vctl[v->vno] || v->next != nil) < 0){
			print("intrenable: couldn't enable irq %d, tbdf 0x%uX for %s\n",
				irq, tbdf, v->name);
			*pv = v->next;
			if(v == vclock)
				vclock = nil;
			if(conf.nmach > 1)
				v = delayfree(v);
			goto Unlockandfree;
		}
	}
	iunlock(&vctllock);
}

void
intrdisable(int irq, void (*f)(Ureg *, void *), void *a, int tbdf, char *name)
{
	Vctl **pv, *v;
	int vno;

	if(arch->intrirqno != nil)
		irq = (*arch->intrirqno)(irq, tbdf);

	if(irq != -1 && arch->intrvecno != nil) {
		vno = (*arch->intrvecno)(irq);
		if(vno < VectorPIC || vno >= nelem(vctl)){
			irq = -1;
			vno = VectorPIC;
		}
	} else {
		irq = -1;
		vno = VectorPIC;
	}

	ilock(&vctllock);
	do {
		for(pv = &vctl[vno]; (v = *pv) != nil; pv = &v->next){
			if((v->irq == irq || irq == -1)
			&& v->tbdf == tbdf && v->f == f && v->a == a
			&& strcmp(v->name, name) == 0)
				break;
		}
		if(v != nil){
			if(v->disable != nil){
				if((*v->disable)(v, pv != &vctl[vno] || v->next != nil) < 0){
					print("intrdisable: couldn't disable irq %d, tbdf 0x%uX for %s\n",
						irq, tbdf, name);
				}
				coherence();
			}
			*pv = v->next;
			if(v == vclock)
				vclock = nil;
			if(conf.nmach > 1)
				v = delayfree(v);
			iunlock(&vctllock);
			if(v != nil)
				xfree(v);
			return;
		}
	} while(irq == -1 && ++vno < nelem(vctl));
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

	for(vno = 0; vno < nelem(vctl); vno++){
		for(v = vctl[vno]; v != nil; v = v->next){
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
irqinit(void)
{
	addarchfile("irqalloc", 0444, irqallocread, nil);
}
