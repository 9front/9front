#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

enum {
	TIMERDIV = 1,
	LTIMERDIV = 1,
	
	ARM_PLL_CTRL = 0x100/4,
	ARM_CLK_CTRL = 0x120/4,

	GTIMERVALL = 0x200/4,
	GTIMERVALH,
	GTIMERCTL,
	LTIMERVAL = 0x604/4,
	LTIMERCTL,
	LTIMERISR,
};

uvlong timerhz;

void
delay(int)
{
}

void
microdelay(int)
{
}

uvlong
fastticks(uvlong *hz)
{
	ulong lo, hi;

	if(hz != nil)
		*hz = timerhz;
	do{
		hi = mpcore[GTIMERVALH];
		lo = mpcore[GTIMERVALL];
	}while(hi != mpcore[GTIMERVALH]);
	return lo | (uvlong)hi << 32;
}

ulong
µs(void)
{
	NOPE
	return 0;
}

void
timerset(Tval v)
{
	vlong w;

	w = v - fastticks(nil);
	if(w < 1)
		w = 1;
	if(w > 0xffffffffLL)
		w = 0xffffffff;
	mpcore[LTIMERCTL] &= ~1;
	mpcore[LTIMERVAL] = w;
	mpcore[LTIMERCTL] |= 1;
}

void
timerirq(Ureg *u, void *)
{
	if((mpcore[LTIMERISR] & 1) != 0){
		mpcore[LTIMERISR] |= 1;
		timerintr(u, 0);
	}
}

void
timerinit(void)
{
	int mhz;
	
	mhz = PS_CLK * (slcr[ARM_PLL_CTRL] >> 12 & 0x7f) / (slcr[ARM_CLK_CTRL] >> 8 & 0x3f);
	timerhz = mhz * 500000;
	mpcore[GTIMERCTL] = TIMERDIV - 1 << 8 | 3;
	mpcore[LTIMERCTL] = LTIMERDIV - 1 << 8 | 4;
	intrenable(TIMERIRQ, timerirq, nil, EDGE, "clock");
	setpmcr(7);
}
