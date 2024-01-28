#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "ureg.h"

#include "mp.h"
#include "apbootstrap.i"

extern void i8259init(void);

/* filled in by pcmpinit or acpiinit */
Bus* mpbus;
Bus* mpbuslast;
int mpisabus = -1;
int mpeisabus = -1;
Apic *mpioapic[MaxAPICNO+1];
Apic *mpapic[MaxAPICNO+1];

int
mpintrinit(Bus* bus, PCMPintr* intr, int vno, int /*irq*/)
{
	int el, po, v;

	/*
	 * Parse an I/O or Local APIC interrupt table entry and
	 * return the encoded vector.
	 */
	v = vno;

	po = intr->flags & PcmpPOMASK;
	el = intr->flags & PcmpELMASK;

	switch(intr->intr){
	default:				/* PcmpINT */
		v |= ApicFIXED;			/* no-op */
		break;

	case PcmpNMI:
		v |= ApicNMI;
		po = PcmpHIGH;
		el = PcmpEDGE;
		break;

	case PcmpSMI:
		v |= ApicSMI;
		break;

	case PcmpExtINT:
		v |= ApicExtINT;
		/*
		 * The AMI Goliath doesn't boot successfully with it's LINTR0
		 * entry which decodes to low+level. The PPro manual says ExtINT
		 * should be level, whereas the Pentium is edge. Setting the
		 * Goliath to edge+high seems to cure the problem. Other PPro
		 * MP tables (e.g. ASUS P/I-P65UP5 have a entry which decodes
		 * to edge+high, so who knows.
		 * Perhaps it would be best just to not set an ExtINT entry at
		 * all, it shouldn't be needed for SMP mode.
		 */
		po = PcmpHIGH;
		el = PcmpEDGE;
		break;
	}

	/*
	 */
	if(bus->type == BusEISA && !po && !el /*&& !(i8259elcr & (1<<irq))*/){
		po = PcmpHIGH;
		el = PcmpEDGE;
	}
	if(!po)
		po = bus->po;
	if(po == PcmpLOW)
		v |= ApicLOW;
	else if(po != PcmpHIGH){
		print("mpintrinit: bad polarity 0x%uX\n", po);
		return ApicIMASK;
	}

	if(!el)
		el = bus->el;
	if(el == PcmpLEVEL)
		v |= ApicLEVEL;
	else if(el != PcmpEDGE){
		print("mpintrinit: bad trigger 0x%uX\n", el);
		return ApicIMASK;
	}

	return v;
}

uvlong
tscticks(uvlong *hz)
{
	if(hz != nil)
		*hz = m->cpuhz;

	cycles(&m->tscticks);	/* Uses the rdtsc instruction */
	return m->tscticks;
}

void
syncclock(void)
{
	uvlong x;

	if(arch->fastclock != tscticks)
		return;

	if(m->machno == 0){
		wrmsr(0x10, 0);
		m->tscticks = 0;
	} else {
		x = MACHP(0)->tscticks;
		while(x == MACHP(0)->tscticks)
			;
		wrmsr(0x10, MACHP(0)->tscticks);
		cycles(&m->tscticks);
	}
}

