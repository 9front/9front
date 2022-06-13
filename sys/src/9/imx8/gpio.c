#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

/* gpio registers */
enum {
	GPIO_DR = 0x00/4,
	GPIO_GDIR = 0x04/4,
	GPIO_PSR = 0x08/4,
	GPIO_ICR1 = 0x0C/4,
	GPIO_ICR2 = 0x10/4,
	GPIO_IMR = 0x14/4,
	GPIO_ISR = 0x18/4,
	GPIO_EDGE_SEL = 0x1C/4,
};

typedef struct Ctlr Ctlr;
struct Ctlr
{
	u32int	*reg;
	char	*clk;
	u32int	dir;
	int	enabled;
};

static Ctlr ctlrs[5] = {
	{(u32int*)(VIRTIO + 0x200000),	"gpio1.ipg_clk_s" },
	{(u32int*)(VIRTIO + 0x210000),	"gpio2.ipg_clk_s" },
	{(u32int*)(VIRTIO + 0x220000),	"gpio3.ipg_clk_s" },
	{(u32int*)(VIRTIO + 0x230000),	"gpio4.ipg_clk_s" },
	{(u32int*)(VIRTIO + 0x240000),	"gpio5.ipg_clk_s" },
};

static Ctlr*
enable(uint pin)
{
	Ctlr *ctlr;

	pin /= 32;
	if(pin < 1 || pin > nelem(ctlrs))
		return nil;

	ctlr = &ctlrs[pin-1];
	if(!ctlr->enabled){
		setclkgate(ctlr->clk, 1);
		ctlr->reg[GPIO_IMR] = 0;
		ctlr->dir = ctlr->reg[GPIO_GDIR];
		ctlr->enabled = 1;
	}
	return ctlr;
}

void
gpioout(uint pin, int set)
{
	int bit = 1 << (pin % 32);
	Ctlr *ctlr = enable(pin);
	if(ctlr == nil)
		return;
	if((ctlr->dir & bit) == 0)
		ctlr->reg[GPIO_GDIR] = ctlr->dir |= bit;
	if(set)
		ctlr->reg[GPIO_DR] |= bit;
	else
		ctlr->reg[GPIO_DR] &= ~bit;
}

int
gpioin(uint pin)
{
	int bit = 1 << (pin % 32);
	Ctlr *ctlr = enable(pin);
	if(ctlr == nil)
		return -1;
	if(ctlr->dir & bit)
		ctlr->reg[GPIO_GDIR] = ctlr->dir &= ~bit;
	return (ctlr->reg[GPIO_DR] & bit) != 0;
}
