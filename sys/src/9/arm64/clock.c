#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "sysreg.h"

static uvlong freq;

enum {
	Enable	= 1<<0,
	Imask	= 1<<1,
	Istatus = 1<<2,
};

void
clockshutdown(void)
{
}

static void
localclockintr(Ureg *ureg, void *)
{
	timerintr(ureg, 0);
}

void
clockinit(void)
{
	syswr(PMCR_EL0, 1<<6 | 7);
	syswr(PMCNTENSET, 1<<31);
	syswr(PMUSERENR_EL0, 1<<2);
	syswr(CNTKCTL_EL1, 1<<1);

	syswr(CNTV_TVAL_EL0, ~0UL);
	syswr(CNTV_CTL_EL0, Enable);

	if(m->machno == 0){
		freq = sysrd(CNTFRQ_EL0);
		print("timer frequency %lld Hz\n", freq);
	}

	/*
	 * we are using virtual counter register CNTVCT_EL0
	 * instead of the performance counter in userspace.
	 */
	m->cyclefreq = freq;

	intrenable(IRQcntvns, localclockintr, nil, BUSUNKNOWN, "clock");
}

void
timerset(uvlong next)
{
	uvlong now;
	long period;

	now = fastticks(nil);
	period = next - now;
	syswr(CNTV_TVAL_EL0, period);
}

uvlong
fastticks(uvlong *hz)
{
	if(hz)
		*hz = freq;
	return sysrd(CNTVCT_EL0);
}

ulong
perfticks(void)
{
	return fastticks(nil);
}

ulong
µs(void)
{
	uvlong hz;
	uvlong t = fastticks(&hz);
	return (t * 1000000ULL) / hz;
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

void
synccycles(void)
{
	static Ref r1, r2;
	int s;

	s = splhi();
	r2.ref = 0;
	incref(&r1);
	while(r1.ref != conf.nmach)
		;
//	syswr(PMCR_EL0, 1<<6 | 7);
	incref(&r2);
	while(r2.ref != conf.nmach)
		;
	r1.ref = 0;
	splx(s);
}
