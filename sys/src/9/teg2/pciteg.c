#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"

typedef struct {
	ulong	cap;
	ulong	ctl;
} Capctl;

typedef struct {
	Capctl	dev;
	Capctl	link;
	Capctl	slot;
} Devlinkslot;

/* capability list id 0x10 is pci-e */
typedef struct Pci Pci;
struct Pci {
	/* pci-compatible config */
	/* what io.h calls type 0 & type 1 pre-defined header */
	ulong	id;
	ulong	cs;
	ulong	revclass;
	ulong	misc;	/* cache line size, latency timer, header type, bist */
	ulong	bar[2];		/* always 0 on tegra 2 */

	/* types 1 & 2 pre-defined header */
	ulong	bus;
	ulong	ioaddrs;
	ulong	memaddrs;
	ulong	prefmem;
	ulong	prefbasehi;
	ulong	preflimhi;
	/* type 2 pre-defined header only */
	ulong	ioaddrhi;
	ulong	cfgcapoff;	/* offset in cfg. space to cap. list (0x40) */
	ulong	rom;
	ulong	intr;		/* PciINT[LP] */
	/* subsystem capability regs */
	ulong	subsysid;
	ulong	subsyscap;
	/* */

	Capctl	pwrmgmt;

	/* msi */
	ulong	msictlcap;
	ulong	msimsgaddr[2];	/* little-endian */
	ulong	msimsgdata;

	/* pci-e cap. */
	uchar	_pad0[0x80-0x60];
	ulong	pciecap;
	Devlinkslot port0;
	ulong	rootctl;
	ulong	rootsts;
	Devlinkslot port1;

	/* 0xbc */
	
};

enum {
	/* offsets from soc.pci */
	Port0		= 0,
	Port1		= 0x1000,
	Pads		= 0x3000,
	Afi		= 0x3800,
	Aficfg		= Afi + 0xac,
	Cfgspace	= 0x4000,
	Ecfgspace	= 0x104000,

	/* cs bits */
	Iospace		= 1<<0,
	Memspace	= 1<<1,
	Busmaster	= 1<<2,

	/* Aficfg bits */
	Fpcion		= 1<<0,
};

struct Pcictlr {
	union {
		uchar	_padpci[0x1000];
		Pci;
	} ports[2];
	uchar	_padpads[0x1000];
	uchar	pads[0x800];
	uchar	afi[0x800];
	ulong	cfg[0x1000];
	ulong	extcfg[0x1000];
};

static int pcicfgmode = -1;
static int pcimaxbno = 1;  /* was 7; only 2 pci buses; touching 3rd hangs */
static Pcidev* pciroot;

extern void rtl8169interrupt(Ureg*, void* arg);

/* not used yet */
static void
pciintr(Ureg *ureg, void *p)
{
	rtl8169interrupt(ureg, p);		/* HACK */
}

static void
pcicfginit(void)
{
	char *p;
	Pci *pci = (Pci *)soc.pci;
	Pcidev **list;
	int bno, n;

	/*
	 * TrimSlice # pci 0 1
	 * Scanning PCI devices on bus 0 1
	 * BusDevFun  VendorId   DeviceId   Device Class       Sub-Class
	 * _____________________________________________________________
	 * 00.00.00   0x10de     0x0bf0     Bridge device           0x04
	 * 01.00.00   0x10ec     0x8168     Network controller      0x00
	 *
	 * thus pci bus 0 has a bridge with, perhaps, an ide/sata ctlr behind,
	 * and pci bus 1 has the realtek 8169 on it:
	 *
	 * TrimSlice # pci 1 long
	 * Scanning PCI devices on bus 1
	 *
	 * Found PCI device 01.00.00:
	 *   vendor ID =                   0x10ec
	 *   device ID =                   0x8168
	 *   command register =            0x0007
	 *   status register =             0x0010
	 *   revision ID =                 0x03
	 *   class code =                  0x02 (Network controller)
	 *   sub class code =              0x00
	 *   programming interface =       0x00
	 *   cache line =                  0x08
	 *   base address 0 =              0x80400001		config
	 *   base address 1 =              0x00000000		(ext. config)
	 *   base address 2 =              0xa000000c		"downstream"
	 *   base address 3 =              0x00000000		(prefetchable)
	 *   base address 4 =              0xa000400c		not "
	 *   base address 5 =              0x00000000		(unused)
	 */
	n = pci->id >> 16;
	if (((pci->id & MASK(16)) != Vnvidia || (n != 0xbf0 && n != 0xbf1)) &&
	     (pci->id & MASK(16)) != Vrealtek) {
		print("no pci controller at %#p\n", pci);
		return;
	}
	if (0)
		iprint("pci: %#p: nvidia, rev %#ux class %#6.6lux misc %#8.8lux\n",
			pci, (uchar)pci->revclass, pci->revclass >> 8,
			pci->misc);

	pci->cs &= Iospace;
	pci->cs |= Memspace | Busmaster;
	coherence();

	pcicfgmode = 1;
	pcimaxdno = 15;			/* for trimslice */

	fmtinstall('T', tbdffmt);

	if(p = getconf("*pcimaxbno")){
		n = strtoul(p, 0, 0);
		if(n < pcimaxbno)
			pcimaxbno = n;
	}
	if(p = getconf("*pcimaxdno")){
		n = strtoul(p, 0, 0);
		if(n < pcimaxdno)
			pcimaxdno = n;
	}

	list = &pciroot;
	/* was bno = 0; trimslice needs to start at 1 */
	for(bno = 1; bno <= pcimaxbno; bno++) {
		bno = pciscan(bno, list, nil);
		while(*list)
			list = &(*list)->link;
	}

	if(getconf("*pcihinv"))
		pcihinv(pciroot);
}

enum {
	Afiintrcode	= 0xb8,
};

void
pcieintrdone(void)				/* dismiss pci-e intr */
{
	ulong *afi;

	afi = (ulong *)(soc.pci + Afi);
	afi[Afiintrcode/sizeof *afi] = 0;	/* magic */
	coherence();
}

/*
 * whole config space for tbdf should be at (return address - rno).
 */
static void *
tegracfgaddr(int tbdf, int rno)
{
	uintptr addr;

	addr = soc.pci + (rno < 256? Cfgspace: Ecfgspace) + BUSBDF(tbdf) + rno;
//	if (BUSBNO(tbdf) == 1)
//		addr += Port1;
	return (void *)addr;
}

int
pcicfgrw8(int tbdf, int rno, int data, int read)
{
	void *addr;

	addr = tegracfgaddr(tbdf, rno);
	if(read)
		data = *(uchar *)addr;
	else
		*(uchar *)addr = data;
	return data;
}

int
pcicfgrw16(int tbdf, int rno, int data, int read)
{
	void *addr;

	addr = tegracfgaddr(tbdf, rno);
	if(read)
		data = *(ushort *)addr;
	else
		*(ushort *)addr = data;
	return data;
}

int
pcicfgrw32(int tbdf, int rno, int data, int read)
{
	vlong v;
	void *addr;

	addr = tegracfgaddr(tbdf, rno);
	v = probeaddr((uintptr)addr);
	if (v < 0)
		return -1;
	if(read)
		data = *(ulong *)addr;
	else
		*(ulong *)addr = data;
	return data;
}

void
pciteglink(void)
{
	pcicfginit();
}
