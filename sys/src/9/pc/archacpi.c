#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "mp.h"

#include <aml.h>

typedef struct Rsd Rsd;
typedef struct Tbl Tbl;

struct Rsd {
	uchar	sig[8];
	uchar	csum;
	uchar	oemid[6];
	uchar	rev;
	uchar	raddr[4];
	uchar	len[4];
	uchar	xaddr[8];
	uchar	xcsum;
	uchar	reserved[3];
};

struct Tbl {
	uchar	sig[4];
	uchar	len[4];
	uchar	rev;
	uchar	csum;
	uchar	oemid[6];
	uchar	oemtid[8];
	uchar	oemrev[4];
	uchar	cid[4];
	uchar	crev[4];
	uchar	data[];
};

enum {
	Tblsz	= 4+4+1+1+6+8+4+4+4,
};

static Rsd *rsd;

/* physical addresses visited by maptable() */
static int ntblpa;
static uintptr tblpa[64];

/* successfully mapped tables */
static int ntblmap;
static Tbl *tblmap[64];

void*
amlalloc(int n){
	void *p;

	if((p = malloc(n)) == nil)
		panic("amlalloc: no memory");
	memset(p, 0, n);
	return p;
}

void
amlfree(void *p){
	free(p);
}

static ushort
get16(uchar *p){
	return p[1]<<8 | p[0];
}

static uint
get32(uchar *p){
	return p[3]<<24 | p[2]<<16 | p[1]<<8 | p[0];
}

static uvlong
get64(uchar *p){
	uvlong u;

	u = get32(p+4);
	return u<<32 | get32(p);
}

static uint
tbldlen(Tbl *t){
	return get32(t->len) - Tblsz;
}

static void
maptable(uvlong xpa)
{
	uchar *p, *e;
	uintptr pa;
	u32int l;
	Tbl *t;
	int i;

	pa = xpa;
	if((uvlong)pa != xpa || pa == 0)
		return;
	if(ntblpa >= nelem(tblpa) || ntblmap >= nelem(tblmap))
		return;

	for(i=0; i<ntblpa; i++){
		if(pa == tblpa[i])
			return;
	}
	tblpa[ntblpa++] = pa;

	if((t = vmap(pa, 8)) == nil)
		return;
	l = get32(t->len);
	if(l < Tblsz){
		vunmap(t, 8);
		return;
	}
	vunmap(t, 8);
	if((t = vmap(pa, l)) == nil)
		return;
	if(checksum(t, l)){
		vunmap(t, l);
		return;
	}
	tblmap[ntblmap++] = t;

	p = (uchar*)t;
	e = p + l;
	if(memcmp("RSDT", t->sig, 4) == 0){
		for(p = t->data; p+3 < e; p += 4)
			maptable(get32(p));
		return;
	}
	if(memcmp("XSDT", t->sig, 4) == 0){
		for(p = t->data; p+7 < e; p += 8)
			maptable(get64(p));
		return;
	}
	if(memcmp("FACP", t->sig, 4) == 0){
		if(l < 44)
			return;
		maptable(get32(p + 40));
		if(l < 148)
			return;
		maptable(get64(p + 140));
		return;
	}
}

static void
maptables(void)
{
	if(rsd == nil || ntblmap > 0 || ntblpa > 0)
		return;
	if(!checksum(rsd, 20))
		maptable(get32(rsd->raddr));
	if(rsd->rev >= 2)
		if(!checksum(rsd, 36))
			maptable(get64(rsd->xaddr));
}

static Apic*
findapic(int gsi, int *pintin)
{
	Apic *a;
	int i;

	for(i=0; i<=MaxAPICNO; i++){
		if((a = mpioapic[i]) == nil)
			continue;
		if((a->flags & PcmpEN) == 0)
			continue;
		if(gsi >= a->gsibase && gsi <= a->gsibase+a->mre){
			if(pintin)
				*pintin = gsi - a->gsibase;
			return a;
		}
	}
	print("findapic: no ioapic found for gsi %d\n", gsi);
	return nil;
}

