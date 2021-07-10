#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"

typedef struct Pcisiz Pcisiz;
struct Pcisiz
{
	Pcidev*	dev;
	int	siz;
	int	bar;
	int	typ;
};

int pcimaxdno;

static Lock pcicfglock;
static Pcidev *pcilist, **pcitail;

static char* bustypes[] = {
	"CBUSI",
	"CBUSII",
	"EISA",
	"FUTURE",
	"INTERN",
	"ISA",
	"MBI",
	"MBII",
	"MCA",
	"MPI",
	"MPSA",
	"NUBUS",
	"PCI",
	"PCMCIA",
	"TC",
	"VL",
	"VME",
	"XPRESS",
};

int
tbdffmt(Fmt* fmt)
{
	int type, tbdf;

	switch(fmt->r){
	default:
		return fmtstrcpy(fmt, "(tbdffmt)");

	case 'T':
		tbdf = va_arg(fmt->args, int);
		if(tbdf == BUSUNKNOWN) {
			return fmtstrcpy(fmt, "unknown");
		} else {
			type = BUSTYPE(tbdf);
			if(type < nelem(bustypes)) {
				return fmtprint(fmt, "%s.%d.%d.%d",
					bustypes[type], BUSBNO(tbdf), BUSDNO(tbdf), BUSFNO(tbdf));
			} else {
				return fmtprint(fmt, "%d.%d.%d.%d",
					type, BUSBNO(tbdf), BUSDNO(tbdf), BUSFNO(tbdf));
			}
		}
	}
}

static Pcidev*
pcidevalloc(void)
{
	Pcidev *p;

	p = xalloc(sizeof(*p));
	if(p == nil)
		panic("pci: no memory for Pcidev");
	return p;
}

void
pcidevfree(Pcidev *p)
{
	Pcidev **l;

	if(p == nil)
		return;

	while(p->bridge != nil)
		pcidevfree(p->bridge);

	if(p->parent != nil){
		for(l = &p->parent->bridge; *l != nil; l = &(*l)->link) {
			if(*l == p) {
				*l = p->link;
				break;
			}
		}
	}
	for(l = &pcilist; *l != nil; l = &(*l)->list) {
		if(*l == p) {
			if((*l = p->list) == nil)
				pcitail = l;
			break;
		}
	}
	/* leaked */
}

int
pcicfgr8(Pcidev* p, int rno)
{
	int data;

	ilock(&pcicfglock);
	data = pcicfgrw8(p->tbdf, rno, 0, 1);
	iunlock(&pcicfglock);

	return data;
}
void
pcicfgw8(Pcidev* p, int rno, int data)
{
	ilock(&pcicfglock);
	pcicfgrw8(p->tbdf, rno, data, 0);
	iunlock(&pcicfglock);
}
int
pcicfgr16(Pcidev* p, int rno)
{
	int data;

	ilock(&pcicfglock);
	data = pcicfgrw16(p->tbdf, rno, 0, 1);
	iunlock(&pcicfglock);

	return data;
}
void
pcicfgw16(Pcidev* p, int rno, int data)
{
	ilock(&pcicfglock);
	pcicfgrw16(p->tbdf, rno, data, 0);
	iunlock(&pcicfglock);
}
int
pcicfgr32(Pcidev* p, int rno)
{
	int data;

	ilock(&pcicfglock);
	data = pcicfgrw32(p->tbdf, rno, 0, 1);
	iunlock(&pcicfglock);

	return data;
}
void
pcicfgw32(Pcidev* p, int rno, int data)
{
	ilock(&pcicfglock);
	pcicfgrw32(p->tbdf, rno, data, 0);
	iunlock(&pcicfglock);
}

int
pcibarsize(Pcidev *p, int rno)
{
	int v, size;

	ilock(&pcicfglock);
	v = pcicfgrw32(p->tbdf, rno, 0, 1);
	pcicfgrw32(p->tbdf, rno, -1, 0);
	size = pcicfgrw32(p->tbdf, rno, 0, 1);
	pcicfgrw32(p->tbdf, rno, v, 0);
	iunlock(&pcicfglock);

	if(rno == PciEBAR0 || rno == PciEBAR1){
		size &= ~0x7FF;
	} else if(v & 1){
		size = (short)size;
		size &= ~3;
	} else {
		size &= ~0xF;
	}

	return -size;
}

