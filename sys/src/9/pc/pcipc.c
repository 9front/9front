#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "../port/error.h"

#define DBG	if(1) print

enum
{	/* configuration mechanism #1 */
	PciADDR		= 0xCF8,	/* CONFIG_ADDRESS */
	PciDATA		= 0xCFC,	/* CONFIG_DATA */

	/* configuration mechanism #2 */
	PciCSE		= 0xCF8,	/* configuration space enable */
	PciFORWARD	= 0xCFA,	/* which bus */
};

static int pcimaxbno = 255;
static int pcicfgmode = -1;
static Pcidev* pciroot;
static int nobios, nopcirouting;
static BIOS32si* pcibiossi;

static int pcicfgrw8raw(int, int, int, int);
static int pcicfgrw16raw(int, int, int, int);
static int pcicfgrw32raw(int, int, int, int);

int (*pcicfgrw8)(int, int, int, int) = pcicfgrw8raw;
int (*pcicfgrw16)(int, int, int, int) = pcicfgrw16raw;
int (*pcicfgrw32)(int, int, int, int) = pcicfgrw32raw;

static int
pcicfgrw8raw(int tbdf, int rno, int data, int read)
{
	int o;

	switch(pcicfgmode){
	case 1:
		o = rno & 0x03;
		rno &= ~0x03;
		outl(PciADDR, 0x80000000|BUSBDF(tbdf)|rno);
		if(read)
			data = inb(PciDATA+o);
		else
			outb(PciDATA+o, data);
		outl(PciADDR, 0);
		break;

	case 2:
		outb(PciCSE, 0x80|(BUSFNO(tbdf)<<1));
		outb(PciFORWARD, BUSBNO(tbdf));
		if(read)
			data = inb((0xC000|(BUSDNO(tbdf)<<8)) + rno);
		else
			outb((0xC000|(BUSDNO(tbdf)<<8)) + rno, data);
		outb(PciCSE, 0);
		break;
	default:
		data = -1;
	}
	return data;
}

static int
pcicfgrw16raw(int tbdf, int rno, int data, int read)
{
	int o;

	switch(pcicfgmode){
	case 1:
		o = rno & 0x02;
		rno &= ~0x03;
		outl(PciADDR, 0x80000000|BUSBDF(tbdf)|rno);
		if(read)
			data = ins(PciDATA+o);
		else
			outs(PciDATA+o, data);
		outl(PciADDR, 0);
		break;

	case 2:
		outb(PciCSE, 0x80|(BUSFNO(tbdf)<<1));
		outb(PciFORWARD, BUSBNO(tbdf));
		if(read)
			data = ins((0xC000|(BUSDNO(tbdf)<<8)) + rno);
		else
			outs((0xC000|(BUSDNO(tbdf)<<8)) + rno, data);
		outb(PciCSE, 0);
		break;
	default:
		data = -1;
	}
	return data;
}

static int
pcicfgrw32raw(int tbdf, int rno, int data, int read)
{
	switch(pcicfgmode){
	case 1:
		rno &= ~0x03;
		outl(PciADDR, 0x80000000|BUSBDF(tbdf)|rno);
		if(read)
			data = inl(PciDATA);
		else
			outl(PciDATA, data);
		outl(PciADDR, 0);
		break;

	case 2:
		outb(PciCSE, 0x80|(BUSFNO(tbdf)<<1));
		outb(PciFORWARD, BUSBNO(tbdf));
		if(read)
			data = inl((0xC000|(BUSDNO(tbdf)<<8)) + rno);
		else
			outl((0xC000|(BUSDNO(tbdf)<<8)) + rno, data);
		outb(PciCSE, 0);
		break;
	default:
		data = -1;
	}
	return data;
}

static int
pcicfgrw8bios(int tbdf, int rno, int data, int read)
{
	BIOS32ci ci;

	if(pcibiossi == nil)
		return -1;

	memset(&ci, 0, sizeof(BIOS32ci));
	ci.ebx = (BUSBNO(tbdf)<<8)|(BUSDNO(tbdf)<<3)|BUSFNO(tbdf);
	ci.edi = rno;
	if(read){
		ci.eax = 0xB108;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return ci.ecx & 0xFF;
	}
	else{
		ci.eax = 0xB10B;
		ci.ecx = data & 0xFF;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return 0;
	}

	return -1;
}

