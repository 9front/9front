#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

static vcpu_time_info_t shadow[MAX_VIRT_CPUS];		// XXX should be in Mach
static ulong wallclock;
static ulong wallclocksystime;

/*
 * Return a consistent set of time parameters.
 */
static vcpu_time_info_t *
getshadow(void)
{
	vcpu_time_info_t *s, *t;

	t = &HYPERVISOR_shared_info->vcpu_info[m->machno].time;
	s = &shadow[m->machno];		// XXX place in mach struct
	while(t->version != s->version) {
		if (t->version&1)
			continue;
		s->version = t->version;
		s->tsc_timestamp = t->tsc_timestamp;
		s->system_time = t->system_time;
		s->tsc_to_system_mul = t->tsc_to_system_mul;
		s->tsc_shift = t->tsc_shift;
	}
	return s;
}


/* just get it from the shared info */
void
xentimerinit(void)
{
	vcpu_time_info_t *t;

	t = getshadow();
	m->cpuhz = (1000000000LL << 32) / t->tsc_to_system_mul;
	if(t->tsc_shift < 0)
		m->cpuhz <<= -t->tsc_shift;
	else
		m->cpuhz >>= t->tsc_shift;
	m->cpumhz = m->cpuhz / 1000000L;
}

void
xentimerset(uvlong next)
{
	uvlong soon;

	soon = fastticks(0) + 100000;
	if (next < soon)
		next = soon;
	HYPERVISOR_set_timer_op(next);
}

void
xentimerclock(Ureg* ureg, void*)
{

	timerintr(ureg, 0);
}

void
xentimerenable(void)
{
	intrenable(VIRQ_TIMER, xentimerclock, nil, 0, "clock");
}

uvlong
xentimerread(uvlong *hz)
{
	uvlong x;
	uvlong delta, sdelta;
	vcpu_time_info_t *t;

	t = getshadow();
	cycles(&x);
	delta = x - t->tsc_timestamp;
	if (t->tsc_shift < 0)
		delta >>= -t->tsc_shift;
	else
		delta <<= t->tsc_shift;
	mul64fract(&sdelta, delta, t->tsc_to_system_mul);
	x = t->system_time + sdelta;
	if (HYPERVISOR_shared_info->wc_sec != wallclock) {
		wallclock = HYPERVISOR_shared_info->wc_sec;
		wallclocksystime = x;
	}
	if (hz)
		*hz = 1000000000;
	return x;
}

ulong
xenwallclock()
{
	ulong elapsed;

	elapsed = (ulong)((xentimerread(0) - wallclocksystime)/1000000000);
	return wallclock + elapsed;
}

void
microdelay(int microsecs)
{
	uvlong targ, hz;

	targ = xentimerread(&hz);
	targ += microsecs * hz / 1000000;
	while(xentimerread(0) < targ)
		continue;
}

void
delay(int millisecs)
{
	microdelay(millisecs * 1000);
}

/*  
 *  performance measurement ticks.  must be low overhead.
 *  doesn't have to count over a second.
 */
ulong
perfticks(void)
{
	uvlong x;

	cycles(&x);
	return x;
}
