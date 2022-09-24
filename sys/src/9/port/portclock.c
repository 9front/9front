#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "tos.h"

typedef struct Timers Timers;
typedef struct Wheel Wheel;

enum {
	WSHIFT	= 5,
	NWHEEL	= 6,
	NSLOT	= 1<<WSHIFT,
	SMASK	= NSLOT-1,
	TMAX	= 1<<(WSHIFT*(NWHEEL-1)),
};

struct Wheel {
	Timer	*slots[NSLOT];
	int	idx;
};
	
struct Timers {
	Lock;
	uvlong	tnow;
	uvlong	tsched;
	Wheel	wheels[NWHEEL];
};

static Timers timers[MAXMACH];

ulong intrcount[MAXMACH];
ulong fcallcount[MAXMACH];

#define slot(tt, i, j)	((tt)->wheels[(i)].slots[(j) & SMASK])
#define slotp(tt, i, j)	(&(tt)->wheels[(i)].slots[(j) & SMASK])

static void
tins(Timers *tt, Timer *t)
{
	Timer *n, **p;
	uvlong dt;
	int i;

	dt = t->twhen - tt->tnow;
	assert((vlong)dt >= 0);
	for(i = 0; dt >= NSLOT && i < NWHEEL-1; i++)
		dt = (dt + tt->wheels[i].idx) >> WSHIFT;
	dt = (dt + tt->wheels[i].idx) & SMASK;

	p = &tt->wheels[i].slots[dt];
	n = *p;
	t->tt = tt;
	t->tnext = n;
	t->tlink = p;
	if(n != nil)
		n->tlink = &t->tnext;
	*p = t;
}

static void
tdel(Timer *dt)
{
	if(dt->tt == nil)
		return;
	if(dt->tnext != nil)
		dt->tnext->tlink = dt->tlink;
	*dt->tlink = dt->tnext;
	dt->tlink = nil;
	dt->tnext = nil;
	dt->tt = nil;
}

/* look for another timer at same frequency for combining */
static uvlong
tcombine(Timers *tt, Timer *nt, uvlong now)
{
	int i, j, s;
	Timer *t;

	for(i = 0; i < NWHEEL; i++){
		s = tt->wheels[i].idx;
		for(j = s; j < s+NSLOT; j++)
			for(t = slot(tt, i, j); t != nil && t->tns < nt->tns; t = t->tnext)
				if(t->tmode == Tperiodic && t->twhen > now  && t->tns == nt->tns)
					return t->twhen;
	}
	return fastticks(nil);
}

/* approximate time of the next timer to schedule, or 0 if there's already one scheduled */
static uvlong
tnext(Timers *tt, uvlong when)
{
	uvlong tk, dt;
	int i, j, s;
	Wheel *w;

	if(when > tt->tsched)
		return 0;

	dt = 1;
	for(i = 0; i < NWHEEL; i++){
		w = &tt->wheels[i];
		s = w->idx+1;
		tk = tt->tnow;
		/* the current slot should always already be processed, and thus empty */
		assert(slot(tt, i, w->idx) == nil);
		for(j = s; j < s+NSLOT-1; j++){
			tk += dt;
			if(tk >= when || slot(tt, i, j) != nil)
				break;
		}
		if(tk < when)
			when = tk;
		dt <<= WSHIFT;
	}
	tt->tsched = when;
	return when;

}

/* Called with tt locked */
static void
tadd(Timers *tt, Timer *nt)
{
	assert(nt->tt == nil);
	nt->tt = tt;
	nt->tnext = nil;
	nt->tlink = nil;
	switch(nt->tmode){
	default:
		panic("timer");
		break;
	case Trelative:
		if(nt->tns <= 0)
			nt->tns = 1;
		nt->twhen = fastticks(nil) + ns2fastticks(nt->tns);
		break;
	case Tperiodic:
		assert(nt->tns >= 100000);	/* At least 100 µs period */
		if(0 && nt->twhen == 0)
			nt->twhen = tcombine(tt, nt, tt->tnow);
		else
			nt->twhen = fastticks(nil);
		nt->twhen += ns2fastticks(nt->tns);
		break;
	}
	tins(tt, nt);
}

/* add or modify a timer */
void
timeradd(Timer *nt)
{
	Timers *tt;
	uvlong when;

	/* Must lock Timer struct before Timers struct */
	ilock(nt);
	tt = nt->tt;
	if(tt != nil){
		ilock(tt);
		tdel(nt);
		iunlock(tt);
	}
	tt = &timers[m->machno];
	ilock(tt);
	tadd(tt, nt);
	when = tnext(tt, nt->twhen);
	if(when)
		timerset(when);
	iunlock(tt);
	iunlock(nt);
}


