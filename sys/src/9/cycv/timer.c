#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#define clockmgr ((u32int*)(CLOCKMGR_BASE))

uvlong timerhz;

enum {
	TIMERDIV = 1,
	LTIMERDIV = 1,

	GTIMERVALL = 0x200/4,
	GTIMERVALH,
	GTIMERCTL,
	LTIMERVAL = 0x604/4,
	LTIMERCTL,
	LTIMERISR,
};

void
microdelay(int n)
{
	ulong now;

	now = µs();
	while(µs() - now < n);
}

void
delay(int n)
{
	while(--n >= 0)
		microdelay(1000);
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
	return fastticks2us(fastticks(nil));
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
	u32int vco;
	u64int hz;
	int denum, numer, mpuclk;
	
	vco = clockmgr[0x40 / 4];
	denum = vco >> 16 & 0x3f;
	numer = vco >> 3 & 0x1fff;
	mpuclk = clockmgr[0x48 / 4] & 0x1ff;
	
	hz = HPS_CLK * 1000000 * (numer + 1) / ((denum + 1) * 2 * (mpuclk + 1));
	m->cpumhz = (hz + 500000) / 1000000;
	m->cpuhz = hz;
	timerhz = m->cpuhz / 4;

	mpcore[GTIMERCTL] = TIMERDIV - 1 << 8 | 3;
	mpcore[LTIMERCTL] = LTIMERDIV - 1 << 8 | 4;
	intrenable(TIMERIRQ, timerirq, nil, EDGE, "clock");
}

/*
 * synchronize all cpu's cycle counter registers
 */
void
synccycles(void)
{
}