void
pcisetbar(Pcidev *p, int rno, uvlong bar)
{
	ilock(&pcicfglock);
	pcicfgrw32(p->tbdf, rno, bar, 0);
	if((bar&7) == 4 && rno >= PciBAR0 && rno < PciBAR0+4*(nelem(p->mem)-1))
		pcicfgrw32(p->tbdf, rno+4, bar>>32, 0);
	iunlock(&pcicfglock);
}

void
pcisetwin(Pcidev *p, uvlong base, uvlong limit)
{
	ilock(&pcicfglock);
	if(base & 1){
		pcicfgrw16(p->tbdf, PciIBR, (limit & 0xF000)|((base & 0xF000)>>8), 0);
		pcicfgrw32(p->tbdf, PciIUBR, (limit & 0xFFFF0000)|(base>>16), 0);
	} else if(base & 8){
		pcicfgrw32(p->tbdf, PciPMBR, (limit & 0xFFF00000)|((base & 0xFFF00000)>>16), 0);
		pcicfgrw32(p->tbdf, PciPUBR, base >> 32, 0);
		pcicfgrw32(p->tbdf, PciPULR, limit >> 32, 0);
	} else {
		pcicfgrw32(p->tbdf, PciMBR, (limit & 0xFFF00000)|((base & 0xFFF00000)>>16), 0);
	}
	iunlock(&pcicfglock);
}

static int
pcisizcmp(void *a, void *b)
{
	Pcisiz *aa, *bb;

	aa = a;
	bb = b;
	return aa->siz - bb->siz;
}

static ulong
pcimask(ulong v)
{
	ulong m;

	m = BI2BY*sizeof(v);
	for(m = 1<<(m-1); m != 0; m >>= 1) {
		if(m & v)
			break;
	}

	m--;
	if((v & m) == 0)
		return v;

	v |= m;
	return v+1;
}

