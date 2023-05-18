#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "../port/error.h"

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
static uvlong tblpa[64];

/* successfully mapped tables */
static int ntblmap;
static Tbl *tblmap[64];

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

static long
memcheck(uintptr pa, long len)
{
	int i;
	uintptr pe;
	Confmem *cm;

	if(len <= 0)
		return len;
	pe = pa + len-1;
	if(pe < pa){
		len = -pa;
		pe = pa + len-1;
	}
	if(pa < PADDR(CPU0END))
		return 0;
	if(pe >= PADDR(KTZERO) && pa < PADDR(end))
		return PADDR(KTZERO) - pa;
	for(i=0; i<nelem(conf.mem); i++){
		cm = &conf.mem[i];
		if(cm->npage == 0)
			continue;
		if(pe >= cm->base && pa <= cm->base + cm->npage*BY2PG - 1)
			return cm->base - pa;
	}
	return len;
}

static void
maptable(uvlong pa)
{
	uchar *p, *e;
	u32int l;
	Tbl *t;
	int i;

	if(-pa < 8)
		return;

	if(ntblpa >= nelem(tblpa) || ntblmap >= nelem(tblmap))
		return;

	for(i=0; i<ntblpa; i++){
		if(pa == tblpa[i])
			return;
	}
	tblpa[ntblpa++] = pa;

	memreserve(pa, 8);
	if((t = vmap(pa, 8)) == nil)
		return;
	l = get32(t->len);
	if(l < Tblsz
	|| l >= 0x10000000
	|| -pa < l){
		vunmap(t, 8);
		return;
	}
	memreserve(pa, l);
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
	if(rsd->rev >= 2){
		if(!checksum(rsd, 36))
			maptable(get64(rsd->xaddr));
	}
}

static Tbl*
findtable(char sig[4])
{
	Tbl *t;
	int i;

	for(i=0; i<ntblmap; i++){
		t = tblmap[i];
		if(memcmp(t->sig, sig, 4) == 0)
			return t;
	}
	return nil;
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
	ai->bus = bus;
	bus->aintr = ai;
}

static int
pcibusno(void *dot)
{
	int bno, adr, tbdf;
	Pcidev *pdev;
	void *p, *x;
	char *id;

	id = nil;
	if((x = amlwalk(dot, "^_HID")) != nil)
		if((p = amlval(x)) != nil)
			id = amleisaid(p);
	if((x = amlwalk(dot, "^_BBN")) == nil)
		if((x = amlwalk(dot, "^_ADR")) == nil)
			return -1;
	p = nil;
	if(amleval(x, "", &p) < 0)
		return -1;
	adr = amlint(p);
	/* if root bridge, then we are done here */
	if(id != nil && (strcmp(id, "PNP0A03")==0 || strcmp(id, "PNP0A08")==0))
		return adr;
	x = amlwalk(dot, "^");
	if(x == nil || x == dot)
		return -1;
	if((bno = pcibusno(x)) < 0)
		return -1;
	tbdf = MKBUS(BusPCI, bno, adr>>16, adr&0xFFFF);
	pdev = pcimatchtbdf(tbdf);
	if(pdev == nil)
		return -1;
	if(pdev->bridge == nil)
		return bno;
	return BUSBNO(pdev->bridge->tbdf);
}

static int
pciaddr(void *dot)
{
	int adr, bno;
	void *x, *p;

	for(;;){
		if((x = amlwalk(dot, "_ADR")) == nil){
			x = amlwalk(dot, "^");
			if(x == nil || x == dot)
				break;
			dot = x;
			continue;
		}
		if((bno = pcibusno(x)) < 0)
			break;
		p = nil;
		if(amleval(x, "", &p) < 0)
			break;
		adr = amlint(p);
		return MKBUS(BusPCI, bno, adr>>16, adr&0xFFFF);
	}
	return -1;
}

static int
getirqs(void *d, uchar pmask[32], int *pflags)
{
	int i, n, m;
	uchar *p;

	*pflags = 0;
	memset(pmask, 0, 32);
	if(amltag(d) != 'b')
		return -1;
	p = amlval(d);
	if(amllen(d) >= 2 && (p[0] == 0x22 || p[0] == 0x23)){
		pmask[0] = p[1];
		pmask[1] = p[2];
		if(amllen(d) >= 3 && p[0] == 0x23)
			*pflags = ((p[3] & (1<<0)) ? PcmpEDGE : PcmpLEVEL)
				| ((p[3] & (1<<3)) ? PcmpLOW : PcmpHIGH);
		return 0;
	}
	if(amllen(d) >= 5 && p[0] == 0x89){
		n = p[4];
		if(amllen(d) < 5+n*4)
			return -1;
		for(i=0; i<n; i++){
			m = get32(p+5 + i*4);
			if(m >= 0 && m < 256)
				pmask[m/8] |= 1<<(m%8);
		}
		*pflags = ((p[3] & (1<<1)) ? PcmpEDGE : PcmpLEVEL)
			| ((p[3] & (1<<2)) ? PcmpLOW : PcmpHIGH);
		return 0;
	}
	return -1;
}

