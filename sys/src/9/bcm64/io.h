#include "../bcm/io.h"

enum {
	IRQgic		= 160,
	IRQpci		= IRQgic + 20,
	IRQether	= IRQgic + 29,
};

#define PCIWINDOW	0
#define PCIWADDR(va)	(PADDR(va)+PCIWINDOW)