static int
pcicfgrw16bios(int tbdf, int rno, int data, int read)
{
	BIOS32ci ci;

	if(pcibiossi == nil)
		return -1;

	memset(&ci, 0, sizeof(BIOS32ci));
	ci.ebx = (BUSBNO(tbdf)<<8)|(BUSDNO(tbdf)<<3)|BUSFNO(tbdf);
	ci.edi = rno;
	if(read){
		ci.eax = 0xB109;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return ci.ecx & 0xFFFF;
	}
	else{
		ci.eax = 0xB10C;
		ci.ecx = data & 0xFFFF;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return 0;
	}

	return -1;
}

static int
pcicfgrw32bios(int tbdf, int rno, int data, int read)
{
	BIOS32ci ci;

	if(pcibiossi == nil)
		return -1;

	memset(&ci, 0, sizeof(BIOS32ci));
	ci.ebx = (BUSBNO(tbdf)<<8)|(BUSDNO(tbdf)<<3)|BUSFNO(tbdf);
	ci.edi = rno;
	if(read){
		ci.eax = 0xB10A;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return ci.ecx;
	}
	else{
		ci.eax = 0xB10D;
		ci.ecx = data;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return 0;
	}

	return -1;
}

static BIOS32si*
pcibiosinit(void)
{
	BIOS32ci ci;
	BIOS32si *si;

	if((si = bios32open("$PCI")) == nil)
		return nil;

	memset(&ci, 0, sizeof(BIOS32ci));
	ci.eax = 0xB101;
	if(bios32ci(si, &ci) || ci.edx != ((' '<<24)|('I'<<16)|('C'<<8)|'P')){
		free(si);
		return nil;
	}
	if(ci.eax & 0x01)
		pcimaxdno = 31;
	else
		pcimaxdno = 15;
	pcimaxbno = ci.ecx & 0xff;

	return si;
}

static uchar
pIIxget(Pcidev *router, uchar link)
{
	uchar pirq;

	/* link should be 0x60, 0x61, 0x62, 0x63 */
	pirq = pcicfgr8(router, link);
	return (pirq < 16)? pirq: 0;
}

static void
pIIxset(Pcidev *router, uchar link, uchar irq)
{
	pcicfgw8(router, link, irq);
}

static uchar
viaget(Pcidev *router, uchar link)
{
	uchar pirq;

	/* link should be 1, 2, 3, 5 */
	pirq = (link < 6)? pcicfgr8(router, 0x55 + (link>>1)): 0;

	return (link & 1)? (pirq >> 4): (pirq & 15);
}

static void
viaset(Pcidev *router, uchar link, uchar irq)
{
	uchar pirq;

	pirq = pcicfgr8(router, 0x55 + (link >> 1));
	pirq &= (link & 1)? 0x0f: 0xf0;
	pirq |= (link & 1)? (irq << 4): (irq & 15);
	pcicfgw8(router, 0x55 + (link>>1), pirq);
}

static uchar
optiget(Pcidev *router, uchar link)
{
	uchar pirq = 0;

	/* link should be 0x02, 0x12, 0x22, 0x32 */
	if ((link & 0xcf) == 0x02)
		pirq = pcicfgr8(router, 0xb8 + (link >> 5));
	return (link & 0x10)? (pirq >> 4): (pirq & 15);
}

static void
optiset(Pcidev *router, uchar link, uchar irq)
{
	uchar pirq;

	pirq = pcicfgr8(router, 0xb8 + (link >> 5));
    	pirq &= (link & 0x10)? 0x0f : 0xf0;
    	pirq |= (link & 0x10)? (irq << 4): (irq & 15);
	pcicfgw8(router, 0xb8 + (link >> 5), pirq);
}

static uchar
aliget(Pcidev *router, uchar link)
{
	/* No, you're not dreaming */
	static const uchar map[] = { 0, 9, 3, 10, 4, 5, 7, 6, 1, 11, 0, 12, 0, 14, 0, 15 };
	uchar pirq;

	/* link should be 0x01..0x08 */
	pirq = pcicfgr8(router, 0x48 + ((link-1)>>1));
	return (link & 1)? map[pirq&15]: map[pirq>>4];
}

