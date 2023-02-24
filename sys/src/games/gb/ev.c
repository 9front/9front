#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

Event evhblank, evtimer, evenv;
extern Event evsamp, chev[4];
extern Event evse;
Event *events[NEVENT] = {&evhblank, &evtimer, &evenv, &evsamp, &chev[0], &chev[1], &chev[2], &chev[3], &evse};
Event *elist;
static int timshtab[4] = {10, 4, 6, 8}, timsh;
ulong timclock;
Var evvars[] = {VAR(timsh), VAR(timclock), {nil, 0, 0}};

void
addevent(Event *ev, int time)
{
	Event **p, *e;
	int t;
	
	assert(time >= 0);
	t = time;
	for(p = &elist; (e = *p) != nil; p = &e->next){
		if(t < e->time){
			e->time -= t;
			break;
		}
		t -= e->time;
	}
	ev->next = e;
	ev->time = t;
	*p = ev;
}

void
delevent(Event *ev)
{
	Event **p, *e;
	
	for(p = &elist; (e = *p) != nil; p = &e->next)
		if(e == ev){
			*p = e->next;
			if(e->next != nil)
				e->next->time += e->time;
			return;
		}
}

void
popevent(void)
{
	Event *e;
	int t;
	
	do{
		e = elist;
		t = e->time;
		elist = e->next;
		e->f(e->aux);
	}while((elist->time += t) <= 0);
}

void
timerset(void)
{
	timclock = clock & -(1<<timsh);
	if((reg[TAC] & 4) != 0){
		delevent(&evtimer);
		addevent(&evtimer, 0x100 - reg[TIMA] << timsh);// | -clock & (1<<timsh)-1);
	}
}

void
timertac(u8int n, int t)
{
	if((reg[TAC] & 7) == (n & 7) && !t)
		return;
	if((reg[TAC] & 4) != 0){
		delevent(&evtimer);
		reg[TIMA] += clock - timclock >> timsh;
	}
	timclock = clock & -(1<<timsh);
	timsh = timshtab[n & 3];
	if((mode & TURBO) == 0)
		 timsh++;
	if((n & 4) != 0)
		addevent(&evtimer, 0x100 - reg[TIMA] << timsh | -clock & (1<<timsh)-1);
}

u8int
timread(void)
{
	if((reg[TAC] & 4) == 0)
		return reg[TIMA];
	return reg[TIMA] + (clock - timclock >> timsh);
}

void
timertick(void *)
{
	reg[TIMA] = reg[TMA];
	addevent(&evtimer, 0x100 - reg[TIMA] << timsh);
	reg[IF] |= IRQTIM;
}

void
eventinit(void)
{
	extern void hblanktick(void *);
	extern void envtick(void *);
	extern void wavetick(void *);
	extern void chantick(void *);
	
	evhblank.f = hblanktick;
	addevent(&evhblank, 240*4);
	evtimer.f = timertick;
	evenv.f = envtick;
	addevent(&evenv, FREQ / 512);
	chev[0].f = chantick;
	chev[1].f = chantick;
	chev[2].f = chantick;
	chev[3].f = chantick;
}
