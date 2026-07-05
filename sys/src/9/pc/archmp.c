#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"

#include "mp.h"

/*
 * MultiProcessor Specification Version 1.[14].
 */
typedef struct {			/* floating pointer */
	uchar	signature[4];		/* "_MP_" */
	long	physaddr;		/* physical address of MP configuration table */
	uchar	length;			/* 1 */
	uchar	specrev;		/* [14] */
	uchar	checksum;		/* all bytes must add up to 0 */
	uchar	type;			/* MP system configuration type */
	uchar	imcrp;
	uchar	reserved[3];
} _MP_;

#define _MP_sz			(4+4+1+1+1+1+1+3)

typedef struct {			/* configuration table header */
	uchar	signature[4];		/* "PCMP" */
	ushort	length;			/* total table length */
	uchar	version;		/* [14] */
	uchar	checksum;		/* all bytes must add up to 0 */
	uchar	product[20];		/* product id */
	ulong	oemtable;		/* OEM table pointer */
	ushort	oemlength;		/* OEM table length */
	ushort	entry;			/* entry count */
	ulong	lapicbase;		/* address of local APIC */
	ushort	xlength;		/* extended table length */
	uchar	xchecksum;		/* extended table checksum */
	uchar	reserved;
} PCMP;

#define PCMPsz			(4+2+1+1+20+4+2+2+4+2+1+1)

typedef struct {			/* processor table entry */
	uchar	type;			/* entry type (0) */
	uchar	apicno;			/* local APIC id */
	uchar	version;		/* local APIC verison */
	uchar	flags;			/* CPU flags */
	uchar	signature[4];		/* CPU signature */
	ulong	feature;		/* feature flags from CPUID instruction */
	uchar	reserved[8];
} PCMPprocessor;

#define PCMPprocessorsz		(1+1+1+1+4+4+8)

typedef struct {			/* bus table entry */
	uchar	type;			/* entry type (1) */
	uchar	busno;			/* bus id */
	char	string[6];		/* bus type string */
} PCMPbus;

#define PCMPbussz		(1+1+6)

typedef struct {			/* I/O APIC table entry */
	uchar	type;			/* entry type (2) */
	uchar	apicno;			/* I/O APIC id */
	uchar	version;		/* I/O APIC version */
	uchar	flags;			/* I/O APIC flags */
	ulong	addr;			/* I/O APIC address */
} PCMPioapic;

#define PCMPioapicsz		(1+1+1+1+4)

typedef struct {			/* interrupt table entry */
	uchar	type;			/* entry type ([34]) */
	uchar	intr;			/* interrupt type */
	ushort	flags;			/* interrupt flag */
	uchar	busno;			/* source bus id */
	uchar	irq;			/* source bus irq */
	uchar	apicno;			/* destination APIC id */
	uchar	intin;			/* destination APIC [L]INTIN# */
} PCMPintr;

#define PCMPintrsz		(1+1+2+1+1+1+1)

typedef struct {			/* system address space mapping entry */
	uchar	type;			/* entry type (128) */
	uchar	length;			/* of this entry (20) */
	uchar	busno;			/* bus id */
	uchar	addrtype;
	ulong	addrbase[2];
	ulong	addrlength[2];
} PCMPsasm;

#define PCMPsasmsz		(1+1+1+1+8+8)

typedef struct {			/* bus hierarchy descriptor entry */
	uchar	type;			/* entry type (129) */
	uchar	length;			/* of this entry (8) */
	uchar	busno;			/* bus id */
	uchar	info;			/* bus info */
	uchar	parent;			/* parent bus */
	uchar	reserved[3];
} PCMPhierarchy;

#define PCMPhirarchysz		(1+1+1+1+1+3)

typedef struct {			/* compatibility bus address space modifier entry */
	uchar	type;			/* entry type (130) */
	uchar	length;			/* of this entry (8) */
	uchar	busno;			/* bus id */
	uchar	modifier;		/* address modifier */
	ulong	range;			/* predefined range list */
} PCMPcbasm;

#define PCMPcbasmsz		(1+1+1+1+4)

static PCMP *pcmp;

static char* buses[] = {
	"CBUSI ",
	"CBUSII",
	"EISA  ",
	"FUTURE",
	"INTERN",
	"ISA   ",
	"MBI   ",
	"MBII  ",
	"MCA   ",
	"MPI   ",
	"MPSA  ",
	"NUBUS ",
	"PCI   ",
	"PCMCIA",
	"TC    ",
	"VL    ",
	"VME   ",
	"XPRESS",
	0,
};