void
pcibusmap(Pcidev *root, uvlong *pmema, ulong *pioa, int wrreg)
{
	Pcidev *p;
	int ntb, i, size, rno, hole;
	uvlong mema, smema;
	ulong ioa, sioa, v;
	Pcisiz *table, *tptr, *mtb, *itb;

	ioa = *pioa;
	mema = *pmema;

	ntb = 0;
	for(p = root; p != nil; p = p->link)
		ntb++;

	ntb *= (PciCIS-PciBAR0)/4;
	table = malloc((2*ntb+1)*sizeof(Pcisiz));
	if(table == nil)
		panic("pcibusmap: can't allocate memory");
	itb = table;
	mtb = table+ntb;

	/*
	 * Build a table of sizes
	 */
	for(p = root; p != nil; p = p->link) {
		if(p->ccrb == 0x06) {
			/* carbus bridge? */
			if(p->ccru == 0x07){
				if(pcicfgr32(p, PciBAR0) & 1)
					continue;
				size = pcibarsize(p, PciBAR0);
				if(size == 0)
					continue;
				mtb->dev = p;
				mtb->bar = 0;
				mtb->siz = size;
				mtb->typ = 0;
				mtb++;
				continue;
			}

			/* pci bridge? */
			if(p->ccru != 0x04 || p->bridge == nil)
				continue;

			sioa = ioa;
			smema = mema;
			pcibusmap(p->bridge, &smema, &sioa, 0);

			hole = pcimask(sioa-ioa);
			if(hole < (1<<12))
				hole = 1<<12;
			itb->dev = p;
			itb->bar = -1;
			itb->siz = hole;
			itb->typ = 0;
			itb++;

			hole = pcimask(smema-mema);
			if(hole < (1<<20))
				hole = 1<<20;
			mtb->dev = p;
			mtb->bar = -1;
			mtb->siz = hole;
			mtb->typ = 0;
			mtb++;

			size = pcibarsize(p, PciEBAR1);
			if(size != 0){
				mtb->dev = p;
				mtb->bar = -3;
				mtb->siz = size;
				mtb->typ = 0;
				mtb++;
			}
			continue;
		}

		size = pcibarsize(p, PciEBAR0);
		if(size != 0){
			mtb->dev = p;
			mtb->bar = -2;
			mtb->siz = size;
			mtb->typ = 0;
			mtb++;
		}

		for(i = 0; i < nelem(p->mem); i++) {
			rno = PciBAR0 + i*4;
			v = pcicfgr32(p, rno);
			size = pcibarsize(p, rno);
			if(size == 0)
				continue;
			if(v & 1) {
				itb->dev = p;
				itb->bar = i;
				itb->siz = size;
				itb->typ = 1;
				itb++;
			} else {
				mtb->dev = p;
				mtb->bar = i;
				mtb->siz = size;
				mtb->typ = v & 7;
				if(mtb->typ & 4)
					i++;
				mtb++;
			}
		}
	}

	/*
	 * Sort both tables IO smallest first, Memory largest
	 */
	qsort(table, itb-table, sizeof(Pcisiz), pcisizcmp);
	tptr = table+ntb;
	qsort(tptr, mtb-tptr, sizeof(Pcisiz), pcisizcmp);

	/*
	 * Allocate IO address space on this bus
	 */
	for(tptr = table; tptr < itb; tptr++) {
		hole = tptr->siz;
		if(tptr->bar == -1)
			hole = 1<<12;
		ioa = (ioa+hole-1) & ~(hole-1);
		if(wrreg){
			p = tptr->dev;
			if(tptr->bar == -1) {
				p->ioa.bar = ioa;
				p->ioa.size = tptr->siz;
			} else {
				p->mem[tptr->bar].size = tptr->siz;
				p->mem[tptr->bar].bar = ioa|1;
				pcisetbar(p, PciBAR0+tptr->bar*4, p->mem[tptr->bar].bar);
			}
		}
		ioa += tptr->siz;
	}

	/*
	 * Allocate Memory address space on this bus
	 */
	for(tptr = table+ntb; tptr < mtb; tptr++) {
		hole = tptr->siz;
		if(tptr->bar == -1)
			hole = 1<<20;
		mema = (mema+hole-1) & ~((uvlong)hole-1);
		if(wrreg){
			p = tptr->dev;
			if(tptr->bar == -1) {
				p->mema.bar = mema;
				p->mema.size = tptr->siz;
			} else if(tptr->bar == -2) {
				p->rom.bar = mema|1;
				p->rom.size = tptr->siz;
				pcisetbar(p, PciEBAR0, p->rom.bar);
			} else if(tptr->bar == -3) {
				p->rom.bar = mema|1;
				p->rom.size = tptr->siz;
				pcisetbar(p, PciEBAR1, p->rom.bar);
			} else {
				p->mem[tptr->bar].size = tptr->siz;
				p->mem[tptr->bar].bar = mema|tptr->typ;
				pcisetbar(p, PciBAR0+tptr->bar*4, p->mem[tptr->bar].bar);
			}
		}
		mema += tptr->siz;
	}

	*pmema = mema;
	*pioa = ioa;
	free(table);

	if(wrreg == 0)
		return;

	/*
	 * Finally set all the bridge addresses & registers
	 */
	for(p = root; p != nil; p = p->link) {
		if(p->bridge == nil) {
			pcienable(p);
			continue;
		}

		/* Set I/O and Mem windows */
		pcisetwin(p, p->ioa.bar|1, p->ioa.bar+p->ioa.size-1);
		pcisetwin(p, p->mema.bar|0, p->mema.bar+p->mema.size-1);

		/* Disable prefetch */
		pcisetwin(p, 0xFFF00000|8, 0);

		/* Enable the bridge */
		pcienable(p);

		sioa = p->ioa.bar;
		smema = p->mema.bar;
		pcibusmap(p->bridge, &smema, &sioa, 1);
	}
}

static int
pcivalidwin(Pcidev *p, uvlong base, uvlong limit)
{
	Pcidev *bridge = p->parent;
	char *typ;

	if(base & 1){
		typ = "io";
		base &= ~3;
		if(base > limit)
			return 0;
		if(bridge == nil)
			return 1;
		if(base >= bridge->ioa.bar && limit < (bridge->ioa.bar + bridge->ioa.size))
			return 1;
	} else {
		typ = "mem";
		base &= ~0xFULL;
		if(base > limit)
			return 0;
		if(bridge == nil)
			return 1;
		if(base >= bridge->mema.bar && limit < (bridge->mema.bar + bridge->mema.size))
			return 1;
		if(base >= bridge->prefa.bar && limit < (bridge->prefa.bar + bridge->prefa.size))
			return 1;
	}
	print("%T: %.2uX invalid %s-window: %.8llux-%.8llux\n", p->tbdf, p->ccrb, typ, base, limit);
	return 0;
}