static void
addirq(int gsi, int type, int busno, int irq, int flags)
{
	Apic *a;
	Bus *bus;
	Aintr *ai;
	PCMPintr *pi;
	int intin;

	if((a = findapic(gsi, &intin)) == nil)
		return;

	for(bus = mpbus; bus; bus = bus->next)
		if(bus->type == type && bus->busno == busno)
			goto Foundbus;

	if((bus = xalloc(sizeof(Bus))) == nil)
		panic("addirq: no memory for Bus");
	bus->busno = busno;
	bus->type = type;
	if(type == BusISA){
		bus->po = PcmpHIGH;
		bus->el = PcmpEDGE;
		if(mpisabus == -1)
			mpisabus = busno;
	} else {
		bus->po = PcmpLOW;
		bus->el = PcmpLEVEL;
	}
	if(mpbus)
		mpbuslast->next = bus;
	else
		mpbus = bus;
	mpbuslast = bus;

Foundbus:
	for(ai = bus->aintr; ai; ai = ai->next)
		if(ai->intr->irq == irq)
			return;

	if((pi = xalloc(sizeof(PCMPintr))) == nil)
		panic("addirq: no memory for PCMPintr");
	pi->type = PcmpIOINTR;
	pi->intr = PcmpINT;
	pi->flags = flags & (PcmpPOMASK|PcmpELMASK);
	pi->busno = busno;
	pi->irq = irq;
	pi->apicno = a->apicno;
	pi->intin = intin;

	if((ai = xalloc(sizeof(Aintr))) == nil)
		panic("addirq: no memory for Aintr");
	ai->intr = pi;
	ai->apic = a;
	ai->next = bus->aintr;
	bus->aintr = ai;
}

static char*
eisaid(void *v)
{
	static char id[8];
	ulong b, l;
	int i;

	if(amltag(v) == 's')
		return v;
	b = amlint(v);
	for(l = 0, i=24; i>=0; i -= 8, b >>= 8)
		l |= (b & 0xFF) << i;
	id[7] = 0;
	for(i=6; i>=3; i--, l >>= 4)
		id[i] = "0123456789ABCDEF"[l & 0xF];
	for(i=2; i>=0; i--, l >>= 5)
		id[i] = '@' + (l & 0x1F);
	return id;
}

static int
pcibusno(void *dot)
{
	int bno, adr, tbdf;
	Pcidev *pdev;
	void *p, *x;
	char *id;

	id = nil;
	if(x = amlwalk(dot, "^_HID")){
		p = nil;
		if(amleval(x, "", &p) == 0)
			id = eisaid(p);
	}
	if((x = amlwalk(dot, "^_BBN")) == nil)
		if((x = amlwalk(dot, "^_ADR")) == nil)
			return -1;
	p = nil;
	if(amleval(x, "", &p) < 0)
		return -1;
	adr = amlint(p);
	/* if root bridge, then we are done here */
	if(id && (strcmp(id, "PNP0A03")==0 || strcmp(id, "PNP0A08")==0))
		return adr;
	x = amlwalk(dot, "^");
	if(x == nil || x == dot)
		return -1;
	if((bno = pcibusno(x)) < 0)
		return -1;
	tbdf = MKBUS(BusPCI, bno, adr>>16, adr&0xFFFF);
	pdev = pcimatchtbdf(tbdf);
	if(pdev == nil || pdev->bridge == nil)
		return -1;
	return BUSBNO(pdev->bridge->tbdf);
}

static int
enumprt(void *dot, void *)
{
	void *p, **a, **b;
	int bno, dno, pin;
	int n, i;

	bno = pcibusno(dot);
	if(bno < 0)
		return 1;

	/* evalulate _PRT method */
	p = nil;
	if(amleval(dot, "", &p) < 0)
		return 1;
	if(amltag(p) != 'p')
		return 1;

	n = amllen(p);
	a = amlval(p);
	for(i=0; i<n; i++){
		if(amltag(a[i]) != 'p')
			continue;
		if(amllen(a[i]) != 4)
			continue;
		b = amlval(a[i]);
		dno = amlint(b[0])>>16;
		pin = amlint(b[1]);
		if(amltag(b[2]) == 'N' || amlint(b[2])){
			print("enumprt: interrupt link not handled %V\n", b[2]);
			continue;
		}
		addirq(amlint(b[3]), BusPCI, bno, (dno<<2)|pin, 0);
	}
	return 1;
}

