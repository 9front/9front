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

typedef struct Ivec Ivec;
struct Ivec
{
	void (*f)(uint pin, void *a);
	void *a;
};

typedef struct Ctlr Ctlr;
struct Ctlr
{
	u32int	*reg;
	char	*clk;
	u32int	dir;
	int	enabled;
	Ivec	vec[32];
	Lock;
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
	u32int bit = 1 << (pin % 32);
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
	u32int bit = 1 << (pin % 32);
	Ctlr *ctlr = enable(pin);
	if(ctlr == nil)
		return -1;
	if(ctlr->dir & bit)
		ctlr->reg[GPIO_GDIR] = ctlr->dir &= ~bit;
	return (ctlr->reg[GPIO_DR] & bit) != 0;
}

void
gpiointrenable(uint pin, int mode, void (*f)(uint pin, void *a), void *a)
{
	u32int bit = 1 << (pin % 32);
	Ctlr *ctlr = enable(pin);
	if(ctlr == nil)
		return;
	ctlr->reg[GPIO_IMR] &= ~bit;

	ilock(ctlr);
	if(ctlr->dir & bit)
		ctlr->reg[GPIO_GDIR] = ctlr->dir &= ~bit;

	if(mode == GpioEdge)
		ctlr->reg[GPIO_EDGE_SEL] |= bit;
	else if(bit < 16)
		ctlr->reg[GPIO_ICR1] |= mode << (bit*2);
	else
		ctlr->reg[GPIO_ICR2] |= mode << (bit-16)*2;

	ctlr->vec[pin % 32].f = f;
	ctlr->vec[pin % 32].a = a;
	iunlock(ctlr);

	ctlr->reg[GPIO_IMR] |= bit;
}

void
gpiointrdisable(uint pin)
{
	u32int bit = 1 << (pin % 32);
	Ctlr *ctlr = enable(pin);
	if(ctlr == nil)
		return;

	ctlr->reg[GPIO_IMR] &= ~bit;

	ilock(ctlr);
	ctlr->vec[pin % 32].f = nil;
	ctlr->vec[pin % 32].a = nil;
	iunlock(ctlr);
}

static void
gpiointerrupt(Ureg *, void *arg)
{
	Ctlr *ctlr = arg;
	u32int status;
	Ivec *vec;
	int pin;

	status = ctlr->reg[GPIO_ISR];
	if(status == 0)
		return;
	ctlr->reg[GPIO_ISR] = status;

	ilock(ctlr);
	for(vec = ctlr->vec; status != 0 && vec < &ctlr->vec[nelem(ctlr->vec)]; vec++, status >>= 1){
		if((status & 1) != 0 && vec->f != nil){
			pin = (ctlr - ctlrs + 1)<<5 | (vec - ctlr->vec);
			(*vec->f)(pin, vec->a);
		}
	}
	iunlock(ctlr);
}

void
gpiolink(void)
{
	intrenable(IRQgpio1l, gpiointerrupt, &ctlrs[0], BUSUNKNOWN, "gpio1");
	intrenable(IRQgpio1h, gpiointerrupt, &ctlrs[0], BUSUNKNOWN, "gpio1");
	intrenable(IRQgpio2l, gpiointerrupt, &ctlrs[1], BUSUNKNOWN, "gpio2");
	intrenable(IRQgpio2h, gpiointerrupt, &ctlrs[1], BUSUNKNOWN, "gpio2");
	intrenable(IRQgpio3l, gpiointerrupt, &ctlrs[2], BUSUNKNOWN, "gpio3");
	intrenable(IRQgpio3h, gpiointerrupt, &ctlrs[2], BUSUNKNOWN, "gpio3");
	intrenable(IRQgpio4l, gpiointerrupt, &ctlrs[3], BUSUNKNOWN, "gpio4");
	intrenable(IRQgpio4h, gpiointerrupt, &ctlrs[3], BUSUNKNOWN, "gpio4");
	intrenable(IRQgpio5l, gpiointerrupt, &ctlrs[4], BUSUNKNOWN, "gpio5");
	intrenable(IRQgpio5h, gpiointerrupt, &ctlrs[4], BUSUNKNOWN, "gpio5");
}