void
mpinit(void)
{
	int ncpu, i;
	Apic *apic;
	char *cp;

	i8259init();
	syncclock();

	if(getconf("*apicdebug")){
		Bus *b;
		Aintr *ai;
		PCMPintr *pi;

		for(i=0; i<=MaxAPICNO; i++){
			if(apic = mpapic[i])
				print("LAPIC%d: pa=%lux va=%#p flags=%x\n",
					i, apic->paddr, apic->addr, apic->flags);
			if(apic = mpioapic[i])
				print("IOAPIC%d: pa=%lux va=%#p flags=%x gsibase=%d mre=%d\n",
					i, apic->paddr, apic->addr, apic->flags, apic->gsibase, apic->mre);
		}
		for(b = mpbus; b; b = b->next){
			print("BUS%d type=%d flags=%x\n", b->busno, b->type, b->po|b->el);
			for(ai = b->aintr; ai; ai = ai->next){
				if(pi = ai->intr)
					print("\ttype=%d irq=%d (%d [%c]) apic=%d intin=%d flags=%x\n",
						pi->type, pi->irq, pi->irq>>2, "ABCD"[pi->irq&3],
						pi->apicno, pi->intin, pi->flags);
			}
		}
	}

	apic = nil;
	for(i=0; i<=MaxAPICNO; i++){
		if(mpapic[i] == nil)
			continue;
		if(mpapic[i]->flags & PcmpBP){
			apic = mpapic[i];
			break;
		}
	}

	if(apic == nil){
		panic("mpinit: no bootstrap processor");
	}
	apic->online = 1;

	lapicinit(apic);

	/*
	 * These interrupts are local to the processor
	 * and do not appear in the I/O APIC so it is OK
	 * to set them now.
	 */
	intrenable(IrqTIMER, lapicclock, 0, BUSUNKNOWN, "clock");
	intrenable(IrqERROR, lapicerror, 0, BUSUNKNOWN, "lapicerror");
	intrenable(IrqSPURIOUS, lapicspurious, 0, BUSUNKNOWN, "lapicspurious");
	lapiconline();

	/*
	 * Initialise the application processors.
	 */
	if(cp = getconf("*ncpu")){
		ncpu = strtol(cp, 0, 0);
		if(ncpu < 1)
			ncpu = 1;
		else if(ncpu > MAXMACH)
			ncpu = MAXMACH;
	}
	else
		ncpu = MAXMACH;
	memmove((void*)APBOOTSTRAP, apbootstrap, sizeof(apbootstrap));
	for(i=0; i<nelem(mpapic); i++){
		if((apic = mpapic[i]) == nil)
			continue;
		if(apic->machno >= MAXMACH)
			continue;
		if(ncpu <= 1)
			break;
		if((apic->flags & (PcmpBP|PcmpEN)) == PcmpEN){
			mpstartap(apic);
			conf.nmach++;
			ncpu--;
		}
	}

	/*
	 *  we don't really know the number of processors till
	 *  here.
	 *
	 *  set conf.copymode here if nmach > 1.
	 *  Should look for an ExtINT line and enable it.
	 */
	if(m->cpuidfamily == 3 || conf.nmach > 1)
		conf.copymode = 1;
}

static int
mpintrcpu(void)
{
	static Lock physidlock;
	static int physid;
	int i;

	/*
	 * The bulk of this code was written ~1995, when there was
	 * one architecture and one generation of hardware, the number
	 * of CPUs was up to 4(8) and the choices for interrupt routing
	 * were physical, or flat logical (optionally with lowest
	 * priority interrupt). Logical mode hasn't scaled well with
	 * the increasing number of packages/cores/threads, so the
	 * fall-back is to physical mode, which works across all processor
	 * generations, both AMD and Intel, using the APIC and xAPIC.
	 *
	 * Interrupt routing policy can be set here.
	 * Currently, just assign each interrupt to a different CPU on
	 * a round-robin basis. Some idea of the packages/cores/thread
	 * topology would be useful here, e.g. to not assign interrupts
	 * to more than one thread in a core, or to use a "noise" core.
	 * But, as usual, Intel make that an onerous task. 
	 */
	lock(&physidlock);
	for(;;){
		i = physid++;
		if(physid >= nelem(mpapic))
			physid = 0;
		if(mpapic[i] == nil)
			continue;
		if(mpapic[i]->online)
			break;
	}
	unlock(&physidlock);

	return mpapic[i]->apicno;
}

/*
 * With the APIC a unique vector can be assigned to each
 * request to enable an interrupt. There are two reasons this
 * is a good idea:
 * 1) to prevent lost interrupts, no more than 2 interrupts
 *    should be assigned per block of 16 vectors (there is an
 *    in-service entry and a holding entry for each priority
 *    level and there is one priority level per block of 16
 *    interrupts).
 * 2) each input pin on the IOAPIC will receive a different
 *    vector regardless of whether the devices on that pin use
 *    the same IRQ as devices on another pin.
 */
static int
allocvector(void)
{
	static int round = 0, num = 0;
	static Lock l;
	int vno;
	
	lock(&l);
	vno = VectorAPIC + num;
	if(vno < MaxVectorAPIC-7)
		num += 8;
	else
		num = ++round % 8;
	unlock(&l);
	return vno;
}

static int
ioapicirqenable(Vctl *v, int shared)
{
	Aintr *aintr = v->aux;
	int lo, hi;

	if(shared)
		return 0;
	hi = v->cpu<<24;
	lo = mpintrinit(aintr->bus, aintr->intr, v->vno, v->irq);
	lo |= ApicPHYSICAL;			/* no-op */
 	ioapicrdtw(aintr->apic, aintr->intr->intin, hi, lo);
	return 0;
}

static int
ioapicirqdisable(Vctl *v, int shared)
{
	Aintr *aintr = v->aux;
	int lo, hi;

	if(shared)
		return 0;
	hi = 0;
	lo = ApicIMASK;
 	ioapicrdtw(aintr->apic, aintr->intr->intin, hi, lo);
	return 0;
}

