#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "ureg.h"
#include "../arm64/sysreg.h"
#include "../port/error.h"

enum {
	GICD_CTLR	= 0x000/4,	/* RW, Distributor Control Register */
	GICD_TYPER	= 0x004/4,	/* RO, Interrupt Controller Type */
	GICD_IIDR	= 0x008/4,	/* RO, Distributor Implementer Identification Register */

	GICD_IGROUPR0	= 0x080/4,	/* RW, Interrupt Group Registers (0x80-0xBC) */

	GICD_ISENABLER0	= 0x100/4,	/* RW, Interrupt Set-Enable Registers (0x100-0x13C) */
	GICD_ICENABLER0	= 0x180/4,	/* RW, Interrupt Clear-Enable Registers (0x180-0x1BC) */

	GICD_ISPENDR0	= 0x200/4,	/* RW, Interrupt Set-Pending Registers (0x200-0x23C) */
	GICD_ICPENDR0	= 0x280/4,	/* RW, Interrupt Clear-Pending Registers (0x280-0x2BC) */

	GICD_ISACTIVER0	= 0x300/4,	/* RW, Interrupt Set-Active Registers (0x300-0x33C) */
	GICD_ICACTIVER0 = 0x380/4,	/* RW, Interrupt Clear-Active Registers (0x380-0x3BC) */

	GICD_IPRIORITYR0= 0x400/4,	/* RW, Interrupt Priority Registers (0x400-0x5FC) */
	GICD_TARGETSR0	= 0x800/4,	/* RW, Interrupt Target Registers (0x800-0x9FC) */
	GICD_ICFGR0	= 0xC00/4,	/* RW, Interrupt Configuration Registers (0xC00-0xC7C) */

	GICD_ISR0	= 0xD00/4,
	GICD_PPISR	= GICD_ISR0,	/* RO, Private Peripheral Interrupt Status Register */
	GICD_SPISR0	= GICD_ISR0+1,	/* RO, Shared Peripheral Interrupt Status Register */
	GICD_SGIR	= 0xF00/4,	/* WO, Software Generated Interrupt Register */

	GICD_CPENDSGIR0	= 0xF10/4,	/* RW, SGI Clear-Pending Registers (0xF10-0xF1C) */
	GICD_SPENDSGIR0	= 0xF20/4,	/* RW, SGI Set-Pending Registers (0xF20-0xF2C) */

	GICD_PIDR4	= 0xFD0/4,	/* RO, Perpheral ID Registers */
	GICD_PIDR5	= 0xFD4/4,
	GICD_PIDR6	= 0xFD8/4,
	GICD_PIDR7	= 0xFDC/4,
	GICD_PIDR0	= 0xFE0/4,
	GICD_PIDR1	= 0xFE4/4,
	GICD_PIDR2	= 0xFE8/4,
	GICD_PIDR3	= 0xFEC/4,

	GICD_CIDR0	= 0xFF0/4,	/* RO, Component ID Registers */
	GICD_CIDR1	= 0xFF4/4,
	GICD_CIDR2	= 0xFF8/4,
	GICD_CIDR3	= 0xFFC/4,

	RD_base		= 0x00000,
	GICR_CTLR	= (RD_base+0x000)/4,
	GICR_IIDR	= (RD_base+0x004)/4,
	GICR_TYPER	= (RD_base+0x008)/4,
	GICR_STATUSR	= (RD_base+0x010)/4,
	GICR_WAKER	= (RD_base+0x014)/4,
	GICR_SETLPIR	= (RD_base+0x040)/4,
	GICR_CLRLPIR	= (RD_base+0x048)/4,
	GICR_PROPBASER	= (RD_base+0x070)/4,
	GICR_PENDBASER	= (RD_base+0x078)/4,
	GICR_INVLPIR	= (RD_base+0x0A0)/4,
	GICR_INVALLR	= (RD_base+0x0B0)/4,
	GICR_SYNCR	= (RD_base+0x0C0)/4,