static int
pcivalidbar(Pcidev *p, uvlong bar, int size)
{
	if(bar & 1){
		bar &= ~3;
		if(bar == 0 || size < 4 || (bar & (size-1)) != 0)
			return 0;
		return pcivalidwin(p, bar|1, bar+size-1);
	} else {
		bar &= ~0xFULL;
		if(bar == 0 || size < 16 || (bar & (size-1)) != 0)
			return 0;
		return pcivalidwin(p, bar|0, bar+size-1);
	}
}

int
pciscan(int bno, Pcidev** list, Pcidev *parent)
{
	Pcidev *p, *head, **tail;
	int dno, fno, i, hdt, l, maxfno, maxubn, rno, sbn, tbdf, ubn;

	maxubn = bno;
	head = nil;
	tail = nil;
	for(dno = 0; dno <= pcimaxdno; dno++){
		maxfno = 0;
		for(fno = 0; fno <= maxfno; fno++){
			/*
			 * For this possible device, form the
			 * bus+device+function triplet needed to address it
			 * and try to read the vendor and device ID.
			 * If successful, allocate a device struct and
			 * start to fill it in with some useful information
			 * from the device's configuration space.
			 */
			tbdf = MKBUS(BusPCI, bno, dno, fno);

			lock(&pcicfglock);
			l = pcicfgrw32(tbdf, PciVID, 0, 1);
			unlock(&pcicfglock);

			if(l == 0xFFFFFFFF || l == 0)
				continue;
			p = pcidevalloc();
			p->tbdf = tbdf;
			p->vid = l;
			p->did = l>>16;

			p->pcr = pcicfgr16(p, PciPCR);
			p->rid = pcicfgr8(p, PciRID);
			p->ccrp = pcicfgr8(p, PciCCRp);
			p->ccru = pcicfgr8(p, PciCCRu);
			p->ccrb = pcicfgr8(p, PciCCRb);
			p->cls = pcicfgr8(p, PciCLS);
			p->ltr = pcicfgr8(p, PciLTR);
			p->intl = pcicfgr8(p, PciINTL);

			/*
			 * If the device is a multi-function device adjust the
			 * loop count so all possible functions are checked.
			 */
			hdt = pcicfgr8(p, PciHDT);
			if(hdt & 0x80)
				maxfno = MaxFNO;

			/*
			 * If appropriate, read the base address registers
			 * and work out the sizes.
			 */
			switch(p->ccrb) {
			case 0x00:		/* prehistoric */
			case 0x01:		/* mass storage controller */
			case 0x02:		/* network controller */
			case 0x03:		/* display controller */
			case 0x04:		/* multimedia device */
			case 0x07:		/* simple comm. controllers */
			case 0x08:		/* base system peripherals */
			case 0x09:		/* input devices */
			case 0x0A:		/* docking stations */
			case 0x0B:		/* processors */
			case 0x0C:		/* serial bus controllers */
			case 0x0D:		/* wireless controllers */
			case 0x0E:		/* intelligent I/O controllers */
			case 0x0F:		/* sattelite communication controllers */
			case 0x10:		/* encryption/decryption controllers */
			case 0x11:		/* signal processing controllers */
				if((hdt & 0x7F) != 0)
					break;
				rno = PciBAR0;
				for(i = 0; i < nelem(p->mem); i++) {
					p->mem[i].bar = (ulong)pcicfgr32(p, rno);
					p->mem[i].size = pcibarsize(p, rno);
					if((p->mem[i].bar & 7) == 4 && i < nelem(p->mem)-1){
						rno += 4;
						p->mem[i++].bar |= (uvlong)pcicfgr32(p, rno) << 32;
						p->mem[i].bar = 0;
						p->mem[i].size = 0;
					}
					rno += 4;
				}

				p->rom.bar = (ulong)pcicfgr32(p, PciEBAR0);
				p->rom.size = pcibarsize(p, PciEBAR0);
				break;

			case 0x06:		/* bridge device */
				/* cardbus bridge? */
				if(p->ccru == 0x07){
					p->mem[0].bar = (ulong)pcicfgr32(p, PciBAR0);
					p->mem[0].size = pcibarsize(p, PciBAR0);
					break;
				}

				/* pci bridge? */
				if(p->ccru != 0x04)
					break;

				p->rom.bar = (ulong)pcicfgr32(p, PciEBAR1);
				p->rom.size = pcibarsize(p, PciEBAR1);
				break;
			case 0x05:		/* memory controller */
			default:
				break;
			}

			p->parent = parent;
			if(head != nil)
				*tail = p;
			else
				head = p;
			tail = &p->link;

			if(pcilist != nil)
				*pcitail = p;
			else
				pcilist = p;
			pcitail = &p->list;
		}
	}

	*list = head;
	for(p = head; p != nil; p = p->link){
		/*
		 * Find PCI-PCI bridges and recursively descend the tree.
		 */
		switch(p->ccrb) {
		case 0x06:
			if(p->ccru == 0x04)
				break;
		default:
			/* check and clear invalid membars for non bridges */
			for(i = 0; i < nelem(p->mem); i++) {
				if(p->mem[i].size == 0)
					continue;
				if(!pcivalidbar(p, p->mem[i].bar, p->mem[i].size)){
					if(p->mem[i].bar & 1)
						p->mem[i].bar &= 3;
					else
						p->mem[i].bar &= 0xF;
					pcisetbar(p, PciBAR0 + i*4, p->mem[i].bar);
				}
			}
			if(p->rom.size) {
				if((p->rom.bar & 1) == 0
				|| !pcivalidbar(p, p->rom.bar & ~0x7FFULL, p->rom.size)){
					p->rom.bar = 0;
					pcisetbar(p, PciEBAR0, p->rom.bar);
				}
			}
			continue;
		}

		if(p->rom.size) {
			if((p->rom.bar & 1) == 0
			|| !pcivalidbar(p, p->rom.bar & ~0x7FFULL, p->rom.size)){
				p->rom.bar = 0;
				pcisetbar(p, PciEBAR1, p->rom.bar);
			}
		}

		/*
		 * If the secondary or subordinate bus number is not
		 * initialised try to do what the PCI BIOS should have
		 * done and fill in the numbers as the tree is descended.
		 * On the way down the subordinate bus number is set to
		 * the maximum as it's not known how many buses are behind
		 * this one; the final value is set on the way back up.
		 */
		sbn = pcicfgr8(p, PciSBN);
		ubn = pcicfgr8(p, PciUBN);

		if(sbn == 0 || ubn == 0) {
			sbn = maxubn+1;
			/*
			 * Make sure memory, I/O and master enables are
			 * off, set the primary, secondary and subordinate
			 * bus numbers and clear the secondary status before
			 * attempting to scan the secondary bus.
			 *
			 * Initialisation of the bridge should be done here.
			 */
			p->pcr = 0;
			pcicfgw32(p, PciPCR, 0xFFFF0000);
			l = (MaxUBN<<16)|(sbn<<8)|bno;
			pcicfgw32(p, PciPBN, l);
			pcicfgw16(p, PciSPSR, 0xFFFF);

			p->ioa.bar = 0;
			p->ioa.size = 0;
			p->mema.bar = 0;
			p->mema.size = 0;
			p->prefa.bar = 0;
			p->prefa.size = 0;

			pcisetwin(p, 0xFFFFF000|1, 0);
			pcisetwin(p, 0xFFF00000|0, 0);
			pcisetwin(p, 0xFFF00000|8, 0);

			maxubn = pciscan(sbn, &p->bridge, p);
			l = (maxubn<<16)|(sbn<<8)|bno;

			pcicfgw32(p, PciPBN, l);
		}
		else {
			uvlong base, limit;
			ulong v;

			v = pcicfgr16(p, PciIBR);
			limit = (v & 0xF000) | 0x0FFF;
			base  = (v & 0x00F0) << 8;
			if((v & 0x0F) == 0x01){
				v = pcicfgr32(p, PciIUBR);
				limit |= (v & 0xFFFF0000);
				base  |= (v & 0x0000FFFF) << 16;
			}
			if(pcivalidwin(p, base|1, limit)){
				p->ioa.bar = base;
				p->ioa.size = (limit - base)+1;
			} else {
				pcisetwin(p, 0xFFFFF000|1, 0);
				p->ioa.bar = 0;
				p->ioa.size = 0;
			}

			v = pcicfgr32(p, PciMBR);
			limit = (v & 0xFFF00000) | 0x000FFFFF;
			base  = (v & 0x0000FFF0) << 16;
			if(pcivalidwin(p, base|0, limit)){
				p->mema.bar = base;
				p->mema.size = (limit - base)+1;
			} else {
				pcisetwin(p, 0xFFF00000|0, 0);
				p->mema.bar = 0;
				p->mema.size = 0;
			}

			v = pcicfgr32(p, PciPMBR);
			limit = (v & 0xFFF00000) | 0x000FFFFF;
			limit |= (uvlong)pcicfgr32(p, PciPULR) << 32;
			base  = (v & 0x0000FFF0) << 16;
			base  |= (uvlong)pcicfgr32(p, PciPUBR) << 32;
			if(pcivalidwin(p, base|8, limit)){
				p->prefa.bar = base;
				p->prefa.size = (limit - base)+1;
			} else {
				pcisetwin(p, 0xFFF00000|8, 0);
				p->prefa.bar = 0;
				p->prefa.size = 0;
			}

			if(ubn > maxubn)
				maxubn = ubn;
			pciscan(sbn, &p->bridge, p);
		}
	}

	return maxubn;
}