static int
mpintrassignx(Vctl* v, int tbdf)
{
	Bus *bus;
	Pcidev *pci;
	Aintr *aintr;
	int bno, dno, pin, irq, type, lo, hi, n;

	type = BUSTYPE(tbdf);
	bno = BUSBNO(tbdf);
	dno = BUSDNO(tbdf);

	pin = 0;
	pci = nil;
	if(type == BusPCI){
		if((pci = pcimatchtbdf(tbdf)) != nil)
			pin = pcicfgr8(pci, PciINTP);
	} else if(type == BusISA)
		bno = mpisabus;

Findbus:
	for(bus = mpbus; bus != nil; bus = bus->next){
		if(bus->type != type)
			continue;
		if(bus->busno == bno)
			break;
	}

	if(bus == nil){
		/*
		 * if the PCI device is behind a bridge thats not described
		 * by the MP or ACPI tables then walk up the bus translating
		 * interrupt pin to parent bus.
		 */
		if(pci != nil && pci->parent != nil && pin > 0){
			pci = pci->parent;
			if(pci->ccrb == 6 && pci->ccru == 7){
				/* Cardbus bridge, use controllers interrupt pin */
				pin = pcicfgr8(pci, PciINTP);
			} else {
				/* PCI-PCI bridge */
				pin = ((dno+(pin-1))%4)+1;
			}
			bno = BUSBNO(pci->tbdf);
			dno = BUSDNO(pci->tbdf);
			goto Findbus;
		}
		print("mpintrassign: can't find bus type %d, number %d\n", type, bno);
		return -1;
	}

	/*
	 * For PCI devices the interrupt pin (INT[ABCD]) and device
	 * number are encoded into the entry irq field, so create something
	 * to match on.
	 */
	if(bus->type == BusPCI){
		if(pin > 0)
			irq = (dno<<2)|(pin-1);
		else
			irq = -1;
	} else
		irq = v->irq;

	/*
	 * Find a matching interrupt entry from the list of interrupts
	 * attached to this bus.
	 */
	for(aintr = bus->aintr; aintr != nil; aintr = aintr->next){
		if(aintr->intr->irq != irq)
			continue;

		/*
		 * Check if already enabled. Multifunction devices may share
		 * INT[A-D]# so, if already enabled, check the polarity matches
		 * and the trigger is level.
		 */
		ioapicrdtr(aintr->apic, aintr->intr->intin, &hi, &lo);
		if(lo & ApicIMASK){
			v->vno = allocvector();
			v->cpu = mpintrcpu();
			lo = mpintrinit(aintr->bus, aintr->intr, v->vno, v->irq);
			lo |= ApicPHYSICAL;			/* no-op */
			if(lo & ApicIMASK){
				print("mpintrassign: disabled irq %d, tbdf %uX, lo %8.8uX, hi %8.8uX\n",
					v->irq, v->tbdf, lo, hi);
				break;
			}
		} else {
			v->vno = lo & 0xFF;
			v->cpu = hi >> 24;
			lo &= ~(ApicRemoteIRR|ApicDELIVS);
			n = mpintrinit(aintr->bus, aintr->intr, v->vno, v->irq);
			n |= ApicPHYSICAL;			/* no-op */
			if(lo != n){
				print("mpintrassign: multiple botch irq %d, tbdf %uX, lo %8.8uX, n %8.8uX\n",
					v->irq, v->tbdf, lo, n);
				break;
			}
		}

		v->isr = lapicisr;
		v->eoi = lapiceoi;

		if((aintr->apic->flags & PcmpEN) && aintr->apic->type == PcmpIOAPIC){
			v->aux = aintr;
			v->enable = ioapicirqenable;
			v->disable = ioapicirqdisable;
		}

		return v->vno;
	}

	return -1;
}

enum {
	HTMSIMapping	= 0xA8,
	HTMSIFlags	= 0x02,
	HTMSIFlagsEn	= 0x01,
};

static int
htmsicapenable(Pcidev *p)
{
	int cap, flags;

	if((cap = pcihtcap(p, HTMSIMapping)) <= 0)
		return -1;
	flags = pcicfgr8(p, cap + HTMSIFlags);
	if((flags & HTMSIFlagsEn) == 0)
		pcicfgw8(p, cap + HTMSIFlags, flags | HTMSIFlagsEn);
	return 0;
}

