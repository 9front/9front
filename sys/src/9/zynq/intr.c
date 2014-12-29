#include "u.h"
#include <ureg.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

enum {
	NINTR = 96,
	NPRIVATE = 32
};

static struct irq {
	void (*f)(Ureg *, void *);
	void *arg;
	char *name;
} irqs[NINTR];

enum {
	ICCICR = 0x100/4,
	ICCPMR,
	ICCBPR,
	ICCIAR,
	ICCEOIR,
	ICDDCR = 0x1000/4,
	ICDISR = 0x1080/4,
	ICDISER = 0x1100/4,
	ICDICER = 0x1180/4,
	ICDICPR = 0x1280/4,
	ICDABR  = 0x1300/4,
	ICDIPRI = 0x1400/4,
	ICDIPTR = 0x1800/4,
	ICDICFR = 0x1C00/4,
};

void
intrinit(void)
{
	int i;

	mpcore[ICDDCR] = 3;
	mpcore[ICCICR] = 7;
	mpcore[ICCBPR] = 3;
	mpcore[ICCPMR] = 255;

	if(m->machno != 0)
		return;
	
	/* disable all irqs and clear any pending interrupts */
	for(i = 0; i < NINTR/32; i++){
		mpcore[ICDISR + i] = -1;
		mpcore[ICDICER + i] = -1;
		mpcore[ICDICPR + i] = -1;
		mpcore[ICDABR + i] = 0;
	}
}

void
intrenable(int irq, void (*f)(Ureg *, void *), void *arg, int type, char *name)
{
	ulong *e, s;
	struct irq *i;

	if(f == nil)
		panic("intrenable: f == nil");
	if(irq < 0 || irq >= NINTR)
		panic("intrenable: invalid irq %d", irq);
	if(type != LEVEL && type != EDGE)
		panic("intrenable: invalid type %d", type);
	if(irqs[irq].f != nil && irqs[irq].f != f)
		panic("intrenable: handler already assigned");
	if(irq >= NPRIVATE){
		e = &mpcore[ICDIPTR + (irq >> 2)];
		s = irq << 3 & 24;
		*e = *e & ~(3 << s) | 1 << s;
		e = &mpcore[ICDICFR + (irq >> 4)];
		s = irq << 1 & 30 | 1;
		*e = *e & ~(1 << s) | type << s;
	}
	((uchar*)&mpcore[ICDIPRI])[irq] = 0;
	i = &irqs[irq];
	i->f = f;
	i->arg = arg;
	i->name = name;
	mpcore[ICDISER + (irq >> 5)] = 1 << (irq & 31);
	mpcore[ICDABR + (irq >> 5)] |= 1 << (irq & 31);
}

void
intr(Ureg *ureg)
{
	ulong v;
	int irq;
	struct irq *i;

	v = mpcore[ICCIAR];
	irq = v & 0x3ff;
	if(irq == 0x3ff)
		return;
		
	m->intr++;
	m->lastintr = irq;
	i = &irqs[irq];
	if(i->f == nil)
		print("irq without handler %d\n", irq);
	else
		i->f(ureg, i->arg);
	mpcore[ICCEOIR] = v;

	if(up != nil){
		if(irq == TIMERIRQ){
			if(up->delaysched){
				splhi();
				sched();
			}
		}else
			preempted();
	}
}