	SGI_base	= 0x10000,
	GICR_IGROUPR0	= (SGI_base+0x080)/4,
	GICR_ISENABLER0	= (SGI_base+0x100)/4,
	GICR_ICENABLER0	= (SGI_base+0x180)/4,
	GICR_ISPENDR0	= (SGI_base+0x200)/4,
	GICR_ICPENDR0	= (SGI_base+0x280)/4,
	GICR_ISACTIVER0	= (SGI_base+0x300)/4,
	GICR_ICACTIVER0	= (SGI_base+0x380)/4,
	GICR_IPRIORITYR0= (SGI_base+0x400)/4,
	GICR_ICFGR0	= (SGI_base+0xC00)/4,
	GICR_ICFGR1	= (SGI_base+0xC04)/4,
	GICR_IGRPMODR0	= (SGI_base+0xD00)/4,
	GICR_NSACR	= (SGI_base+0xE00)/4,
};

typedef struct Vctl Vctl;
struct Vctl {
	Vctl	*next;
	void	(*f)(Ureg*, void*);
	void	*a;
	int	irq;
	u32int	intid;
};

static Lock vctllock;
static Vctl *vctl[MAXMACH][32], *vfiq;
static u32int *dregs = (u32int*)(VIRTIO + 0x8800000);

static u32int*
getrregs(int machno)
{
	u32int *rregs = (u32int*)(VIRTIO + 0x8880000);

	for(;;){
		if((rregs[GICR_TYPER] & 0xFFFF00) == (machno << 8))
			return rregs;
		if(rregs[GICR_TYPER] & (1<<4))
			break;
		rregs += (0x20000/4);
	}
	panic("getrregs: no re-distributor for cpu %d\n", machno);
}

void
intrcpushutdown(void)
{
	/* disable cpu interface */
	syswr(ICC_IGRPEN0_EL1, 0);
	syswr(ICC_IGRPEN1_EL1, 0);
	coherence();
}

void
intrsoff(void)
{
	/* disable distributor */
	dregs[GICD_CTLR] = 0;
	coherence();
	while(dregs[GICD_CTLR]&(1<<31))
		;
}

void
intrinit(void)
{
	u32int *rregs;
	int i, n;

	if(m->machno == 0){
		intrsoff();

		/* clear all interrupts */
		n = ((dregs[GICD_TYPER] & 0x1F)+1) << 5;
		for(i = 32; i < n; i += 32){
			dregs[GICD_IGROUPR0 + (i/32)] = -1;

			dregs[GICD_ISENABLER0 + (i/32)] = -1;
			while(dregs[GICD_CTLR]&(1<<31))
				;
			dregs[GICD_ICENABLER0 + (i/32)] = -1;
			while(dregs[GICD_CTLR]&(1<<31))
				;
			dregs[GICD_ICACTIVER0 + (i/32)] = -1;
		}
		for(i = 0; i < n; i += 4){
			dregs[GICD_IPRIORITYR0 + (i/4)] = 0;
			dregs[GICD_TARGETSR0 + (i/4)] = 0;
		}
		for(i = 32; i < n; i += 16){
			dregs[GICD_ICFGR0 + (i/16)] = 0;
		}
		coherence();
		while(dregs[GICD_CTLR]&(1<<31))
			;
		dregs[GICD_CTLR] = (1<<0) | (1<<1) | (1<<4);
	}

	rregs = getrregs(m->machno);
	n = 32;
	for(i = 0; i < n; i += 32){
		rregs[GICR_IGROUPR0 + (i/32)] = -1;

		rregs[GICR_ISENABLER0 + (i/32)] = -1;
		while(rregs[GICR_CTLR]&(1<<3))
			;
		rregs[GICR_ICENABLER0 + (i/32)] = -1;
		while(dregs[GICD_CTLR]&(1<<31))
			;
		rregs[GICR_ICACTIVER0 + (i/32)] = -1;
	}
	for(i = 0; i < n; i += 4){
		rregs[GICR_IPRIORITYR0 + (i/4)] = 0;
	}
	coherence();
	while(rregs[GICR_CTLR]&(1<<3))
		;

	coherence();

	/* enable cpu interface */
	syswr(ICC_CTLR_EL1, 0);
	syswr(ICC_BPR1_EL1, 7);
	syswr(ICC_PMR_EL1, 0xFF);

	coherence();
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
	u32int intid;

	m->intr++;
	intid = sysrd(ICC_IAR1_EL1) & 0xFFFFFF;
// iprint("i<%d>", intid);
	if((intid & ~3) == 1020)
		return 0; // spurious
	clockintr = 0;
	for(v = vctl[m->machno][intid%32]; v != nil; v = v->next)
		if(v->intid == intid){
			coherence();
			v->f(ureg, v->a);
			coherence();
			if(v->irq == IRQcntpns)
				clockintr = 1;
		}
	coherence();
	syswr(ICC_EOIR1_EL1, intid);
	return clockintr;
}