static int
htmsienable(Pcidev *pdev)
{
	Pcidev *p;

	p = nil;
	while((p = pcimatch(p, 0x1022, 0)) != nil)
		if(p->did == 0x1103 || p->did == 0x1203)
			break;

	if(p == nil)
		return 0;	/* not hypertransport platform */

	p = nil;
	while((p = pcimatch(p, 0x10de, 0)) != nil){
		switch(p->did){
		case 0x02f0:	/* NVIDIA NFORCE C51 MEMC0 */
		case 0x02f1:	/* NVIDIA NFORCE C51 MEMC1 */
		case 0x02f2:	/* NVIDIA NFORCE C51 MEMC2 */
		case 0x02f3:	/* NVIDIA NFORCE C51 MEMC3 */
		case 0x02f4:	/* NVIDIA NFORCE C51 MEMC4 */
		case 0x02f5:	/* NVIDIA NFORCE C51 MEMC5 */
		case 0x02f6:	/* NVIDIA NFORCE C51 MEMC6 */
		case 0x02f7:	/* NVIDIA NFORCE C51 MEMC7 */
		case 0x0369:	/* NVIDIA NFORCE MCP55 MEMC */
			htmsicapenable(p);
			break;
		}
	}

	if(htmsicapenable(pdev) == 0)
		return 0;

	for(p = pdev->parent; p != nil; p = p->parent)
		if(htmsicapenable(p) == 0)
			return 0;

	return -1;
}

static int
msiirqenable(Vctl *v, int)
{
	Pcidev *pci = v->aux;
	return pcimsienable(pci, 0xFEE00000ULL | (v->cpu << 12), v->vno | (1<<14));
}

static int
msiirqdisable(Vctl *v, int)
{
	Pcidev *pci = v->aux;
	return pcimsidisable(pci);
}

static int
msiintrenable(Vctl *v)
{
	Pcidev *pci;
	int tbdf;

	if(getconf("*nomsi") != nil)
		return -1;

	tbdf = v->tbdf;
	if(tbdf == BUSUNKNOWN || BUSTYPE(tbdf) != BusPCI)
		return -1;
	pci = pcimatchtbdf(tbdf);
	if(pci == nil) {
		print("msiintrenable: could not find Pcidev for tbdf %uX\n", tbdf);
		return -1;
	}
	if(htmsienable(pci) < 0)
		return -1;
	if(pcimsidisable(pci) < 0)
		return -1;

	v->vno = allocvector();
	v->cpu = mpintrcpu();
	v->isr = lapicisr;
	v->eoi = lapiceoi;

	v->aux = pci;
	v->enable = msiirqenable;
	v->disable = msiirqdisable;

	return v->vno;
}

int
mpintrassign(Vctl* v)
{
	int irq, tbdf, vno;

	vno = msiintrenable(v);
	if(vno != -1)
		return vno;

	/*
	 * If the bus is known, try it.
	 * BUSUNKNOWN is given both by [E]ISA devices and by
	 * interrupts local to the processor (local APIC, coprocessor
	 * breakpoint and page-fault).
	 */
	tbdf = v->tbdf;
	if(tbdf != BUSUNKNOWN && (vno = mpintrassignx(v, tbdf)) != -1)
		return vno;

	irq = v->irq;
	if(irq >= IrqLINT0 && irq <= MaxIrqLAPIC){
		v->local = 1;
		if(irq != IrqSPURIOUS)
			v->isr = lapiceoi;
		return VectorPIC+irq;
	}
	if(irq < 0 || irq > MaxIrqPIC){
		print("mpintrassign: irq %d out of range\n", irq);
		return -1;
	}

	/*
	 * Either didn't find it or have to try the default buses
	 * (ISA and EISA). This hack is due to either over-zealousness 
	 * or laziness on the part of some manufacturers.
	 *
	 * The MP configuration table on some older systems
	 * (e.g. ASUS PCI/E-P54NP4) has an entry for the EISA bus
	 * but none for ISA. It also has the interrupt type and
	 * polarity set to 'default for this bus' which wouldn't
	 * be compatible with ISA.
	 */
	if(mpeisabus != -1){
		vno = mpintrassignx(v, MKBUS(BusEISA, 0, 0, 0));
		if(vno != -1)
			return vno;
	}
	if(mpisabus != -1){
		vno = mpintrassignx(v, MKBUS(BusISA, 0, 0, 0));
		if(vno != -1)
			return vno;
	}
	print("mpintrassign: out of choices eisa %d isa %d tbdf %uX irq %d\n",
		mpeisabus, mpisabus, v->tbdf, v->irq);
	return -1;
}

void
mpshutdown(void)
{
	/*
	 * Park application processors.
	 */
	if(m->machno != 0){
		splhi();
		arch->introff();
		for(;;) idle();
	}
	delay(1000);
	splhi();

	/*
	 * INIT all excluding self.
	 */
	lapicicrw(0, 0x000C0000|ApicINIT);

	pcireset();
}
