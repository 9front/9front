#include "../bcm/io.h"

enum {
	IRQgic		= 160,
	IRQpci		= IRQgic + 20,
	IRQether	= IRQgic + 29,
};

#define PCIWINDOW	soc.pcidmawin
#define PCIWADDR(va)	(PADDR(va)+PCIWINDOW)