void
timerdel(Timer *dt)
{
	Mach *mp;
	Timers *tt;
	uvlong when;

	/* avoid Tperiodic getting re-added */
	dt->tmode = Trelative;

	ilock(dt);
	if(tt = dt->tt){
		ilock(tt);
		tdel(dt);
		when = tnext(tt, dt->twhen);
		if(when && tt == &timers[m->machno])
			timerset(when);
		iunlock(tt);
	}
	if((mp = dt->tactive) == nil || mp->machno == m->machno){
		iunlock(dt);
		return;
	}
	iunlock(dt);
	/* rare, but tf can still be active on another cpu */
	while(dt->tactive == mp && dt->tt == nil)
		if(up->nlocks == 0 && islo())
			sched();
}

void
hzclock(Ureg *ur)
{
	m->ticks++;
	if(m->proc)
		m->proc->pc = ur->pc;

	if(m->flushmmu){
		if(up && up->newtlb)
			flushmmu();
		m->flushmmu = 0;
	}

	accounttime();
	dtracytick(ur);
	kmapinval();

	if(kproftimer != nil)
		kproftimer(ur->pc);

	if(active.machs[m->machno] == 0)
		return;

	if(active.exiting)
		exit(0);

	if(up && up->state == Running){
		if(userureg(ur)){
			/* user profiling clock */
			Tos *tos = (Tos*)(USTKTOP-sizeof(Tos));
			tos->clock += TK2MS(1);
			segclock(ur->pc);
		}

		hzsched();	/* in proc.c */
	}
}

void
timerintr(Ureg *u, Tval)
{
	uvlong dt, when, now;
	int i, j, s, hz;
	Timer *t, *n;
	Timers *tt;
	Wheel *w;

	intrcount[m->machno]++;
	tt = &timers[m->machno];
	now = fastticks(nil);

	ilock(tt);
	hz = 0;
	dt = now - tt->tnow;
	tt->tnow = now;
	/*
	 * We need to look at all the wheels.
	 */
	for(i = 0; i < NWHEEL; i++){
		w = &tt->wheels[i];
		s = w->idx;
		w->idx = (s+dt)&SMASK;
		for(j = s; j <= s+dt && j < s+NSLOT; j++){
			for(t = slot(tt, i, j); t != nil; t = n){
				assert(t->tt == tt);
				n = t->tnext;
				/*
				 * The last wheel can contain timers that are
				 * expiring both before and after now.
				 * Cascade the future ones, and expire the current
				 * ones.
				 */
				if(t->twhen > now+TMAX)
					continue;
				/*
				 * Otherwise, we cascade this timer into a new slot
				 * in a closer wheel.
				 */
				tdel(t);
				if(t->twhen > now){
					tins(tt, t);
					continue;
				}
				/*
				 * No need to ilock t here: any manipulation of t
				 * requires tdel(t) and this must be done with a
				 * lock to tt held.  We have tt, so the tdel will
				 * wait until we're done
				 */
				t->tactive = MACHP(m->machno);
				fcallcount[m->machno]++;
				iunlock(tt);
				if(t->tf)
					t->tf(u, t);
				else
					hz = 1;
				t->tactive = nil;
				ilock(tt);
				if(t->tmode == Tperiodic)
					tadd(tt, t);
			}
		}
		dt += s;
		dt >>= WSHIFT;
	}
	when = tnext(tt, ~0);

	if(when != 0)
		timerset(when);
	iunlock(tt);
	if(hz)
		hzclock(u);
}

void
timersinit(void)
{
	Timer *t;

	/*
	 * T->tf == nil means the HZ clock for this processor.
	 */
	todinit();
	t = malloc(sizeof(*t));
	if(t == nil)
		panic("timersinit: no memory for Timer");
	t->tmode = Tperiodic;
	t->tt = nil;
	t->tns = 1000000000/HZ;
	t->tf = nil;
	timeradd(t);
}

Timer*
addclock0link(void (*f)(void), int ms)
{
	Timer *nt;
	uvlong when;

	/* Synchronize to hztimer if ms is 0 */
	nt = malloc(sizeof(Timer));
	if(nt == nil)
		panic("addclock0link: no memory for Timer");
	if(ms == 0)
		ms = 1000/HZ;
	nt->tns = (vlong)ms*1000000LL;
	nt->tmode = Tperiodic;
	nt->tt = nil;
	nt->tf = (void (*)(Ureg*, Timer*))f;

	ilock(&timers[0]);
	tadd(&timers[0], nt);
	when = tnext(&timers[0], nt->twhen);
	if(when)
		timerset(when);
	iunlock(&timers[0]);
	return nt;
}

/*
 *  This tk2ms avoids overflows that the macro version is prone to.
 *  It is a LOT slower so shouldn't be used if you're just converting
 *  a delta.
 */
ulong
tk2ms(ulong ticks)
{
	uvlong t, hz;

	t = ticks;
	hz = HZ;
	t *= 1000L;
	t = t/hz;
	ticks = t;
	return ticks;
}

ulong
ms2tk(ulong ms)
{
	/* avoid overflows at the cost of precision */
	if(ms >= 1000000000/HZ)
		return (ms/1000)*HZ;
	return (ms*HZ+500)/1000;
}