void
pcibussize(Pcidev *root, uvlong *msize, ulong *iosize)
{
	*msize = 0;
	*iosize = 0;
	pcibusmap(root, msize, iosize, 0);
}

Pcidev*
pcimatch(Pcidev* prev, int vid, int did)
{
	if(prev == nil)
		prev = pcilist;
	else
		prev = prev->list;

	while(prev != nil){
		if((vid == 0 || prev->vid == vid)
		&& (did == 0 || prev->did == did))
			break;
		prev = prev->list;
	}
	return prev;
}

Pcidev*
pcimatchtbdf(int tbdf)
{
	Pcidev *pcidev;

	for(pcidev = pcilist; pcidev != nil; pcidev = pcidev->list) {
		if(pcidev->tbdf == tbdf)
			break;
	}
	return pcidev;
}

uchar
pciipin(Pcidev *pci, uchar pin)
{
	if (pci == nil)
		pci = pcilist;

	while (pci != nil) {
		uchar intl;

		if (pcicfgr8(pci, PciINTP) == pin && pci->intl != 0 && pci->intl != 0xff)
			return pci->intl;

		if (pci->bridge && (intl = pciipin(pci->bridge, pin)) != 0)
			return intl;

		pci = pci->list;
	}
	return 0;
}

static void
pcilhinv(Pcidev* p)
{
	int i;
	Pcidev *t;

	for(t = p; t != nil; t = t->link) {
		print("%d  %2d/%d %.2ux %.2ux %.2ux %.4ux %.4ux %3d  ",
			BUSBNO(t->tbdf), BUSDNO(t->tbdf), BUSFNO(t->tbdf),
			t->ccrb, t->ccru, t->ccrp, t->vid, t->did, t->intl);
		for(i = 0; i < nelem(p->mem); i++) {
			if(t->mem[i].size == 0)
				continue;
			print("%d:%.8llux %d ", i, t->mem[i].bar, t->mem[i].size);
		}
		if(t->rom.bar || t->rom.size)
			print("rom:%.8llux %d ", t->rom.bar, t->rom.size);
		if(t->ioa.bar || t->ioa.size)
			print("ioa:%.8llux-%.8llux %d ", t->ioa.bar, t->ioa.bar+t->ioa.size, t->ioa.size);
		if(t->mema.bar || t->mema.size)
			print("mema:%.8llux-%.8llux %d ", t->mema.bar, t->mema.bar+t->mema.size, t->mema.size);
		if(t->prefa.bar || t->prefa.size)
			print("prefa:%.8llux-%.8llux %llud ", t->prefa.bar, t->prefa.bar+t->prefa.size, t->prefa.size);
		if(t->bridge)
			print("->%d", BUSBNO(t->bridge->tbdf));
		print("\n");
	}
	while(p != nil) {
		if(p->bridge != nil)
			pcilhinv(p->bridge);
		p = p->link;
	}
}

