/*
 * PCI support code.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "../port/error.h"

enum {
	/* configuration mechanism #1 */
	PciADDR		= 0xCF8,	/* CONFIG_ADDRESS */
	PciDATA		= 0xCFC,	/* CONFIG_DATA */

	/* configuration mechanism #2 */
	PciCSE		= 0xCF8,	/* configuration space enable */
	PciFORWARD	= 0xCFA,	/* which bus */
};

static int pcicfgmode = -1;
static int pcimaxbno = 7;
static Pcidev* pciroot;

static void
pcicfginit(void)
{
	char *p;
	int bno;
	Pcidev **list;
	uvlong mema;
	ulong ioa;

	fmtinstall('T', tbdffmt);

	/*
	 * Try to determine which PCI configuration mode is implemented.
	 * Mode2 uses a byte at 0xCF8 and another at 0xCFA; Mode1 uses
	 * a DWORD at 0xCF8 and another at 0xCFC and will pass through
	 * any non-DWORD accesses as normal I/O cycles. There shouldn't be
	 * a device behind these addresses so if Mode2 accesses fail try
	 * for Mode1 (which is preferred, Mode2 is deprecated).
	 */
	outb(PciCSE, 0);
	if(inb(PciCSE) == 0){
		pcicfgmode = 2;
		pcimaxdno = 15;
	}
	else {
		outl(PciADDR, 0);
		if(inl(PciADDR) == 0){
			pcicfgmode = 1;
			pcimaxdno = 31;
		}
	}
	
	if(pcicfgmode < 0)
		return;

	if(p = getconf("*pcimaxbno"))
		pcimaxbno = strtoul(p, 0, 0);
	if(p = getconf("*pcimaxdno"))
		pcimaxdno = strtoul(p, 0, 0);

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
			pci = pciroot;
			while (pci) {
				if (pci->ccrb == 6 && pci->ccru == 7) {
					ushort bcr;

					/* reset the cardbus */
					bcr = pcicfgr16(pci, PciBCR);
					pcicfgw16(pci, PciBCR, 0x40 | bcr);
					delay(50);
				}
				pci = pci->link;
			}
		}
	}

	if(pciroot == nil)
		return;

	/*
	 * Work out how big the top bus is
	 */
	mema = 0;
	ioa = 0;
	pcibusmap(pciroot, &mema, &ioa, 0);

	/*
	 * Align the windows and map it
	 */
	ioa = 0x1000;
	mema = 0;
	pcibusmap(pciroot, &mema, &ioa, 1);
}

int
pcicfgrw8(int tbdf, int rno, int data, int read)
{
	int o, x;

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

int
pcicfgrw16(int tbdf, int rno, int data, int read)
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

int
pcicfgrw32(int tbdf, int rno, int data, int read)
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

void
pcimtxlink(void)
{
	pcicfginit();
}
