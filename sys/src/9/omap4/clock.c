#include "u.h"
#include "ureg.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

extern uchar *periph;
ulong *global, *local;
enum {
	PERIPHCLK = 506965000,
	MaxPeriod = PERIPHCLK / (256 * 100),
	MinPeriod = MaxPeriod / 100,
} ;

void
globalclockinit(void)
{
	global = (ulong*) (periph + 0x200);
	local = (ulong*) (periph + 0x600);
	global[2] &= 0xFFFF00F0;
	global[0] = 0;
	global[1] = 0;
	global[2] |= 1;
}

void
cycles(uvlong *x)
{
	ulong hi, newhi, lo, *y;
	
	newhi = global[1];
	do{
		hi = newhi;
		lo = global[0];
	}while((newhi = global[1]) != hi);
	y = (ulong *) x;
	y[0] = lo;
	y[1] = hi;
}

uvlong
fastticks(uvlong *hz)
{
	uvlong ret;

	if(hz != nil)
		*hz = PERIPHCLK;
	cycles(&ret);
	return ret;
}

ulong
µs(void)
{
	return fastticks2us(fastticks(nil));
}


ulong
perfticks(void)
{
	return global[0];
}

void
clocktick(Ureg* ureg, void *)
{
	timerintr(ureg, 0);
}

void
localclockinit(void)
{
	local[2] = 0xFF06;
	intenable(29, clocktick, nil);
	timerset(0);
}

void
timerset(uvlong val)
{
	uvlong now, ticks;

	if(val == 0)
		ticks = MaxPeriod;
	else{
		cycles(&now);
		ticks = (val - now) / 256;
		if(ticks < MinPeriod)
			ticks = MinPeriod;
		if(ticks > MaxPeriod)
			ticks = MaxPeriod;
	}
	local[2] &= ~1;
	local[0] = local[1] = ticks;
	local[2] |= 1;
}

static void
waituntil(uvlong n)
{
	uvlong now, then;
	
	cycles(&now);
	then = now + n;
	while(now < then)
		cycles(&now);
}

void
microdelay(int n)
{
	waituntil(((uvlong)n) * PERIPHCLK / 1000000);
}

void
delay(int n)
{
	waituntil(((uvlong)n) * PERIPHCLK / 1000);
}