static Apic*
mkprocessor(PCMPprocessor* p)
{
	int apicno;
	Apic *apic;

	apicno = p->apicno;
	if(!(p->flags & PcmpEN) || apicno > MaxAPICNO || mpgetapic(mplapic, apicno) != nil)
		return nil;

	if((apic = xalloc(sizeof(Apic))) == nil)
		panic("mkprocessor: no memory for Apic");
	apic->type = PcmpPROCESSOR;
	apic->apicno = apicno;
	apic->x2apic = -1;
	apic->flags = p->flags;

	*mplapicp = apic, mplapicp = &apic->next;

	return apic;
}

static Bus*
mkbus(PCMPbus* p)
{
	int i;

	for(i = 0; buses[i] != nil; i++){
		if(strncmp(buses[i], p->string, sizeof(p->string)) == 0)
			return mpgetbus(i, p->busno);
	}
	return nil;
}

static Apic*
mkioapic(PCMPioapic* p)
{
	int apicno;
	Apic *apic;

	apicno = p->apicno;
	if(!(p->flags & PcmpEN) || apicno > MaxAPICNO || mpgetapic(mpioapic, apicno) != nil)
		return nil;
	/*
	 * Map the I/O APIC.
	 */
	if((apic = xalloc(sizeof(Apic))) == nil)
		panic("mkioapic: no memory for Apic");
	apic->type = PcmpIOAPIC;
	apic->apicno = apicno;
	apic->paddr = p->addr;
	apic->flags = p->flags;
	*mpioapicp = apic, mpioapicp = &apic->next;

	return apic;
}

static Aintr*
mkiointr(PCMPintr* p)
{
	Bus *bus;
	Apic *apic;
	Aintr *aintr;

	/*
	 * According to the MultiProcessor Specification, a destination
	 * I/O APIC of 0xFF means the signal is routed to all I/O APICs.
	 * It's unclear how that can possibly be correct so treat it as
	 * an error for now.
	 */
	if(p->apicno > MaxAPICNO)
		return nil;
	if((apic = mpgetapic(mpioapic, p->apicno)) == nil)
		return nil;
	if((bus = mpgetbus(-1, p->busno)) == nil)
		return nil;

	if((aintr = xalloc(sizeof(Aintr))) == nil)
		panic("mkiointr: no memory for Aintr");

	aintr->type = p->intr;
	aintr->flags = p->flags;
	aintr->irq = p->irq;
	aintr->intin = p->intin;

	if(0)
		print("mkiointr: type %d flags %#o "
			"bus %d irq %d apicno %d intin %d\n",
			aintr->type, aintr->flags,
			bus->busno, aintr->irq, apic->apicno, aintr->intin);
	/*
	 * Hack for Intel SR1520ML motherboard, which BIOS describes
	 * the i82575 dual ethernet controllers incorrectly.
	 */
	if(memcmp(pcmp->product, "INTEL   X38MLST     ", 20) == 0){
		if(p->busno == 1 && p->intin == 16 && p->irq == 1){
			print("mkiointr: %20.20s bus %d intin %d irq %d\n",
				(char*)pcmp->product,
				bus->busno, aintr->intin,
				aintr->irq);
			aintr->intin = 17;
		}
	}
	aintr->apic = apic;
	aintr->anext = apic->aintr;
	apic->aintr = aintr;

	aintr->next = bus->aintr;
	aintr->bus = bus;
	bus->aintr = aintr;

	return aintr;
}

static void
mklintr(PCMPintr* p)
{
	Bus *bus;
	Apic *apic;
	Aintr *aintr;

	if(p->intin > 1)
		return;

	/*
	 * The offsets of vectors for LINT[01] are known to be
	 * 0 and 1 from the local APIC vector space at VectorLAPIC.
	 */
	if((bus = mpgetbus(-1, p->busno)) == nil)
		return;

	for(apic = mplapic; apic != nil; apic = apic->next) {
		if((apic->flags & PcmpEN) == 0)
			continue;
		if(p->apicno != 0xFF && p->apicno != apic->apicno)
			continue;

		if(apic->lint[p->intin] != nil)
			continue;

		if((aintr = xalloc(sizeof(Aintr))) == nil)
			panic("mkiointr: no memory for Aintr");

		aintr->type = p->intr;
		aintr->flags = p->flags;
		aintr->irq = p->irq;
		aintr->bus = bus;
		if(aintr->type == PcmpINT) {
			aintr->next = bus->aintr;
			bus->aintr = aintr;
		}
		aintr->intin = p->intin;
		aintr->apic = apic;
		aintr->anext = apic->aintr;
		apic->aintr = aintr;
		apic->lint[aintr->intin] = aintr;
	}
}

static void
dumpmp(uchar *p, uchar *e)
{
	int i;

	for(i = 0; p < e; p++) {
		if((i % 16) == 0) print("*mp%d=", i/16);
		print("%.2x ", *p);
		if((++i % 16) == 0) print("\n");
	}
	if((i % 16) != 0) print("\n");
}

