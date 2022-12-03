/*
 *  I/O interface for usb XHCI controller.
 */

typedef struct Xhci Xhci;
struct Xhci
{
	u32int	*mmio;
	u64int	base;
	u64int	size;

	void	*aux;
	void	(*dmaenable)(Xhci*);
	u64int	(*dmaaddr)(void*);

	Hci	*active;
};

Xhci* xhcialloc(u32int *mmio, u64int base, u64int size);
void xhcilinkage(Hci *hp, Xhci *ctlr);

void xhciinit(Hci *hp);
void xhcishutdown(Hci *hp);