static uchar*
setirq(void *d, uint irq)
{
	uchar *p;

	if(amltag(d) != 'b')
		return nil;
	p = amlnew('b', amllen(d));
	memmove(p, d, amllen(p));
	if(p[0] == 0x22 || p[0] == 0x23){
		irq = 1<<irq;
		p[1] = irq;
		p[2] = irq>>8;
	}
	if(p[0] == 0x89){
		p[4] = 1;
		p[5] = irq;
		p[6] = irq>>8;
		p[7] = irq>>16;
		p[8] = irq>>24;
	}
	return p;
}

static int
setuplink(void *link, int *pflags)
{
	uchar im, pm[32], cm[32], *c;
	static int lastirq = 1;
	int gsi, i;
	void *r;

	if(amltag(link) != 'N')
		return -1;

	r = nil;
	if(amleval(amlwalk(link, "_PRS"), "", &r) < 0)
		return -1;
	if(getirqs(r, pm, pflags) < 0)
		return -1;

	r = nil;
	if(amleval(amlwalk(link, "_CRS"), "", &r) < 0)
		return -1;
	if(getirqs(r, cm, pflags) < 0)
		return -1;
	
	gsi = -1;
	for(i=0; i<256; i++){
		im = 1<<(i%8);
		if(pm[i/8] & im){
			if(cm[i/8] & im)
				gsi = i;
		}
	}

	if(gsi > 0 || getconf("*nopcirouting") != nil)
		return gsi;

	for(i=0; i<256; i++){
		gsi = lastirq++ & 0xFF;	/* round robin */
		im = 1<<(gsi%8);
		if(pm[gsi/8] & im){
			if((c = setirq(r, gsi)) == nil)
				break;
			if(amleval(amlwalk(link, "_SRS"), "b", c, nil) < 0)
				break;
			return gsi;
		}
	}
	return -1;
}

static int
enumprt(void *dot, void *)
{
	void *p, **a, **b;
	int bno, dno, pin, gsi, flags;
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

	amltake(p);
	n = amllen(p);
	a = amlval(p);
	for(i=0; i<n; i++){
		if(amltag(a[i]) != 'p')
			continue;
		if(amllen(a[i]) != 4)
			continue;
		flags = 0;
		b = amlval(a[i]);
		dno = amlint(b[0])>>16;
		pin = amlint(b[1]);
		gsi = amlint(b[3]);
		if(gsi==0){
			gsi = setuplink(b[2], &flags);
			if(gsi <= 0)
				continue;
		}
		addirq(gsi, BusPCI, bno, (dno<<2)|pin, flags);
	}
	amldrop(p);

	return 1;
}

static int
enumec(void *dot, void *)
{
	int cmdport, dataport;
	uchar *b;
	void *x;
	char *id;

	b = nil;
	id = amleisaid(amlval(amlwalk(dot, "^_HID")));
	if(id == nil || strcmp(id, "PNP0C09") != 0)
		return 1;
	if((x = amlwalk(dot, "^_CRS")) == nil)
		return 1;
	if(amleval(x, "", &b) < 0 || amltag(b) != 'b' || amllen(b) < 16)
		return 1;
	if(b[0] != 0x47 || b[8] != 0x47)	/* two i/o port descriptors */
		return 1;
	dataport = b[0+2] | b[0+3]<<8;
	cmdport = b[8+2] | b[8+3]<<8;
	ecinit(cmdport, dataport);
	return 1;
}

static long
readmem(Chan*, void *v, long n, vlong o)
{
	uvlong pa = (uvlong)o;
	void *t;

	if((n = memcheck(pa, n)) <= 0)
		return 0;
	if((t = vmap(pa, n)) == nil)
		error(Enovmem);
	if(waserror()){
		vunmap(t, n);
		nexterror();
	}
	memmove(v, t, n);
	vunmap(t, n);
	poperror();
	return n;
}

static long
writemem(Chan*, void *v, long n, vlong o)
{
	uvlong pa = (uvlong)o;
	void *t;

	if(memcheck(pa, n) != n)
		error(Eio);
	if((t = vmap(pa, n)) == nil)
		error(Enovmem);
	if(waserror()){
		vunmap(t, n);
		nexterror();
	}
	memmove(t, v, n);
	vunmap(t, n);
	poperror();
	return n;
}

