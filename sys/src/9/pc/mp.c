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
extern int i8259isr(int);

int mpisabus = -1;
int mpeisabus = -1;
Bus* mpbus, **mpbusp = &mpbus;
Apic *mpioapic, **mpioapicp = &mpioapic;
Apic *mplapic, **mplapicp = &mplapic;

static int nmplapic;

Bus*
mpgetbus(int type, int busno)
{
	Bus *bus;

	for(bus = mpbus; bus != nil; bus = bus->next)
		if((type == -1 || bus->type == type) && bus->busno == busno)
			return bus;

	if(type == -1)
		return nil;

	if((bus = xalloc(sizeof(Bus))) == nil)
		panic("addirq: no memory for Bus");

	bus->busno = busno;
	bus->type = type;
	bus->po = PcmpHIGH;
	bus->el = PcmpEDGE;
	if(type == BusISA){
		if(mpisabus == -1)
			mpisabus = busno;
	} else if(type == BusEISA){
		if(mpeisabus == -1)
			mpeisabus = busno;
	} else if(type == BusPCI) {
		bus->po = PcmpLOW;
		bus->el = PcmpLEVEL;
	}

	*mpbusp = bus, mpbusp = &bus->next;

	return bus;
}

Apic*
mpgetapic(Apic *a, int apicno)
{
	while(a != nil){	
		if(a->apicno == apicno)
			return a;
		a = a->next;
	}
	return nil;
}

/*
 * Parse an I/O or Local APIC interrupt and
 * return the encoded vector.
 */