static void
aliset(Pcidev *router, uchar link, uchar irq)
{
	/* Inverse of map in aliget */
	static const uchar map[] = { 0, 8, 0, 2, 4, 5, 7, 6, 0, 1, 3, 9, 11, 0, 13, 15 };
	uchar pirq;

	pirq = pcicfgr8(router, 0x48 + ((link-1)>>1));
	pirq &= (link & 1)? 0x0f: 0xf0;
	pirq |= (link & 1)? (map[irq] << 4): (map[irq] & 15);
	pcicfgw8(router, 0x48 + ((link-1)>>1), pirq);
}

static uchar
cyrixget(Pcidev *router, uchar link)
{
	uchar pirq;

	/* link should be 1, 2, 3, 4 */
	pirq = pcicfgr8(router, 0x5c + ((link-1)>>1));
	return ((link & 1)? pirq >> 4: pirq & 15);
}

static void
cyrixset(Pcidev *router, uchar link, uchar irq)
{
	uchar pirq;

	pirq = pcicfgr8(router, 0x5c + (link>>1));
	pirq &= (link & 1)? 0x0f: 0xf0;
	pirq |= (link & 1)? (irq << 4): (irq & 15);
	pcicfgw8(router, 0x5c + (link>>1), pirq);
}

typedef struct Bridge Bridge;
struct Bridge
{
	ushort	vid;
	ushort	did;
	uchar	(*get)(Pcidev *, uchar);
	void	(*set)(Pcidev *, uchar, uchar);
};

