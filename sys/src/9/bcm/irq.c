#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

#define INTREGS		(VIRTIO+0xB200)

enum {
	Fiqenable = 1<<7,

	Localtimerint	= 0x40,
	Localmboxint	= 0x50,
	Localintpending	= 0x60,
};

/*
 * interrupt control registers
 */
typedef struct Intregs Intregs;
struct Intregs {
	u32int	ARMpending;
	u32int	GPUpending[2];
	u32int	FIQctl;
	u32int	GPUenable[2];
	u32int	ARMenable;
	u32int	GPUdisable[2];
	u32int	ARMdisable;
};

typedef struct Vctl Vctl;
struct Vctl {
	Vctl	*next;
	int	irq;
	u32int	*reg;
	u32int	mask;
	void	(*f)(Ureg*, void*);
	void	*a;
};

static Lock vctllock;
static Vctl *vctl[MAXMACH], *vfiq;

void
intrcpushutdown(void)
{
	u32int *enable;

	if(soc.armlocal == 0)
		return;
	enable = (u32int*)(ARMLOCAL + Localtimerint) + m->machno;
	*enable = 0;
	if(m->machno){
		enable = (u32int*)(ARMLOCAL + Localmboxint) + m->machno;
		*enable = 1;
	}
}

void
intrsoff(void)
{
	Intregs *ip;
	int disable;

	ip = (Intregs*)INTREGS;
	disable = ~0;
	ip->GPUdisable[0] = disable;
	ip->GPUdisable[1] = disable;
	ip->ARMdisable = disable;
	ip->FIQctl = 0;
}

/*
 *  called by trap to handle irq interrupts.
 *  returns true iff a clock interrupt, thus maybe reschedule.
 */
int
irq(Ureg* ureg)
{
	Vctl *v;
	int clockintr;

	m->intr++;
	clockintr = 0;
	for(v = vctl[m->machno]; v != nil; v = v->next)
		if((*v->reg & v->mask) != 0){
			coherence();
			v->f(ureg, v->a);
			coherence();
			if(v->irq == IRQclock || v->irq == IRQcntps || v->irq == IRQcntpns)
				clockintr = 1;
		}
	return clockintr;
}

/*
 * called direct from lexception.s to handle fiq interrupt.
 */
void
fiq(Ureg *ureg)
{
	Vctl *v;

	m->intr++;
	v = vfiq;
	if(v == nil || m->machno)
		panic("cpu%d: unexpected item in bagging area", m->machno);
	coherence();
	v->f(ureg, v->a);
	coherence();
}

void
intrenable(int irq, void (*f)(Ureg*, void*), void* a, int, char*)
{
	Vctl *v;
	Intregs *ip;
	u32int *enable;
	int cpu;

	ip = (Intregs*)INTREGS;
	if((v = xalloc(sizeof(Vctl))) == nil)
		panic("irqenable: no mem");
	cpu = 0;
	v->irq = irq;
	if(irq >= IRQlocal){
		cpu = m->machno;
		v->reg = (u32int*)(ARMLOCAL + Localintpending) + cpu;
		if(irq >= IRQmbox0)
			enable = (u32int*)(ARMLOCAL + Localmboxint) + cpu;
		else
			enable = (u32int*)(ARMLOCAL + Localtimerint) + cpu;
		v->mask = 1 << (irq - IRQlocal);
	}else if(irq >= IRQbasic){
		enable = &ip->ARMenable;
		v->reg = &ip->ARMpending;
		v->mask = 1 << (irq - IRQbasic);
	}else{
		enable = &ip->GPUenable[irq/32];
		v->reg = &ip->GPUpending[irq/32];
		v->mask = 1 << (irq % 32);
	}
	v->f = f;
	v->a = a;
	lock(&vctllock);
	if(irq == IRQfiq){
		assert((ip->FIQctl & Fiqenable) == 0);
		assert((*enable & v->mask) == 0);
		assert(cpu == 0);
		vfiq = v;
		ip->FIQctl = Fiqenable | irq;
	}else{
		v->next = vctl[cpu];
		vctl[cpu] = v;
		if(irq >= IRQmbox0){
			if(irq <= IRQmbox3)
				*enable |= 1 << (irq - IRQmbox0);
		}else if(irq >= IRQlocal)
			*enable |= 1 << (irq - IRQlocal);
		else
			*enable = v->mask;
	}
	unlock(&vctllock);
}

void
intrdisable(int, void (*)(Ureg*, void*), void*, int, char*)
{
}