static void
mpoverride(uchar** newp, uchar** e)
{
	int size, i, j;
	char buf[20];
	uchar* p;
	char* s;
	
	size = strtol(getconf("*mp"), 0, 0);
	if(size <= 0) panic("mpoverride: invalid size in *mp");
	*newp = p = xalloc(size);
	if(p == nil) panic("mpoverride: can't allocate memory");
	*e = p + size;
	for(i = 0; ; i++){
		snprint(buf, sizeof buf, "*mp%d", i);
		s = getconf(buf);
		if(s == nil) break;
		while(*s){
			j = strtol(s, &s, 16);
			if(*s && *s != ' ' || j < 0 || j > 0xff) panic("mpoverride: invalid entry in %s", buf);
			if(p >= *e) panic("mpoverride: overflow in %s", buf);
			*p++ = j;
		}
	}
	if(p != *e) panic("mpoverride: size doesn't match");
}

static void
pcmpinit(void)
{
	uchar *p, *e;
	Apic *apic;

	p = ((uchar*)pcmp)+PCMPsz;
	e = ((uchar*)pcmp)+pcmp->length;
	if(getconf("*dumpmp") != nil)
		dumpmp(p, e);
	if(getconf("*mp") != nil)
		mpoverride(&p, &e);

	/*
	 * Run through the table saving information needed for starting
	 * application processors and initialising any I/O APICs. The table
	 * is guaranteed to be in order such that only one pass is necessary.
	 */
	while(p < e) switch(*p){
	default:
		print("pcmpinit: unknown PCMP type 0x%uX (e-p 0x%zuX)\n",
			*p, e-p);
		while(p < e){
			print("%uX ", *p);
			p++;
		}
		break;

	case PcmpPROCESSOR:
		if(apic = mkprocessor((PCMPprocessor*)p))
			apic->paddr = pcmp->lapicbase;
		p += PCMPprocessorsz;
		continue;

	case PcmpBUS:
		mkbus((PCMPbus*)p);
		p += PCMPbussz;
		continue;

	case PcmpIOAPIC:
		if(apic = mkioapic((PCMPioapic*)p))
			ioapicinit(apic);
		p += PCMPioapicsz;
		continue;

	case PcmpIOINTR:
		mkiointr((PCMPintr*)p);
		p += PCMPintrsz;
		continue;

	case PcmpLINTR:
		mklintr((PCMPintr*)p);
		p += PCMPintrsz;
		continue;
	}

	/*
	 * Ininitalize local APIC and start application processors.
	 */
	mpinit();
}

static void
mpreset(void)
{
	/* stop application processors */
	mpshutdown();

	/* do generic reset */
	archreset();
}

static int identify(void);
extern int i8259irqno(int, int);

PCArch archmp = {
.id=		"_MP_",	
.ident=		identify,
.reset=		mpreset,
.intrinit=	pcmpinit,
.intrassign=	mpintrassign,
.intrspurious=	mpintrspurious,
.intrirqno=	i8259irqno,
.intron=	lapicintron,
.introff=	lapicintroff,
.fastclock=	i8253read,
.timerset=	lapictimerset,
};

static int
identify(void)
{
	char *cp;
	_MP_ *_mp_;
	ulong pa, len;

	if((cp = getconf("*nomp")) != nil && strcmp(cp, "0") != 0)
		return 1;

	/*
	 * Search for an MP configuration table. For now,
	 * don't accept the default configurations (physaddr == 0).
	 * Check for correct signature, calculate the checksum and,
	 * if correct, check the version.
	 * To do: check extended table checksum.
	 */
	if((_mp_ = sigsearch("_MP_", _MP_sz)) == nil || _mp_->physaddr == 0)
		return 1;

	len = PCMPsz;
	pa = _mp_->physaddr;
	if(pa + len-1 < pa)
		return 1;

	memreserve(pa, len);
	if((pcmp = vmap(pa, len)) == nil)
		return 1;
	if(pcmp->length < PCMPsz
	|| pa + pcmp->length-1 < pa
	|| memcmp(pcmp, "PCMP", 4) != 0
	|| (pcmp->version != 1 && pcmp->version != 4)){
Bad:
		vunmap(pcmp, len);
		pcmp = nil;
		return 1;
	}
	len = pcmp->length;
	memreserve(pa, len);
	vunmap(pcmp, PCMPsz);
	if((pcmp = vmap(pa, len)) == nil)
		return 1;

	if(checksum(pcmp, len) != 0)
		goto Bad;

	if(m->havetsc && getconf("*notsc") == nil)
		archmp.fastclock = tscticks;

	return 0;
}