static Bridge southbridges[] = {
	{ 0x8086, 0x122e, pIIxget, pIIxset },	/* Intel 82371FB */
	{ 0x8086, 0x1234, pIIxget, pIIxset },	/* Intel 82371MX */
	{ 0x8086, 0x7000, pIIxget, pIIxset },	/* Intel 82371SB */
	{ 0x8086, 0x7110, pIIxget, pIIxset },	/* Intel 82371AB */
	{ 0x8086, 0x7198, pIIxget, pIIxset },	/* Intel 82443MX (fn 1) */
	{ 0x8086, 0x2410, pIIxget, pIIxset },	/* Intel 82801AA */
	{ 0x8086, 0x2420, pIIxget, pIIxset },	/* Intel 82801AB */
	{ 0x8086, 0x2440, pIIxget, pIIxset },	/* Intel 82801BA */
	{ 0x8086, 0x2448, pIIxget, pIIxset },	/* Intel 82801BAM/CAM/DBM */
	{ 0x8086, 0x244c, pIIxget, pIIxset },	/* Intel 82801BAM */
	{ 0x8086, 0x244e, pIIxget, pIIxset },	/* Intel 82801 */
	{ 0x8086, 0x2480, pIIxget, pIIxset },	/* Intel 82801CA */
	{ 0x8086, 0x248c, pIIxget, pIIxset },	/* Intel 82801CAM */
	{ 0x8086, 0x24c0, pIIxget, pIIxset },	/* Intel 82801DBL */
	{ 0x8086, 0x24cc, pIIxget, pIIxset },	/* Intel 82801DBM */
	{ 0x8086, 0x24d0, pIIxget, pIIxset },	/* Intel 82801EB */
	{ 0x8086, 0x25a1, pIIxget, pIIxset },	/* Intel 6300ESB */
	{ 0x8086, 0x2640, pIIxget, pIIxset },	/* Intel 82801FB */
	{ 0x8086, 0x2641, pIIxget, pIIxset },	/* Intel 82801FBM */
	{ 0x8086, 0x2670, pIIxget, pIIxset },	/* Intel 632xesb */
	{ 0x8086, 0x27b8, pIIxget, pIIxset },	/* Intel 82801GB */
	{ 0x8086, 0x27b9, pIIxget, pIIxset },	/* Intel 82801GBM */
	{ 0x8086, 0x27bd, pIIxget, pIIxset },	/* Intel 82801GB/GR */
	{ 0x8086, 0x3a16, pIIxget, pIIxset },	/* Intel 82801JIR */
	{ 0x8086, 0x3a40, pIIxget, pIIxset },	/* Intel 82801JI */
	{ 0x8086, 0x3a42, pIIxget, pIIxset },	/* Intel 82801JI */
	{ 0x8086, 0x3a48, pIIxget, pIIxset },	/* Intel 82801JI */
	{ 0x8086, 0x2916, pIIxget, pIIxset },	/* Intel 82801? */
	{ 0x8086, 0x1c02, pIIxget, pIIxset },	/* Intel 6 Series/C200 */
	{ 0x8086, 0x1e53, pIIxget, pIIxset },	/* Intel 7 Series/C216 */
	{ 0x8086, 0x8c56, pIIxget, pIIxset },	/* Intel 8 Series/C226 */
	{ 0x8086, 0x2810, pIIxget, pIIxset },	/* Intel 82801HB/HR (ich8/r) */
	{ 0x8086, 0x2812, pIIxget, pIIxset },	/* Intel 82801HH (ich8dh) */
	{ 0x8086, 0x2912, pIIxget, pIIxset },	/* Intel 82801ih ich9dh */
	{ 0x8086, 0x2914, pIIxget, pIIxset },	/* Intel 82801io ich9do */
	{ 0x8086, 0x2916, pIIxget, pIIxset },	/* Intel 82801ibr ich9r */
	{ 0x8086, 0x2917, pIIxget, pIIxset },	/* Intel 82801iem ich9m-e  */
	{ 0x8086, 0x2918, pIIxget, pIIxset },	/* Intel 82801ib ich9 */
	{ 0x8086, 0x2919, pIIxget, pIIxset },	/* Intel 82801? ich9m  */
	{ 0x8086, 0x3a16, pIIxget, pIIxset },	/* Intel 82801jir ich10r */
	{ 0x8086, 0x3a18, pIIxget, pIIxset },	/* Intel 82801jib ich10 */
	{ 0x8086, 0x3a40, pIIxget, pIIxset },	/* Intel 82801ji */
	{ 0x8086, 0x3a42, pIIxget, pIIxset },	/* Intel 82801ji */
	{ 0x8086, 0x3a48, pIIxget, pIIxset },	/* Intel 82801ji */
	{ 0x8086, 0x3b06, pIIxget, pIIxset },	/* Intel 82801? ibex peak */
	{ 0x8086, 0x3b14, pIIxget, pIIxset },	/* Intel 82801? 3420 */
	{ 0x8086, 0x1c49, pIIxget, pIIxset },	/* Intel 82hm65 cougar point pch */
	{ 0x8086, 0x1c4b, pIIxget, pIIxset },	/* Intel 82hm67 */
	{ 0x8086, 0x1c4f, pIIxget, pIIxset },	/* Intel 82qm67 cougar point pch */
	{ 0x8086, 0x1c52, pIIxget, pIIxset },	/* Intel 82q65 cougar point pch */
	{ 0x8086, 0x1c54, pIIxget, pIIxset },	/* Intel 82q67 cougar point pch */
	{ 0x8086, 0x1e55, pIIxget, pIIxset },	/* Intel QM77 panter point lpc */

	{ 0x1106, 0x0586, viaget, viaset },	/* Viatech 82C586 */
	{ 0x1106, 0x0596, viaget, viaset },	/* Viatech 82C596 */
	{ 0x1106, 0x0686, viaget, viaset },	/* Viatech 82C686 */
	{ 0x1106, 0x3177, viaget, viaset },	/* Viatech VT8235 */
	{ 0x1106, 0x3227, viaget, viaset },	/* Viatech VT8237 */
	{ 0x1106, 0x3287, viaget, viaset },	/* Viatech VT8251 */
	{ 0x1106, 0x8410, viaget, viaset },	/* Viatech PV530 bridge */
	{ 0x1045, 0xc700, optiget, optiset },	/* Opti 82C700 */
	{ 0x10b9, 0x1533, aliget, aliset },	/* Al M1533 */
	{ 0x1039, 0x0008, pIIxget, pIIxset },	/* SI 503 */
	{ 0x1039, 0x0496, pIIxget, pIIxset },	/* SI 496 */
	{ 0x1078, 0x0100, cyrixget, cyrixset },	/* Cyrix 5530 Legacy */

	{ 0x1022, 0x790e, nil, nil },		/* AMD FCH LPC bridge */
	{ 0x1022, 0x746b, nil, nil },		/* AMD 8111 */
	{ 0x10de, 0x00d1, nil, nil },		/* NVIDIA nForce 3 */
	{ 0x10de, 0x00e0, nil, nil },		/* NVIDIA nForce 3 250 Series */
	{ 0x10de, 0x00e1, nil, nil },		/* NVIDIA nForce 3 250 Series */
	{ 0x1166, 0x0200, nil, nil },		/* ServerWorks ServerSet III LE */
	{ 0x1002, 0x4377, nil, nil },		/* ATI Radeon Xpress 200M */
	{ 0x1002, 0x4372, nil, nil },		/* ATI SB400 */
	{ 0x1002, 0x9601, nil, nil },		/* AMD SB710 */
	{ 0x1002, 0x438d, nil, nil },		/* AMD SB600 */
	{ 0x1002, 0x439d, nil, nil },		/* AMD SB810 */
};

