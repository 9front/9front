/*
 * Raspberry Pi GPIO support
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#define GPIOREGS	(VIRTIO+0x200000)

/* GPIO regs */
enum {
	Fsel0	= 0x00>>2,
		FuncMask= 0x7,
	Set0	= 0x1c>>2,
	Clr0	= 0x28>>2,
	Lev0	= 0x34>>2,
	Evds0	= 0x40>>2,
	Redge0	= 0x4C>>2,
	Fedge0	= 0x58>>2,
	Hpin0	= 0x64>>2,
	Lpin0	= 0x70>>2,
	ARedge0	= 0x7C>>2,
	AFedge0	= 0x88>2,
	PUD	= 0x94>>2,
		Off	= 0x0,
		Pulldown= 0x1,
		Pullup	= 0x2,
	PUDclk0	= 0x98>>2,
	PUDclk1	= 0x9c>>2,
};

void
gpiosel(uint pin, int func)
{	
	u32int *gp, *fsel;
	int off;

	gp = (u32int*)GPIOREGS;
	fsel = &gp[Fsel0 + pin/10];
	off = (pin % 10) * 3;
	*fsel = (*fsel & ~(FuncMask<<off)) | func<<off;
}

void
gpiopull(uint pin, int func)
{
	u32int *gp, *reg;
	u32int mask;

	gp = (u32int*)GPIOREGS;
	reg = &gp[PUDclk0 + pin/32];
	mask = 1 << (pin % 32);
	gp[PUD] = func;
	microdelay(1);
	*reg = mask;
	microdelay(1);
	*reg = 0;
}

void
gpiopulloff(uint pin)
{
	gpiopull(pin, Off);
}

void
gpiopullup(uint pin)
{
	gpiopull(pin, Pullup);
}

void
gpiopulldown(uint pin)
{
	gpiopull(pin, Pulldown);
}

void
gpioout(uint pin, int set)
{
	u32int *gp;
	int v;

	gp = (u32int*)GPIOREGS;
	v = set? Set0 : Clr0;
	gp[v + pin/32] = 1 << (pin % 32);
}

int
gpioin(uint pin)
{
	u32int *gp;

	gp = (u32int*)GPIOREGS;
	return (gp[Lev0 + pin/32] & (1 << (pin % 32))) != 0;
}

void
gpioselevent(uint pin, int falling, int enable)
{
	u32int *gp, *field;
	int reg;

	enable = enable != 0;
	if(falling)
		reg = Fedge0;
	else
		reg = Redge0;
	gp = (u32int*)GPIOREGS;
	field = &gp[reg + pin/32];
	*field = (*field & ~(enable<<pin)) | (enable<<pin);
}

int
gpiogetevent(uint pin)
{
	u32int *gp, *reg, val;

	gp = (u32int*)GPIOREGS;
	reg = &gp[Evds0 + pin/32];
	val = *reg & (1 << (pin % 32));
	*reg |= val;
	return val != 0;
}

void
gpiomeminit(void)
{
	Physseg seg;

	memset(&seg, 0, sizeof seg);
	seg.attr = SG_PHYSICAL;
	seg.name = "gpio";
	seg.pa = GPIOREGS;
	seg.size = BY2PG;
	addphysseg(&seg);
}