static void
acpiinit(void)
{
	Tbl *t;
	Apic *a;
	void *va;
	uchar *p, *e;
	ulong lapicbase;
	int machno, i, c;

	maptables();

	amlinit();

	/* load DSDT */
	for(i=0; i<ntblmap; i++){
		t = tblmap[i];
		if(memcmp(t->sig, "DSDT", 4) == 0){
			amlload(t->data, tbldlen(t));
			break;
		}
	}

	/* load SSDT, there can be multiple tables */
	for(i=0; i<ntblmap; i++){
		t = tblmap[i];
		if(memcmp(t->sig, "SSDT", 4) == 0)
			amlload(t->data, tbldlen(t));
	}

	/* set APIC mode */
	amleval(amlwalk(amlroot, "_PIC"), "i", 1, nil);

	for(i=0; i<ntblmap; i++){
		t = tblmap[i];
		if(memcmp(t->sig, "APIC", 4) == 0)
			goto Foundapic;
	}
	panic("acpiinit: no MADT (APIC) table");
	return;

Foundapic:
	p = t->data;
	e = p + tbldlen(t);
	lapicbase = get32(p); p += 8;
	va = vmap(lapicbase, 1024);
	print("LAPIC: %.8lux %.8lux\n", lapicbase, (ulong)va);
	if(va == nil)
		panic("acpiinit: cannot map lapic %.8lux", lapicbase);

	machno = 0;
	for(; p < e; p += c){
		c = p[1];
		if(c < 2 || (p+c) > e)
			break;
		switch(*p){
		case 0x00:	/* Processor Local APIC */
			if(p[3] > MaxAPICNO)
				break;
			if((a = xalloc(sizeof(Apic))) == nil)
				panic("acpiinit: no memory for Apic");
			a->type = PcmpPROCESSOR;
			a->apicno = p[3];
			a->paddr = lapicbase;
			a->addr = va;
			a->lintr[0] = ApicIMASK;
			a->lintr[1] = ApicIMASK;
			a->flags = p[4] & PcmpEN;
			if(a->flags & PcmpEN){
				a->machno = machno++;

				/*
				 * platform firmware should list the boot processor
				 * as the first processor entry in the MADT
				 */
				if(a->machno == 0)
					a->flags |= PcmpBP;
			}
			mpapic[a->apicno] = a;
			break;
		case 0x01:	/* I/O APIC */
			if(p[2] > MaxAPICNO)
				break;
			if((a = xalloc(sizeof(Apic))) == nil)
				panic("acpiinit: no memory for io Apic");
			a->type = PcmpIOAPIC;
			a->apicno = p[2];
			a->paddr = get32(p+4);
			if((a->addr = vmap(a->paddr, 1024)) == nil)
				panic("acpiinit: cannot map ioapic %.8lux", a->paddr);
			a->gsibase = get32(p+8);
			a->flags = PcmpEN;
			mpioapic[a->apicno] = a;
			ioapicinit(a, a->apicno);
			break;
		case 0x02:	/* Interrupt Source Override */
			addirq(get32(p+4), BusISA, 0, p[3], get16(p+8));
			break;
		case 0x03:	/* NMI Source */
		case 0x04:	/* Local APIC NMI */
		case 0x05:	/* Local APIC Address Override */
		case 0x06:	/* I/O SAPIC */
		case 0x07:	/* Local SAPIC */
		case 0x08:	/* Platform Interrupt Sources */
		case 0x09:	/* Processor Local x2APIC */
		case 0x0A:	/* x2APIC NMI */
		case 0x0B:	/* GIC */
		case 0x0C:	/* GICD */
			break;
		}
	}

	/* look for PCI interrupt mappings */
	amlenum(amlroot, "_PRT", enumprt, nil);

	/* add identity mapped legacy isa interrupts */
	for(i=0; i<16; i++)
		addirq(i, BusISA, 0, i, 0);

	/* free the AML interpreter */
	amlexit();

	/*
	 * Ininitalize local APIC and start application processors.
	 */
	mpinit();
}

static int identify(void);

PCArch archacpi = {
.id=		"ACPI",	
.ident=		identify,
.reset=		mpshutdown,
.intrinit=	acpiinit,
.intrenable=	mpintrenable,
.intron=	lapicintron,
.introff=	lapicintroff,
.fastclock=	i8253read,
.timerset=	lapictimerset,
};

static long
readtbls(Chan*, void *v, long n, vlong o)
{
	int i, l, m;
	uchar *p;
	Tbl *t;

	maptables();

	p = v;
	for(i=0; n > 0 && i < ntblmap; i++){
		t = tblmap[i];
		l = get32(t->len);
		if(o >= l){
			o -= l;
			continue;
		}
		m = l - o;
		if(m > n)
			m = n;
		memmove(p, (uchar*)t + o, m);
		p += m;
		n -= m;
		o = 0;
	}
	return p - (uchar*)v;
}

static int
identify(void)
{
	char *cp;

	if((cp = getconf("*acpi")) == nil)
		return 1;
	if((rsd = sigsearch("RSD PTR ")) == nil)
		return 1;
	if(checksum(rsd, 20) && checksum(rsd, 36))
		return 1;
	addarchfile("acpitbls", 0444, readtbls, nil);
	if(strcmp(cp, "0") == 0)
		return 1;
	if((cp = getconf("*nomp")) != nil && strcmp(cp, "0") != 0)
		return 1;
	if(m->havetsc && getconf("*notsc") == nil)
		archacpi.fastclock = tscticks;
	return 0;
}
