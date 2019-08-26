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
	AFedge0	= 0x88>>2,
	PUD	= 0x94>>2,
		Off	= 0x0,
		Pulldown= 0x1,
		Pullup	= 0x2,
		PudMask	= 0x3,

	PUDclk0	= 0x98>>2,
	PUDclk1	= 0x9c>>2,

	/* BCM2711 only */
	PUPPDN0	= 0xe4>>2,
	PUPPDN1	= 0xe8>>2,
	PUPPDN2	= 0xec>>2,
	PUPPDN3	= 0xf0>>2,
};

static u32int *regs = (u32int*)GPIOREGS;

void
gpiosel(uint pin, int func)
{	
	int shift = (pin % 10) * 3;
	u32int *reg = &regs[Fsel0 + pin/10];
	func &= FuncMask;
	*reg = (*reg & ~(FuncMask<<shift)) | (func<<shift);
}

void
gpiopull(uint pin, int func)
{
	u32int *reg;
	func &= PudMask;
	if(regs[PUPPDN3] == 0x6770696f){
		/* BCM2835, BCM2836, BCM2837 */
		u32int mask = 1 << (pin % 32);
		reg = &regs[PUDclk0 + pin/32];
		regs[PUD] = func;
		microdelay(1);
		*reg = mask;
		microdelay(1);
		*reg = 0;
	} else {
		/* BCM2711 */
		int shift = 2*(pin % 16);
		static u32int map[PudMask+1] = {0x00,0x02,0x01};
		reg = &regs[PUPPDN0 + pin/16];
		*reg = (*reg & ~(3<<shift)) | (map[func] << shift);
	}
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
	regs[(set? Set0: Clr0) + pin/32] = 1 << (pin % 32);
}

int
gpioin(uint pin)
{
	return (regs[Lev0 + pin/32] & (1 << (pin % 32))) != 0;
}

void
gpioselevent(uint pin, int falling, int enable)
{
	u32int *reg = &regs[(falling? Fedge0: Redge0) + pin/32];
	*reg = (*reg & ~(1<<pin)) | ((enable != 0)<<pin);
}

int
gpiogetevent(uint pin)
{
	u32int *reg, val;

	reg = &regs[Evds0 + pin/32];
	val = *reg & (1 << (pin % 32));
	*reg |= val;
	return val != 0;
}

void
gpiomeminit(void)
{
	Physseg seg;

	memset(&seg, 0, sizeof seg);
	seg.attr = SG_PHYSICAL | SG_DEVICE | SG_NOEXEC;
	seg.name = "gpio";
	seg.pa = (GPIOREGS - soc.virtio) + soc.physio;
	seg.size = BY2PG;
	addphysseg(&seg);
}