static void
acpiinit(void)
{
	Tbl *t;
	Apic *a;
	void *va;
	uchar *s, *p, *e;
	ulong lapicbase;
	int machno, i, c;

	amlinit();

	/* load DSDT */
	if((t = findtable("DSDT")) != nil){
		amlintmask = (~0ULL) >> (t->rev <= 1)*32;
		amlload(t->data, tbldlen(t));
	}

	/* load SSDT, there can be multiple tables */
	for(i=0; i<ntblmap; i++){
		t = tblmap[i];
		if(memcmp(t->sig, "SSDT", 4) == 0)
			amlload(t->data, tbldlen(t));
	}

	/* set APIC mode */
	amleval(amlwalk(amlroot, "_PIC"), "i", 1, nil);

	t = findtable("APIC");
	if(t == nil)
		panic("acpiinit: no MADT (APIC) table");

	s = t->data;
	e = s + tbldlen(t);
	lapicbase = get32(s); s += 8;
	va = vmap(lapicbase, 1024);
	print("LAPIC: %.8lux %#p\n", lapicbase, va);
	if(va == nil)
		panic("acpiinit: cannot map lapic %.8lux", lapicbase);

	machno = 0;
	for(p = s; p < e; p += c){
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

			/* skip disabled processors */
			if((a->flags & PcmpEN) == 0 || mpapic[a->apicno] != nil){
				xfree(a);
				break;
			}
			a->machno = machno++;

			/*
			 * platform firmware should list the boot processor
			 * as the first processor entry in the MADT
			 */
			if(a->machno == 0)
				a->flags |= PcmpBP;

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
		}
	}

	/*
	 * need 2nd pass as vbox puts interrupt overrides
	 * *before* the ioapic entries (!)
	 */
	for(p = s; p < e; p += c){
		c = p[1];
		if(c < 2 || (p+c) > e)
			break;
		switch(*p){
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

	/* find embedded controller */
	amlenum(amlroot, "_HID", enumec, nil);

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

static void
acpireset(void)
{
	uchar *p;
	Tbl *t;

	/* stop application processors */
	mpshutdown();

	/* locate and write platform reset register */
	while((t = findtable("FACP")) != nil){
		if(get32(t->len) <= 128)
			break;
		p = (uchar*)t;
		if((get32(p + 112) & (1<<10)) == 0)
			break;
		if(p[116+0] != IoSpace)
			break;
		outb(get32(p+116+4), p[128]);
		break;
	}

	/* acpi shutdown failed, try generic reset */
	archreset();
}

static int identify(void);
extern int i8259irqno(int, int);
extern void i8253init(void);

extern int hpetprobe(uvlong);
extern void hpetinit(void);
extern uvlong hpetread(uvlong*);

PCArch archacpi = {
.id=		"ACPI",	
.ident=		identify,
.reset=		acpireset,
.intrinit=	acpiinit,
.intrassign=	mpintrassign,
.intrirqno=	i8259irqno,
.intron=	lapicintron,
.introff=	lapicintroff,
.clockinit=	i8253init,
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
	uvlong v;
	char *cp;
	Tbl *t;

	if((cp = getconf("*acpi")) == nil || *cp == '\0')
		cp = "1";	/* search for rsd by default */
	v = (uintptr)strtoull(cp, nil, 16);
	if(v <= 1)
		rsd = rsdsearch();
	else {
		memreserve(v, sizeof(Rsd));
		rsd = vmap(v, sizeof(Rsd));
	}
	if(rsd == nil)
		return 1;
	if(checksum(rsd, 20) && checksum(rsd, 36))
		return 1;
	maptables();
	addarchfile("acpitbls", 0444, readtbls, nil);
	addarchfile("acpimem", 0600, readmem, writemem);
	if(v == 0 || findtable("APIC") == nil)
		return 1;
	if((cp = getconf("*nomp")) != nil && strcmp(cp, "0") != 0)
		return 1;
	if(getconf("*nohpet") == nil
	&& (t = findtable("HPET")) != nil
	&& ((uchar*)t)[40] == 0
	&& hpetprobe(get64((uchar*)t+44)) == 0){
		archacpi.clockinit = hpetinit;
		archacpi.fastclock = hpetread;
	}
	if(m->havetsc && getconf("*notsc") == nil)
		archacpi.fastclock = tscticks;

	return 0;
}

static int
readpcicfg(Amlio *io, void *data, int n, int offset)
{
	ulong r, x;
	Pcidev *p;
	uchar *a;
	int i;

	a = data;
	p = io->aux;
	if(p == nil)
		return -1;
	offset += io->off;
	if(offset > 256)
		return 0;
	if(n+offset > 256)
		n = 256-offset;
	r = offset;
	if(!(r & 3) && n == 4){
		x = pcicfgr32(p, r);
		PBIT32(a, x);
		return 4;
	}
	if(!(r & 1) && n == 2){
		x = pcicfgr16(p, r);
		PBIT16(a, x);
		return 2;
	}
	for(i = 0; i <  n; i++){
		x = pcicfgr8(p, r);
		PBIT8(a, x);
		a++;
		r++;
	}
	return i;
}

static int
readec(Amlio *io, void *data, int n, int off)
{
	int port, v;
	uchar *p;

	USED(io);
	if(off < 0 || off >= 256)
		return 0;
	if(off+n > 256)
		n = 256 - off;
	p = data;
	for(port = off; port < off+n; port++){
		if((v = ecread(port)) < 0)
			break;
		*p++ = v;
	}
	return n;
}

static int
writeec(Amlio *io, void *data, int n, int off)
{
	int port;
	uchar *p;

	USED(io);
	if(off < 0 || off+n > 256)
		return -1;
	p = data;
	for(port = off; port < off+n; port++)
		if(ecwrite(port, *p++) < 0)
			break;
	return n;
}

static int
writepcicfg(Amlio *io, void *data, int n, int offset)
{
	ulong r, x;
	Pcidev *p;
	uchar *a;
	int i;

	a = data;
	p = io->aux;
	if(p == nil)
		return -1;
	offset += io->off;
	if(offset > 256)
		return 0;
	if(n+offset > 256)
		n = 256-offset;
	r = offset;
	if(!(r & 3) && n == 4){
		x = GBIT32(a);
		pcicfgw32(p, r, x);
		return 4;
	}
	if(!(r & 1) && n == 2){
		x = GBIT16(a);
		pcicfgw16(p, r, x);
		return 2;
	}
	for(i = 0; i <  n; i++){
		x = GBIT8(a);
		pcicfgw8(p, r, x);
		a++;
		r++;
	}
	return i;
}

static int
readioport(Amlio *io, void *data, int len, int port)
{
	uchar *a;

	a = data;
	port += io->off;
	switch(len){
	case 4:
		PBIT32(a, inl(port));
		return 4;
	case 2:
		PBIT16(a, ins(port));
		return 2;
	case 1:
		PBIT8(a, inb(port));
		return 1;
	}
	return -1;
}

static int
writeioport(Amlio *io, void *data, int len, int port)
{
	uchar *a;

	a = data;
	port += io->off;
	switch(len){
	case 4:
		outl(port, GBIT32(a));
		return 4;
	case 2:
		outs(port, GBIT16(a));
		return 2;
	case 1:
		outb(port, GBIT8(a));
		return 1;
	}
	return -1;
}

int
amlmapio(Amlio *io)
{
	int tbdf;
	Pcidev *pdev;
	char buf[64];

	switch(io->space){
	default:
		print("amlmapio: address space %x not implemented\n", io->space);
		break;
	case MemSpace:
		if(memcheck(io->off, io->len) != io->len){
			print("amlmapio: [%#p-%#p) overlaps usable memory\n",
				(uintptr)io->off, (uintptr)(io->off+io->len));
			break;
		}
		if((io->va = vmap(io->off, io->len)) == nil){
			print("amlmapio: vmap failed\n");
			break;
		}
		return 0;
	case IoSpace:
		snprint(buf, sizeof(buf), "%N", io->name);
		if(ioalloc(io->off, io->len, 0, buf) < 0){
			print("amlmapio: ioalloc failed\n");
			break;
		}
		io->read = readioport;
		io->write = writeioport;
		return 0;
	case PcicfgSpace:
		if((tbdf = pciaddr(io->name)) < 0){
			print("amlmapio: no address\n");
			break;
		}
		if((pdev = pcimatchtbdf(tbdf)) == nil){
			print("amlmapio: no device %T\n", tbdf);
			break;
		}
		io->aux = pdev;
		io->read = readpcicfg;
		io->write = writepcicfg;
		return 0;
	case EbctlSpace:
		io->read = readec;
		io->write = writeec;
		return 0;
	}
	print("amlmapio: mapping %N failed\n", io->name);
	return -1;
}

void
amlunmapio(Amlio *io)
{
	switch(io->space){
	case MemSpace:
		vunmap(io->va, io->len);
		break;
	case IoSpace:
		iofree(io->off);
		break;
	}
}

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

void
amldelay(int us)
{
	microdelay(us);
}