void
pcihinv(Pcidev* p)
{
	print("bus dev type     vid  did  intl memory\n");
	pcilhinv(p);
}

void
pcireset(void)
{
	Pcidev *p;

	for(p = pcilist; p != nil; p = p->list) {
		/* don't mess with the bridges */
		if(p->ccrb == 0x06)
			continue;
		pcidisable(p);
	}
}

void
pcisetioe(Pcidev* p)
{
	p->pcr |= IOen;
	pcicfgw16(p, PciPCR, p->pcr);
}

void
pciclrioe(Pcidev* p)
{
	p->pcr &= ~IOen;
	pcicfgw16(p, PciPCR, p->pcr);
}

void
pcisetbme(Pcidev* p)
{
	p->pcr |= MASen;
	pcicfgw16(p, PciPCR, p->pcr);
}

void
pciclrbme(Pcidev* p)
{
	p->pcr &= ~MASen;
	pcicfgw16(p, PciPCR, p->pcr);
}

void
pcisetmwi(Pcidev* p)
{
	p->pcr |= MemWrInv;
	pcicfgw16(p, PciPCR, p->pcr);
}

void
pciclrmwi(Pcidev* p)
{
	p->pcr &= ~MemWrInv;
	pcicfgw16(p, PciPCR, p->pcr);
}

