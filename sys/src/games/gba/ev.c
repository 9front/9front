#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

typedef struct {
	u16int *cnt;
	Event;
	u16int val;
	int clock;
	u8int i, sh, snd;
} Timer;

typedef struct fifo fifo;
struct fifo {
	u32int d[8];
	u8int head, level, headpos;
};
fifo sndfifo[2];

Event *elist;
Timer timers[4];
Event evhblank;
extern Event evsamp;
Event *events[NEVENT] = {&timers[0].Event, &timers[1].Event, &timers[2].Event, &timers[3].Event, &evhblank, &evsamp};

Var evvars[] = {
	VAR(clock),
	ARR(sndfifo[0].d), VAR(sndfifo[0].head), VAR(sndfifo[0].level), VAR(sndfifo[0].headpos),
	ARR(sndfifo[1].d), VAR(sndfifo[1].head), VAR(sndfifo[1].level), VAR(sndfifo[1].headpos),
	VAR(timers[0].val), VAR(timers[0].clock), VAR(timers[0].sh), VAR(timers[0].snd),
	VAR(timers[1].val), VAR(timers[1].clock), VAR(timers[1].sh), VAR(timers[1].snd),
	VAR(timers[2].val), VAR(timers[2].clock), VAR(timers[2].sh), VAR(timers[2].snd),
	VAR(timers[3].val), VAR(timers[3].clock), VAR(timers[3].sh), VAR(timers[3].snd),
	{nil, 0, 0},
};

void
addevent(Event *ev, int time)
{
	Event **p, *e;
	int t;
	
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
fifoput(int i, u32int s)
{
	fifo *f;
	
	f = sndfifo + i;
	if(f->level < 8)
		f->d[(f->head + f->level++) & 7] = s;
}

void
fifotimer(int b, int n)
{
	fifo *f;
	int i, j;
	extern s8int snddma[2];
	
	for(i = 0; i < 2; i++){
		if((b & 1<<i) == 0)
			continue;
		f = &sndfifo[i];
		for(j = 0; j < n && f->level > 0; j++){
			snddma[i] = f->d[f->head] & 0xff;
			f->d[f->head] >>= 8;
			if(++f->headpos == 4){
				f->head = (f->head + 1) & 7;
				f->level--;
				f->headpos = 0;
			}
		}
		if(f->level <= 4)
			dmastart(DMASOUND);
	}
}

void
soundcnth(u16int v)
{
	timers[0].snd = 0;
	timers[1].snd = 0;
	if((v & 3<<8) != 0)
		timers[(v >> 10) & 1].snd |= 1;
	if((v & 3<<12) != 0)
		timers[(v >> 14) & 1].snd |= 2;
	if((v & 1<<11) != 0){
		sndfifo[0].level = 0;
		sndfifo[0].head = 0;
		sndfifo[0].headpos = 0;
	}
	if((v & 1<<15) != 0){
		sndfifo[1].level = 0;
		sndfifo[1].head = 0;
		sndfifo[1].headpos = 0;
	}
}

u16int
timerget(int i)
{
	Timer *t;
	
	t = &timers[i];
	if((*t->cnt & (COUNTUP|TIMERON)) != TIMERON)
		return t->val;
	return t->val + (clock - t->clock >> t->sh);
}

void
timerset(int i, u16int nc)
{
	u32int v;
	u16int oc;
	Timer *t;
	
	t = &timers[i];
	oc = *t->cnt;
	if((oc & (PRESC|COUNTUP|TIMERON)) == (nc & (PRESC|COUNTUP|TIMERON)))
		return;
	if((oc & (COUNTUP|TIMERON)) == TIMERON){
		v = t->val + (clock - t->clock >> t->sh);
		delevent(t);
	}else
		v = t->val;
	if((oc & TIMERON) == 0 && (nc & TIMERON) != 0)
		v = t->cnt[-1];
	if((nc & 3) != 0)
		t->sh = 4 + (nc & 3) * 2;
	else
		t->sh = 0;
	t->val = v;
	t->clock = clock & -(1 << t->sh);
	if((nc & (COUNTUP|TIMERON)) == TIMERON)
		addevent(t, (0x10000 - t->val << t->sh) + (-clock & (1 << t->sh) - 1));
}

void
timertick(void *aux)
{
	Timer *t;
	u32int v;
	int to;
	ulong clock0;
	
	t = aux;
	clock0 = clock + t->time;
	t->clock = clock0 & -(1 << t->sh);
	t->val = -t->time >> t->sh;
	do{
		to = 0;
		do{
			t->val = v = t->val + t->cnt[-1];
			to++;
		}while(v >= 0x10000);
		if(t == aux)
			addevent(t, (0x10000 - t->val << t->sh) + (-clock0 & (1 << t->sh) - 1));
		if((*t->cnt & TIMERIRQ) != 0)
			setif(IRQTIM0 << t->i);
		if(t->snd)
			fifotimer(t->snd, to);
		if(++t >= timers + 4 || (*t->cnt & (COUNTUP | TIMERON)) != (COUNTUP|TIMERON))
			break;
		t->val = v = t->val + to;
	}while(v >= 0x10000);
}

void
eventinit(void)
{
	int i;
	extern void hblanktick(void *);

	for(i = 0; i < 4; i++){
		timers[i].f = timertick;
		timers[i].aux = &timers[i];
		timers[i].i = i;
		timers[i].cnt = &reg[TM0CNTH + i * 2];
	}
	evhblank.f = hblanktick;
	addevent(&evhblank, 240*4);
}