typedef struct Slot Slot;
struct Slot {
	uchar	bus;		/* Pci bus number */
	uchar	dev;		/* Pci device number */
	uchar	maps[12];	/* Avoid structs!  Link and mask. */
	uchar	slot;		/* Add-in/built-in slot */
	uchar	reserved;
};

typedef struct Router Router;
struct Router {
	uchar	signature[4];	/* Routing table signature */
	uchar	version[2];	/* Version number */
	uchar	size[2];	/* Total table size */
	uchar	bus;		/* Interrupt router bus number */
	uchar	devfn;		/* Router's devfunc */
	uchar	pciirqs[2];	/* Exclusive PCI irqs */
	uchar	compat[4];	/* Compatible PCI interrupt router */
	uchar	miniport[4];	/* Miniport data */
	uchar	reserved[11];
	uchar	checksum;
};

static ushort pciirqs;		/* Exclusive PCI irqs */
static Bridge *southbridge;	/* Which southbridge to use. */

static void
pcirouting(void)
{
	Slot *e;
	Router *r;
	int i, size, tbdf;
	Pcidev *sbpci, *pci;
	uchar *p, pin, irq, link, *map;

	if((p = sigsearch("$PIR", 0)) == nil)
		return;

	r = (Router*)p;
	size = (r->size[1] << 8)|r->size[0];
	if(size < sizeof(Router) || checksum(r, size))
		return;

	if(0) print("PCI interrupt routing table version %d.%d at %p\n",
		r->version[0], r->version[1], r);

	tbdf = MKBUS(BusPCI, r->bus, (r->devfn>>3)&0x1f, r->devfn&7);
	sbpci = pcimatchtbdf(tbdf);
	if(sbpci == nil) {
		print("pcirouting: Cannot find south bridge %T\n", tbdf);
		return;
	}

	for(i = 0; i < nelem(southbridges); i++)
		if(sbpci->vid == southbridges[i].vid && sbpci->did == southbridges[i].did)
			break;

	if(i == nelem(southbridges)) {
		print("pcirouting: ignoring south bridge %T %.4uX/%.4uX\n", tbdf, sbpci->vid, sbpci->did);
		return;
	}
	southbridge = &southbridges[i];
	if(southbridge->get == nil)
		return;

	pciirqs = (r->pciirqs[1] << 8)|r->pciirqs[0];
	for(e = (Slot *)&r[1]; (uchar *)e < p + size; e++) {
		if(0) {
			print("%.2uX/%.2uX %.2uX: ", e->bus, e->dev, e->slot);
			for (i = 0; i < 4; i++) {
				map = &e->maps[i * 3];
				print("[%d] %.2uX %.4uX ", i, map[0], (map[2] << 8)|map[1]);
			}
			print("\n");
		}
		for(i = 0; i < 8; i++) {
			tbdf = MKBUS(BusPCI, e->bus, (e->dev>>3)&0x1f, i);
			pci = pcimatchtbdf(tbdf);
			if(pci == nil)
				continue;
			pin = pcicfgr8(pci, PciINTP);
			if(pin == 0 || pin == 0xff)
				continue;

			map = &e->maps[((pin - 1) % 4) * 3];
			link = map[0];
			irq = southbridge->get(sbpci, link);
			if(irq == pci->intl)
				continue;
			if(irq == 0 || (irq & 0x80) != 0){
				irq = pci->intl;
				if(irq == 0 || irq == 0xff)
					continue;
				if(southbridge->set == nil)
					continue;
				southbridge->set(sbpci, link, irq);
			}
			print("pcirouting: %T at pin %d link %.2uX irq %d -> %d\n", tbdf, pin, link, pci->intl, irq);
			pcicfgw8(pci, PciINTL, irq);
			pci->intl = irq;
		}
	}
}

