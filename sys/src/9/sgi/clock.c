#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"

#include	"ureg.h"

enum {
	Cyccntres	= 2, /* counter advances at ½ clock rate */
	Basetickfreq	= 150*Mhz / Cyccntres,	/* sgi/indy */

	Instrs		= 10*Mhz,
};

static long
issue1loop(void)
{
	register int i;
	long st;

	i = Instrs;
	st = perfticks();
	do {
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i;
		/* omit 3 (--i) to account for conditional branch, nop & jump */
		i -= 1+3;	 /* --i plus 3 omitted (--i) instructions */
	} while(--i >= 0);
	return perfticks() - st;
}

/* estimate instructions/s. */
static int
guessmips(long (*loop)(void), char *)
{
	int s;
	long cyc;

	do {
		s = splhi();
		cyc = loop();
		splx(s);
		if (cyc < 0)
			iprint("again...");
	} while (cyc < 0);
	/*
	 * Instrs instructions took cyc cycles @ Basetickfreq Hz.
	 * round the result.
	 */
	return (((vlong)Basetickfreq * Instrs) / cyc + Mhz/2) / Mhz;
}

void
clockinit(void)
{
	int mips;

	/*
	 * calibrate fastclock
	 */
	mips = guessmips(issue1loop, "single");

	/*
	 * m->delayloop should be the number of delay loop iterations
	 * needed to consume 1 ms, assuming 2 instr'ns in the delay loop.
	 */
	m->delayloop = mips*Mhz / (1000 * 2);
	if(m->delayloop == 0)
		m->delayloop = 1;

	m->speed = mips;
	m->hz = m->speed*Mhz;
	m->cyclefreq = Basetickfreq;
	m->maxperiod = Basetickfreq / HZ;
	m->minperiod = Basetickfreq / (100*HZ);
	m->lastcount = rdcount();
	wrcompare(m->lastcount+m->maxperiod);

	intron(INTR7);
}

void
clock(Ureg *ur)
{
	wrcompare(rdcount()+m->maxperiod);	/* side-effect: dismiss intr */
	timerintr(ur, 0);
}

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

ulong
µs(void)
{
	return fastticks2us(fastticks(nil));
}

uvlong
fastticks(uvlong *hz)
{
	int x;
	ulong delta, count;

	if(hz)
		*hz = Basetickfreq;

	/* avoid reentry on interrupt or trap, to prevent recursion */
	x = splhi();
	count = rdcount();
	delta = count - m->lastcount;
	m->lastcount = count;
	m->fastticks += delta;
	splx(x);

	return m->fastticks;
}

ulong
perfticks(void)
{
	return rdcount();
}

void
timerset(Tval next)
{
	long period;

	period = next - fastticks(nil);
	if(period < m->minperiod)
		period = m->minperiod;
	else if(period > m->maxperiod - m->minperiod)
		period = m->maxperiod;
	wrcompare(rdcount()+period);
}