int
mpintrinit(Aintr *intr, int vno)
{
	int el, po, v;

	if(intr == nil)
		return ApicIMASK;

	po = intr->flags & PcmpPOMASK;
	el = intr->flags & PcmpELMASK;

	v = vno;
	switch(intr->type){
	default:				/* PcmpINT */
		v |= ApicFIXED;			/* no-op */
		break;

	case PcmpNMI:
		v = ApicNMI;			/* vno ignored */
		po = PcmpHIGH;
		el = PcmpEDGE;
		break;

	case PcmpSMI:
		v = ApicSMI;			/* vno ignored */
		break;

	case PcmpExtINT:
		/* ExtINT should be masked off */
		return ApicIMASK;
	}

	if(intr->bus == nil && (!po || !el)){
		print("mpintrinit: vector %d: no bus for default trigger/polarity\n", vno);
		return ApicIMASK;
	}

	if(!po)
		po = intr->bus->po;
	if(po == PcmpLOW)
		v |= ApicLOW;
	else if(po != PcmpHIGH){
		print("mpintrinit: vector %d: bad polarity 0x%uX\n", vno, po);
		return ApicIMASK;
	}

	if(!el)
		el = intr->bus->el;
	if(el == PcmpLEVEL)
		v |= ApicLEVEL;
	else if(el != PcmpEDGE){
		print("mpintrinit: vector %d: bad trigger 0x%uX\n", vno, el);
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

static void
printapic(Apic *apic)
{
	Aintr *ai;

	if(apic->type == PcmpPROCESSOR){
		print("LAPIC%d: paddr=%llux x2apic=%x flags=%x\n",
			apic->apicno, apic->paddr, apic->x2apic, apic->flags);
	} else {
		print("IOAPIC%d: paddr=%llux addr=%#p flags=%x gsibase=%d mre=%d\n",
			apic->apicno, apic->paddr, apic->addr, apic->flags, apic->gsibase, apic->mre);
	}
	for(ai = apic->aintr; ai != nil; ai = ai->anext) {
		static char *itype[] = {
			"INT", "NMI", "SMI", "ExtINT",
		};
		print("\t%2d: %5s irq=%-4d flags=%x", ai->intin, itype[ai->type&3], ai->irq, ai->flags);
		if(ai->bus == nil)
			print("\n");
		else if(ai->bus->type == BusPCI)
			print(" bus: %T (INT%c)\n", MKBUS(BusPCI, ai->bus->busno, ai->irq>>2, 0),
				"ABCD"[ai->irq&3]);
		else
			print(" bus: %T flags=%x\n", MKBUS(ai->bus->type, ai->bus->busno, 0, 0),
				ai->bus->po | ai->bus->el);
	}
}

void
mpinit(void)
{
	Apic *apic, *ioapic;
	char *cp;
	int ncpu;

	i8259init();
	syncclock();

	if(getconf("*apicdebug")){
		for(apic = mplapic; apic != nil; apic = apic->next)
			printapic(apic);
		for(apic = mpioapic; apic != nil; apic = apic->next)
			printapic(apic);
	}

	nmplapic = 0;
	for(apic = mplapic; apic != nil; apic = apic->next)
		nmplapic++;

	for(apic = mplapic; apic != nil; apic = apic->next)
		if(apic->flags & PcmpBP)
			break;
	if(apic == nil)
		panic("mpinit: no bootstrap processor (%d processors found)", nmplapic);

	assert(m->machno < conf.nmach);
	apic->machno = m->machno;
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
	 * Enable I/O APIC NMI's and route to cpu0
	 */
	if(apic->apicno <= MaxAPICNO)
	for(ioapic = mpioapic; ioapic != nil; ioapic = ioapic->next){
		Aintr *ai;
		for(ai = ioapic->aintr; ai != nil; ai = ai->anext){
			if(ai->type == PcmpNMI)
				ioapicrdtw(ioapic, ai->intin,
					(apic->apicno & 0xFF) << 24,
					mpintrinit(ai, VectorNMI) | ApicPHYSICAL);
		}
	}

	ncpu = MAXMACH;
	if(cp = getconf("*ncpu")){
		ncpu = strtol(cp, 0, 0);
		if(ncpu < 1)
			ncpu = 1;
		else if(ncpu > MAXMACH)
			ncpu = MAXMACH;
	}

	/*
	 * Initialise the application processors.
	 */
	memmove((void*)APBOOTSTRAP, apbootstrap, sizeof(apbootstrap));
	for(apic = mplapic; apic != nil; apic = apic->next){
		if((apic->flags & PcmpEN) != 0 && !apic->online){
			if(conf.nmach >= ncpu)
				break;
			apic->machno = conf.nmach++;
			mpstartap(apic);
		}
	}

	/*
	 *  we don't really know the number of processors till
	 *  here.
	 *
	 *  set conf.copymode here if nmach > 1.
	 */
	if(m->cpuidfamily == 3 || conf.nmach > 1)
		conf.copymode = 1;
}

static int
allocdest(int *pmachno)
{
	static Apic *apic;
	static Lock l;
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
	lock(&l);
	for(i=1;;i++){
		if(apic == nil)
			apic = mplapic;
		if(apic->online && apic->apicno <= MaxAPICNO)
			break;
		if(i >= nmplapic)
			return -1;
		apic = apic->next;
	}
	*pmachno = apic->machno;
	i = apic->apicno;
	apic = apic->next;
	unlock(&l);

	return i;
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
	hi = (v->dest & 0xFF) << 24;
	lo = mpintrinit(aintr, v->vno);
	lo |= ApicPHYSICAL;			/* no-op */
 	ioapicrdtw(aintr->apic, aintr->intin, hi, lo);
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
 	ioapicrdtw(aintr->apic, aintr->intin, hi, lo);
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
	} else if(type == BusISA && mpisabus != -1){
		bno = mpisabus;
	} else if(type == BusEISA && mpeisabus != -1){
		bno = mpeisabus;
	}
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
		if(aintr->type != PcmpINT || aintr->irq != irq || irq == -1)
			continue;

		if(aintr->apic->type == PcmpPROCESSOR){
			if(!aintr->apic->online)
				continue;

			/* LINT[01] already enabled by lapicinit() */
			v->dest = aintr->apic->apicno;
			v->machno = aintr->apic->machno;
			v->vno = VectorLAPIC+aintr->intin;
			v->eoi = lapiceoi;
			v->aux = aintr;
			return v->vno;
		}

		if(aintr->apic->type != PcmpIOAPIC)
			continue;

		/*
		 * Check if already enabled. Multifunction devices may share
		 * INT[A-D]# so, if already enabled, check the polarity matches
		 * and the trigger is level.
		 */
		ioapicrdtr(aintr->apic, aintr->intin, &hi, &lo);
		if(lo & ApicIMASK){
			v->dest = allocdest(&v->machno);
			if(v->dest < 0){
				print("mpintrassign: no destination irq %d, tbdf %T, lo %8.8uX, hi %8.8uX\n",
					v->irq, v->tbdf, lo, hi);
				break;
			}
			v->vno = allocvector();
			lo = mpintrinit(aintr, v->vno);
			lo |= ApicPHYSICAL;			/* no-op */
			if(lo & ApicIMASK){
				print("mpintrassign: disabled irq %d, tbdf %T, lo %8.8uX, hi %8.8uX\n",
					v->irq, v->tbdf, lo, hi);
				break;
			}
		} else {
			v->dest = (hi >> 24) & 0xFF;
			v->vno = lo & 0xFF;
			lo &= ~(ApicRemoteIRR|ApicDELIVS);
			n = mpintrinit(aintr, v->vno);
			n |= ApicPHYSICAL;			/* no-op */
			if(lo != n){
				print("mpintrassign: multiple botch irq %d, tbdf %T, lo %8.8uX, n %8.8uX\n",
					v->irq, v->tbdf, lo, n);
				break;
			}
		}

		v->eoi = lapiceoi;

		v->aux = aintr;
		v->enable = ioapicirqenable;
		v->disable = ioapicirqdisable;

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
	return pcimsienable(pci, 0xFEE00000ULL | (v->dest & 0xFF) << 12, v->vno | (1<<14));
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
		print("msiintrenable: could not find Pcidev for tbdf %T\n", tbdf);
		return -1;
	}
	if(htmsienable(pci) < 0)
		return -1;
	if(pcimsidisable(pci) < 0)
		return -1;

	v->dest = allocdest(&v->machno);
	if(v->dest < 0){
		print("msiintrenable: no destination for tbdf %T\n", tbdf);
		return -1;
	}

	v->vno = allocvector();
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
	print("mpintrassign: out of choices eisa %d isa %d tbdf %T irq %d\n",
		mpeisabus, mpisabus, v->tbdf, v->irq);
	return -1;
}

int
mpintrspurious(int vno)
{
	if(vno >= VectorLAPIC)
		return lapiceoi(vno);
	return i8259isr(vno);
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
	lapicicr(0x000C0000|ApicINIT);

	pcireset();
}
