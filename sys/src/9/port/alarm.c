#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

static Alarms	alarms;
static Rendez	alarmr;

void
alarmkproc(void*)
{
	Proc *rp;
	ulong now, when;

	while(waserror())
		;

	for(;;){
		now = MACHP(0)->ticks;
		qlock(&alarms);
		for(rp = alarms.head; rp != nil; rp = rp->palarm){
			if((when = rp->alarm) == 0)
				continue;
			if((long)(now - when) < 0)
				break;
			if(!canqlock(&rp->debug))
				break;
			if(rp->alarm != 0){
				postnote(rp, 0, "alarm", NUser);
				rp->alarm = 0;
			}
			qunlock(&rp->debug);
		}
		alarms.head = rp;
		qunlock(&alarms);

		sleep(&alarmr, return0, 0);
	}
}

/*
 *  called every clock tick on cpu0
 */
void
checkalarms(void)
{
	Proc *p;
	ulong now, when;

	p = alarms.head;
	if(p != nil){
		now = MACHP(0)->ticks;
		when = p->alarm;
		if(when == 0 || (long)(now - when) >= 0)
			wakeup(&alarmr);
	}
}

ulong
procalarm(ulong time)
{
	Proc **l, *f;
	ulong when, old;

	old = up->alarm;
	if(old)
		old = tk2ms(old - MACHP(0)->ticks);
	if(time == 0) {
		up->alarm = 0;
		return old;
	}
	when = ms2tk(time)+MACHP(0)->ticks;
	if(when == 0)
		when = 1;

	qlock(&alarms);
	l = &alarms.head;
	for(f = *l; f; f = f->palarm) {
		if(up == f){
			*l = f->palarm;
			break;
		}
		l = &f->palarm;
	}

	up->palarm = 0;
	if(alarms.head) {
		l = &alarms.head;
		for(f = *l; f; f = f->palarm) {
			if((long)(f->alarm - when) >= 0) {
				up->palarm = f;
				*l = up;
				goto done;
			}
			l = &f->palarm;
		}
		*l = up;
	}
	else
		alarms.head = up;
done:
	up->alarm = when;
	qunlock(&alarms);

	return old;
}