/*
 * called direct from lexception.s to handle fiq interrupt.
 */
void
fiq(Ureg *ureg)
{
	Vctl *v;
	u32int intid;

	m->intr++;
	intid = sysrd(ICC_IAR1_EL1) & 0xFFFFFF;
// iprint("f<%d>", intid);
	if((intid & ~3) == 1020)
		return;	// spurious
	v = vfiq;
	if(v != nil && v->intid == intid && m->machno == 0){
		coherence();
		v->f(ureg, v->a);
		coherence();
	}
	syswr(ICC_EOIR1_EL1, intid);
}

void
intrenable(int irq, void (*f)(Ureg*, void*), void *a, int tbdf, char *)
{
	Vctl *v;
	u32int intid;
	int cpu, prio;

	if(BUSTYPE(tbdf) == BusPCI){
		pciintrenable(tbdf, f, a);
		return;
	}

	if(tbdf != BUSUNKNOWN)
		return;

	prio = 0x80;
	intid = irq;
	if((v = xalloc(sizeof(Vctl))) == nil)
		panic("intrenable: no mem");
	v->irq = irq;
	v->intid = intid;
	v->f = f;
	v->a = a;

	lock(&vctllock);
	if(intid < SPI)
		cpu = m->machno;
	else
		cpu = 0;
	if(irq == IRQfiq){
		vfiq = v;
		prio = 0;
	}else{
		v->next = vctl[cpu][intid%32];
		vctl[cpu][intid%32] = v;
	}
	syswr(ICC_IGRPEN1_EL1, sysrd(ICC_IGRPEN1_EL1)|1);
	coherence();

	syswr(ICC_EOIR1_EL1, intid);
	coherence();

	/* setup */
	if(intid < 32){
		u32int *rregs = getrregs(cpu);
		rregs[GICR_IPRIORITYR0 + (intid/4)] |= prio << ((intid%4) << 3);
		coherence();
		rregs[GICR_ISENABLER0] = 1 << (intid%32);
		coherence();
		while(rregs[GICR_CTLR]&(1<<3))
			;
	} else {
		dregs[GICD_IPRIORITYR0 + (intid/4)] |= prio << ((intid%4) << 3);
		dregs[GICD_TARGETSR0 + (intid/4)] |= (1<<cpu) << ((intid%4) << 3);
		coherence();
		dregs[GICD_ISENABLER0 + (intid/32)] = 1 << (intid%32);
		coherence();
		while(dregs[GICD_CTLR]&(1<<31))
			;
	}
	unlock(&vctllock);
}

void
intrdisable(int, void (*f)(Ureg*, void*), void *a, int tbdf, char*)
{
	if(BUSTYPE(tbdf) == BusPCI){
		pciintrdisable(tbdf, f, a);
		return;
	}
}