int
pcienumcaps(Pcidev *p, int (*fmatch)(Pcidev*, int, int, int), int arg)
{
	int i, r, cap, off;

	/* status register bit 4 has capabilities */
	if((pcicfgr16(p, PciPSR) & 1<<4) == 0)
		return -1;      
	switch(pcicfgr8(p, PciHDT) & 0x7F){
	default:
		return -1;
	case 0:                         /* etc */
	case 1:                         /* pci to pci bridge */
		off = 0x34;
		break;
	case 2:                         /* cardbus bridge */
		off = 0x14;
		break;
	}
	for(i = 48; i--;){
		off = pcicfgr8(p, off);
		if(off < 0x40 || (off & 3))
			break;
		off &= ~3;
		cap = pcicfgr8(p, off);
		if(cap == 0xff)
			break;
		r = (*fmatch)(p, cap, off, arg);
		if(r < 0)
			break;
		if(r == 0)
			return off;
		off++;
	}
	return -1;
}

static int
matchcap(Pcidev *, int cap, int, int arg)
{
	return cap != arg;
}

static int
matchhtcap(Pcidev *p, int cap, int off, int arg)
{
	int mask;

	if(cap != PciCapHTC)
		return 1;
	if(arg == 0x00 || arg == 0x20)
		mask = 0xE0;
	else
		mask = 0xF8;
	cap = pcicfgr8(p, off+3);
	return (cap & mask) != arg;
}

int
pcicap(Pcidev *p, int cap)
{
	return pcienumcaps(p, matchcap, cap);
}

int
pcihtcap(Pcidev *p, int cap)
{
	return pcienumcaps(p, matchhtcap, cap);
}

static int
pcigetmsi(Pcidev *p)
{
	if(p->msi != 0)
		return p->msi;
	return p->msi = pcicap(p, PciCapMSI);
}

enum {
	MSICtrl = 0x02, /* message control register (16 bit) */
	MSIAddr = 0x04, /* message address register (64 bit) */
	MSIData32 = 0x08, /* message data register for 32 bit MSI (16 bit) */
	MSIData64 = 0x0C, /* message data register for 64 bit MSI (16 bit) */
};

int
pcimsienable(Pcidev *p, uvlong addr, ulong data)
{
	int off, ok64;

	if((off = pcigetmsi(p)) < 0)
		return -1;
	ok64 = (pcicfgr16(p, off + MSICtrl) & (1<<7)) != 0;
	pcicfgw32(p, off + MSIAddr, addr);
	if(ok64) pcicfgw32(p, off + MSIAddr+4, addr >> 32);
	pcicfgw16(p, off + (ok64 ? MSIData64 : MSIData32), data);
	pcicfgw16(p, off + MSICtrl, 1);
	return 0;
}

