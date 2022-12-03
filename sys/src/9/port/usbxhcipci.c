#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/pci.h"
#include	"../port/error.h"
#include	"../port/usb.h"

#include	"usbxhci.h"

static Xhci *ctlrs[Nhcis];

static void
pcidmaenable(Xhci *ctlr)
{
	Pcidev *pcidev = ctlr->aux;
	pcisetbme(pcidev);
}

static u64int
pciwaddr(void *va)
{
	return PCIWADDR(va);
}

static void
scanpci(void)
{
	static int already = 0;
	int i;
	u64int io, iosize;
	Xhci *ctlr;
	Pcidev *p;
	u32int *mmio; 

	if(already)
		return;
	already = 1;
	p = nil;
	while ((p = pcimatch(p, 0, 0)) != nil) {
		/*
		 * Find XHCI controllers (Programming Interface = 0x30).
		 */
		if(p->ccrb != Pcibcserial || p->ccru != Pciscusb || p->ccrp != 0x30)
			continue;
		if(p->mem[0].bar & 1)
			continue;
		iosize = p->mem[0].size;
		if(iosize == 0)
			continue;
		io = p->mem[0].bar & ~0x0f;
		if(io == 0)
			continue;
		print("usbxhci: %#x %#x: port %llux size %lld irq %d\n",
			p->vid, p->did, io, iosize, p->intl);
		mmio = vmap(io, iosize);
		if(mmio == nil){
			print("usbxhci: cannot map registers\n");
			continue;
		}
		ctlr = xhcialloc(mmio, io, iosize);
		if(ctlr == nil){
			print("usbxhci: no memory\n");
			vunmap(mmio, iosize);
			continue;
		}
		ctlr->aux = p;
		ctlr->dmaenable = pcidmaenable;
		ctlr->dmaaddr = pciwaddr;

		for(i = 0; i < nelem(ctlrs); i++)
			if(ctlrs[i] == nil){
				ctlrs[i] = ctlr;
				break;
			}
		if(i >= nelem(ctlrs))
			print("xhci: bug: more than %d controllers\n", nelem(ctlrs));
	}
}

static void
init(Hci *hp)
{
	Xhci *ctlr = hp->aux;
	Pcidev *pcidev = ctlr->aux;

	pcienable(pcidev);
	if(ctlr->mmio[0] == -1){
		pcidisable(pcidev);
		error("controller vanished");
	}
	xhciinit(hp);
}

static void
shutdown(Hci *hp)
{
	Xhci *ctlr = hp->aux;
	Pcidev *pcidev = ctlr->aux;

	xhcishutdown(hp);
	pcidisable(pcidev);
}

static int
reset(Hci *hp)
{
	Xhci *ctlr;
	Pcidev *pcidev;
	int i;

	if(getconf("*nousbxhci"))
		return -1;

	scanpci();

	/*
	 * Any adapter matches if no hp->port is supplied,
	 * otherwise the ports must match.
	 */
	for(i = 0; i < nelem(ctlrs); i++){
		ctlr = ctlrs[i];
		if(ctlr == nil)
			break;
		if(ctlr->active == nil)
		if(hp->port == 0 || hp->port == ctlr->base)
			goto Found;
	}
	return -1;

Found:
	pcidev = ctlr->aux;
	hp->irq = pcidev->intl;
	hp->tbdf = pcidev->tbdf;

	xhcilinkage(hp, ctlr);
	hp->init = init;
	hp->shutdown = shutdown;

	return 0;
}

void
usbxhcipcilink(void)
{
	addhcitype("xhci", reset);
}
