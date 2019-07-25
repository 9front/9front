#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "sysreg.h"
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

	GICC_CTLR	= 0x000/4,	/* RW, CPU Interace Control Register */
	GICC_PMR	= 0x004/4,	/* RW, Interrupt Priority Mask Register */
	GICC_BPR	= 0x008/4,	/* RW, Binary Point Register */
	GICC_IAR	= 0x00C/4,	/* RO, Interrupt Acknowledge Register */
	GICC_EOIR	= 0x010/4,	/* WO, End of Interrupt Register */
	GICC_RPR	= 0x014/4,	/* RO, Running Priority Register */
	GICC_HPPIR	= 0x018/4,	/* RO, Highest Priority Pending Interrupt Register */
	GICC_ABPR	= 0x01C/4,	/* RW, Aliased Binary Point Register */
	GICC_AIAR	= 0x020/4,	/* RO, Aliased Interrupt Acknowledge Register */
	GICC_AEOIR	= 0x024/4,	/* WO, Aliased End of Interrupt Register */
	GICC_AHPPIR	= 0x028/4,	/* RO, Aliased Highest Priority Pending Interrupt Register */
	GICC_APR0	= 0x0D0/4,	/* RW, Active Priority Register */
	GICC_NSAPR0	= 0x0E0/4,	/* RW, Non-Secure Active Priority Register */
	GICC_IIDR	= 0x0FC/4,	/* RO, CPU Interface Identification Register */
	GICC_DIR	= 0x1000/4,	/* WO, Deactivate Interrupt Register */

	GICH_HCR	= 0x000/4,	/* RW, Hypervisor Control Register */
	GICH_VTR	= 0x004/4,	/* RO, VGIC Type Register */
	GICH_VMCR	= 0x008/4,	/* RW, Virtual Machine Control Register */
	GICH_MISR	= 0x010/4,	/* RO, Maintenance Interrupt Status Register */
	GICH_EISR0	= 0x020/4,	/* RO, End of Interrupt Status Register */
	GICH_ELSR0	= 0x030/4,	/* RO, Empty List Register Status Register */
	GICH_APR0	= 0x0F0/4,	/* RW, Active Priority Register */
	GICH_LR0	= 0x100/4,	/* RW, List Registers (0x100-0x10C) */

	GICV_CTLR	= 0x000/4,	/* RW, Virtual Machine Control Register */
	GICV_PMR	= 0x004/4,	/* RW, VM Priority Mask Register */
	GICV_BPR	= 0x008/4,	/* RW, VM Binary Point Register */
	GICV_IAR	= 0x00C/4,	/* RO, VM Interrupt Acknowledge Register */
	GICV_EOIR	= 0x010/4,	/* WO, VM End of Interrupt Register */
	GICV_RPR	= 0x014/4,	/* RO, VM Running Priority Register */
	GICV_HPPIR	= 0x018/4,	/* RO, VM Highest Piority Pending Interrupt Register */
	GICV_ABPR	= 0x01C/4,	/* RW, VM Aliased Binary Point Register */
	GICV_AIAR	= 0x020/4,	/* RO, VM Aliased Interrupt Acknowledge Register */
	GICV_AEOIR	= 0x024/4,	/* WO, VM Aliased End of Interrupt Register */
	GICV_AHPPIR	= 0x028/4,	/* RO, VM Aliaed Highest Piority Pending Interrupt Register */
	GICV_APR0	= 0x0D0/4,	/* RW, VM Active Priority Register */
	GICV_IIDR	= 0x0FC/4,	/* RO, VM CPU Interface Identification Register */
	GICV_DIR	= 0x1000/4,	/* WO, VM Deactivate Interrupt Register */
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
static u32int *cregs, *dregs;