int
pcimsidisable(Pcidev *p)
{
	int off;

	if((off = pcigetmsi(p)) < 0)
		return -1;
	pcicfgw16(p, off + MSICtrl, 0);
	return 0;
}

enum {
	MSIXCtrl = 0x02,
};

static int
pcimsixdisable(Pcidev *p)
{
	int off;

	if((off = pcicap(p, PciCapMSIX)) < 0)
		return -1;
	pcicfgw16(p, off + MSIXCtrl, 0);
	return 0;
}

static int
pcigetpmrb(Pcidev *p)
{
        if(p->pmrb != 0)
                return p->pmrb;
        return p->pmrb = pcicap(p, PciCapPMG);
}

int
pcigetpms(Pcidev* p)
{
	int pmcsr, ptr;

	if((ptr = pcigetpmrb(p)) == -1)
		return -1;

	/*
	 * Power Management Register Block:
	 *  offset 0:	Capability ID
	 *	   1:	next item pointer
	 *	   2:	capabilities
	 *	   4:	control/status
	 *	   6:	bridge support extensions
	 *	   7:	data
	 */
	pmcsr = pcicfgr16(p, ptr+4);

	return pmcsr & 0x0003;
}

int
pcisetpms(Pcidev* p, int state)
{
	int ostate, pmc, pmcsr, ptr;

	if((ptr = pcigetpmrb(p)) == -1)
		return -1;

	pmc = pcicfgr16(p, ptr+2);
	pmcsr = pcicfgr16(p, ptr+4);
	ostate = pmcsr & 0x0003;
	pmcsr &= ~0x0003;

	switch(state){
	default:
		return -1;
	case 0:
		break;
	case 1:
		if(!(pmc & 0x0200))
			return -1;
		break;
	case 2:
		if(!(pmc & 0x0400))
			return -1;
		break;
	case 3:
		break;
	}
	pmcsr |= state;
	pcicfgw16(p, ptr+4, pmcsr);

	return ostate;
}

void
pcienable(Pcidev *p)
{
	uint pcr;
	int i;

	if(p == nil)
		return;

	pcienable(p->parent);

	switch(pcisetpms(p, 0)){
	case 1:
		print("pcienable %T: wakeup from D1\n", p->tbdf);
		break;
	case 2:
		print("pcienable %T: wakeup from D2\n", p->tbdf);
		if(p->bridge != nil)
			delay(100);	/* B2: minimum delay 50ms */
		else
			delay(1);	/* D2: minimum delay 200µs */
		break;
	case 3:
		print("pcienable %T: wakeup from D3\n", p->tbdf);
		delay(100);		/* D3: minimum delay 50ms */

		/* restore registers */
		for(i = 0; i < nelem(p->mem); i++){
			if(p->mem[i].size == 0)
				continue;
			pcisetbar(p, PciBAR0+i*4, p->mem[i].bar);
		}

		pcicfgw8(p, PciINTL, p->intl);
		pcicfgw8(p, PciLTR, p->ltr);
		pcicfgw8(p, PciCLS, p->cls);
		pcicfgw16(p, PciPCR, p->pcr);
		break;
	}

	if(p->ltr == 0 || p->ltr == 0xFF){
		p->ltr = 64;
		pcicfgw8(p,PciLTR, p->ltr);
	}
	if(p->cls == 0 || p->cls == 0xFF){
		p->cls = 64/4;
		pcicfgw8(p, PciCLS, p->cls);
	}

	if(p->bridge != nil)
		pcr = IOen|MEMen|MASen;
	else {
		pcr = 0;
		for(i = 0; i < nelem(p->mem); i++){
			if(p->mem[i].size == 0)
				continue;
			if(p->mem[i].bar & 1)
				pcr |= IOen;
			else
				pcr |= MEMen;
		}
	}

	if((p->pcr & pcr) != pcr){
		print("pcienable %T: pcr %ux->%ux\n", p->tbdf, p->pcr, p->pcr|pcr);
		p->pcr |= pcr;
		pcicfgw32(p, PciPCR, 0xFFFF0000|p->pcr);
	}
}

void
pcidisable(Pcidev *p)
{
	if(p == nil)
		return;
	pcimsixdisable(p);
	pcimsidisable(p);
	pciclrbme(p);
}
