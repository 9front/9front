#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"error.h"

static Proc	*tripped;
static Rendez	alarmr;
static Lock	triplk;

static int
tfn(void *)
{
	int t;

	ilock(&triplk);
	t = (tripped != nil);
	iunlock(&triplk);
	return t;
}

/*
 *  called every clock tick on cpu0
 */
static void
tripalarm(Ureg*, Timer *t)
{
	ilock(&triplk);
	t->p->palarm = tripped;
	tripped = t->p;
	iunlock(&triplk);

	wakeup(&alarmr);
}

void
alarmkproc(void*)
{
	static Note alarmnote = {
		"alarm",
		NUser,
		1,
	};
	Proc *p, *n;
	Timer *a;

	while(waserror())
		;

	while(1){
		ilock(&triplk);
		p = tripped;
		tripped = nil;
		iunlock(&triplk);

		for(; p != nil; p = n){
			n = p->palarm;
			a = &p->alarm;
			if(!canqlock(&p->debug)){
				a->tns = MS2NS(10);
				timeradd(a);
				continue;
			}
			incref(&alarmnote);
			pushnote(p, &alarmnote);
			qunlock(&p->debug);
		}
		sleep(&alarmr, tfn, nil);
	}
}

ulong
procalarm(ulong time)
{
	uvlong old;
	Timer *a;

	a = &up->alarm;
	old = a->tns;
	timerdel(a);

	lock(a);
	a->tns = MS2NS(time);
	a->tf = tripalarm;
	a->tmode = Trelative;
	a->p = up;
	a->ta = nil;
	unlock(a);
	if(time != 0)
		timeradd(a);
	return NS2MS(old);
}