void
intrcpushutdown(void)
{
	if(cregs == nil || dregs == nil){
		uintptr va, pa;

		pa = sysrd(CBAR_EL1);
		va = ARMLOCAL + (pa - soc.armlocal);
		dregs = (u32int*)(va + 0x1000);
		cregs = (u32int*)(va + 0x2000);
	}

	/* disable cpu interface */
	cregs[GICC_CTLR] &= ~1;
	coherence();
}

void
intrsoff(void)
{
	int i, n;

	intrcpushutdown();

	/* disable distributor */
	dregs[GICD_CTLR] &= ~1;
	coherence();

	/* clear all interrupts */
	n = ((dregs[GICD_TYPER] & 0x1F)+1) << 5;
	for(i = 0; i < n; i += 32){
		dregs[GICD_ISENABLER0 + (i/32)] = -1;
		coherence();
		dregs[GICD_ICENABLER0 + (i/32)] = -1;
		coherence();
	}
	for(i = 0; i < n; i += 4){
		dregs[GICD_IPRIORITYR0 + (i/4)] = 0;
		dregs[GICD_TARGETSR0 + (i/4)] = 0;
	}
	for(i = 32; i < n; i += 16)
		dregs[GICD_ICFGR0 + (i/16)] = 0;
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
	intid = cregs[GICC_IAR] & 0xFFFFFF;
	if((intid & ~3) == 1020)
		return 0; // spurious
	clockintr = 0;
	for(v = vctl[m->machno][intid%32]; v != nil; v = v->next)
		if(v->intid == intid){
			coherence();
			v->f(ureg, v->a);
			coherence();
			if(v->irq == IRQclock || v->irq == IRQcntps || v->irq == IRQcntpns)
				clockintr = 1;
		}
	coherence();
	cregs[GICC_EOIR] = intid;
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
	intid = cregs[GICC_IAR] & 0xFFFFFF;
	if((intid & ~3) == 1020)
		return;	// spurious
	v = vfiq;
	if(v != nil && v->intid == intid && m->machno == 0){
		coherence();
		v->f(ureg, v->a);
		coherence();
	}
	cregs[GICC_EOIR] = intid;
}

void
intrenable(int irq, void (*f)(Ureg*, void*), void *a, int tbdf, char*)
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

	cpu = 0;
	prio = 0x80;
	intid = irq;
	switch(irq){
	case IRQcntps:
		intid = 16 + 13;
		break;
	case IRQcntpns:
		intid = 16 + 14;
		break;

	case IRQmbox0:
	case IRQmbox1:
	case IRQmbox2:
	case IRQmbox3:
	case IRQlocaltmr:
		print("irqenable: missing documentation for local irq %d\n", irq);
		return;

	default:
		if(irq < IRQgic){
			if(irq < 64)
				intid += IRQgic-64;
			else if(irq >= IRQbasic)
				intid += IRQgic-64-32-8-IRQbasic;
		}
	}
	if(intid < 32)
		cpu = m->machno;

	if((v = xalloc(sizeof(Vctl))) == nil)
		panic("irqenable: no mem");
	v->irq = irq;
	v->intid = intid;
	v->f = f;
	v->a = a;

	lock(&vctllock);
	if(irq == IRQfiq){
		vfiq = v;
		prio = 0;
	}else{
		v->next = vctl[cpu][intid%32];
		vctl[cpu][intid%32] = v;
	}

	/* enable cpu interface */
	cregs[GICC_PMR] = 0xFF;
	coherence();

	cregs[GICC_CTLR] |= 1;
	coherence();

	cregs[GICC_EOIR] = intid;

	/* enable distributor */
	dregs[GICD_CTLR] |= 1;
	coherence();

	/* setup */
	dregs[GICD_IPRIORITYR0 + (intid/4)] |= prio << ((intid%4) << 3);
	dregs[GICD_TARGETSR0 + (intid/4)] |= (1<<cpu) << ((intid%4) << 3);
	coherence();

	/* turn on */
	dregs[GICD_ISENABLER0 + (intid/32)] = 1 << (intid%32);
	coherence();

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