static void
pcireserve(void)
{
	char tag[64];
	Pcidev *p;
	uvlong pa;
	ulong io;
	int i;

	/*
	 * mark all valid io/mem address space claimed by pci devices
	 * so that ioreserve/upaalloc doesn't give it out.
	 */
	for(p=pciroot; p != nil; p=p->list){
		snprint(tag, sizeof(tag), "%T", p->tbdf);
		for(i=0; i<nelem(p->mem); i++){
			if(p->mem[i].size == 0)
				continue;
			if(p->mem[i].bar & 1){
				io = p->mem[i].bar & ~3ULL;
				if(io == 0)
					continue;
				ioreserve(io, p->mem[i].size, 0, tag);
			} else {
				pa = p->mem[i].bar & ~0xFULL;
				if(pa == 0)
					continue;
				upaalloc(pa, p->mem[i].size, 0);
			}
		}
		if(p->rom.size && (p->rom.bar & 1) != 0){
			pa = p->rom.bar & ~0x7FFULL;
			upaalloc(pa, p->rom.size, 0);
		}
	}

	/*
	 * allocate io/mem address space for unassigned membars.
	 */
	for(p=pciroot; p != nil; p=p->list){
		snprint(tag, sizeof(tag), "%T", p->tbdf);
		for(i=0; i<nelem(p->mem); i++){
			if(p->mem[i].size == 0)
				continue;
			if(p->mem[i].bar & 1){
				if(p->mem[i].bar & ~0x3ULL)
					continue;
				if(p->parent == nil){
					io = ioreserve(-1, p->mem[i].size, p->mem[i].size, tag);
				} else {
					io = ioreservewin(p->parent->ioa.bar, p->parent->ioa.size,
						p->mem[i].size, p->mem[i].size, tag);
				}
				if(io == -1)
					continue;
				p->mem[i].bar |= io;
			} else {
				if(p->mem[i].bar & ~0xFULL)
					continue;
				if(p->parent == nil){
					pa = upaalloc(-1ULL, p->mem[i].size, p->mem[i].size);
				} else if(p->mem[i].bar & 8){
					pa = upaallocwin(p->parent->prefa.bar, p->parent->prefa.size,
						p->mem[i].size, p->mem[i].size);
					if(pa == -1ULL)
						goto Mem;
				} else {
				Mem:
					pa = upaallocwin(p->parent->mema.bar, p->parent->mema.size,
						p->mem[i].size, p->mem[i].size);
				}
				if(pa == -1ULL)
					continue;
				p->mem[i].bar |= pa;
			}
			pcisetbar(p, PciBAR0 + i*4, p->mem[i].bar);
			DBG("%s: bar%d: fixed %.8lluX %d\n", tag, i, p->mem[i].bar, p->mem[i].size);
		}
	}
}

