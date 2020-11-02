#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

static Pcidev *devs;

Pcidev*
pciopen(int bdf)
{
	char path[64];
	Pcidev *pci;

	for(pci = devs; pci != nil; pci = pci->next){
		if(pci->bdf == bdf){
			if(pci->fd < 0)
				return nil;
			return pci;
		}
	}

	pci = malloc(sizeof(Pcidev));
	pci->bdf = bdf;

	snprint(path, sizeof(path), "#$/pci/%d.%d.%draw",
		BDFBNO(bdf), BDFDNO(bdf), BDFFNO(bdf));
	pci->fd = open(path, ORDWR);

	pci->next = devs;
	devs = pci;

	return pci;
}

int
pcicfgr(Pcidev *pci, void *data, int len, int addr)
{
	return pread(pci->fd, data, len, addr);
}

int
pcicfgw(Pcidev *pci, void *data, int len, int addr)
{
	return pwrite(pci->fd, data, len, addr);
}