void
pcicfginit(void)
{
	char *p;
	Pcidev **list;
	int bno, n, pcibios;

	fmtinstall('T', tbdffmt);

	pcibios = 0;
	if(getconf("*nobios"))
		nobios = 1;
	else if(getconf("*pcibios"))
		pcibios = 1;
	if(getconf("*nopcirouting"))
		nopcirouting = 1;

	/*
	 * Try to determine which PCI configuration mode is implemented.
	 * Mode2 uses a byte at 0xCF8 and another at 0xCFA; Mode1 uses
	 * a DWORD at 0xCF8 and another at 0xCFC and will pass through
	 * any non-DWORD accesses as normal I/O cycles. There shouldn't be
	 * a device behind these addresses so if Mode1 accesses fail try
	 * for Mode2 (Mode2 is deprecated).
	 */
	if(!pcibios){
		/*
		 * Bits [30:24] of PciADDR must be 0,
		 * according to the spec.
		 */
		n = inl(PciADDR);
		if(!(n & 0x7F000000)){
			outl(PciADDR, 0x80000000);
			outb(PciADDR+3, 0);
			if(inl(PciADDR) & 0x80000000){
				ioalloc(PciADDR, 4, 0, "pcicfg.addr");
				ioalloc(PciDATA, 4, 0, "pcicfg.data");

				pcicfgmode = 1;
				pcimaxdno = 31;
			}
		}
		outl(PciADDR, n);

		if(pcicfgmode < 0){
			/*
			 * The 'key' part of PciCSE should be 0.
			 */
			n = inb(PciCSE);
			if(!(n & 0xF0)){
				outb(PciCSE, 0x0E);
				if(inb(PciCSE) == 0x0E){
					ioalloc(PciCSE, 1, 0, "pcicfg.cse");
					ioalloc(PciFORWARD, 1, 0, "pcicfg.forward");
					ioalloc(0xC000, 0x1000, 0, "pcicfg.io");

					pcicfgmode = 2;
					pcimaxdno = 15;
				}
			}
			outb(PciCSE, n);
		}
	}

	if(pcicfgmode < 0 || pcibios) {
		if((pcibiossi = pcibiosinit()) == nil)
			goto out;
		pcicfgrw8 = pcicfgrw8bios;
		pcicfgrw16 = pcicfgrw16bios;
		pcicfgrw32 = pcicfgrw32bios;
		pcicfgmode = 3;
	}

	if(p = getconf("*pcimaxbno"))
		pcimaxbno = strtoul(p, 0, 0);
	if(p = getconf("*pcimaxdno")){
		n = strtoul(p, 0, 0);
		if(n < pcimaxdno)
			pcimaxdno = n;
	}

	list = &pciroot;
	for(bno = 0; bno <= pcimaxbno; bno++) {
		int sbno = bno;
		bno = pciscan(bno, list, nil);

		while(*list)
			list = &(*list)->link;

		if (sbno == 0) {
			Pcidev *pci;

			/*
			  * If we have found a PCI-to-Cardbus bridge, make sure
			  * it has no valid mappings anymore.
			  */
			for(pci = pciroot; pci != nil; pci = pci->link){
				if (pci->ccrb == 6 && pci->ccru == 7) {
					ushort bcr;

					/* reset the cardbus */
					bcr = pcicfgr16(pci, PciBCR);
					pcicfgw16(pci, PciBCR, 0x40 | bcr);
					delay(50);
				}
			}
		}
	}

	if(pciroot == nil)
		goto out;

	/*
	 * Disabling devices here (by clearing bus master enable)
	 * causes problems with with some OHCI USB controllers.
	 * I supected that this is due to legacy device emulation
	 * and revoking bus master flag before executing the handoff
	 * makes BIOS/SMM lock up the system.
	 *
	 * pcireset();
	 */

	if(nobios) {
		uvlong mema;
		ulong ioa;

		/*
		 * Work out how big the top bus is
		 */
		pcibussize(pciroot, &mema, &ioa);
		DBG("Size:  mem=%.8llux io=%lux\n", mema, ioa);

		/*
		 * Align the windows and map it
		 */
		mema = upaalloc(-1ULL, mema, mema);
		if(mema == -1ULL)
			panic("pcicfginit: can't allocate pci mem window");

		ioa = ioreserve(-1, ioa, ioa, "pci");
		if(ioa == -1UL)
			panic("pcicfginit: can't allocate pci io window");

		DBG("Base:  mem=%.8llux io=%lux\n", mema, ioa);
		pcibusmap(pciroot, &mema, &ioa, 1);
		DBG("Limit: mem=%.8llux io=%lux\n", mema, ioa);
		goto out;
	}

	pcireserve();

	if(!nopcirouting)
		pcirouting();

out:
	if(getconf("*pcihinv"))
		pcihinv(pciroot);
}
