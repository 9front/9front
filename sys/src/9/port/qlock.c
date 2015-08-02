#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include	"../port/error.h"

struct {
	ulong rlock;
	ulong rlockq;
	ulong wlock;
	ulong wlockq;
	ulong qlock;
	ulong qlockq;
} rwstats;

void
eqlock(QLock *q)
{
	Proc *p;

	if(m->ilockdepth != 0)
		print("eqlock: %#p: ilockdepth %d\n", getcallerpc(&q), m->ilockdepth);
	if(up != nil && up->nlocks)
		print("eqlock: %#p: nlocks %d\n", getcallerpc(&q), up->nlocks);
	if(up != nil && up->eql != nil)
		print("eqlock: %#p: eql %p\n", getcallerpc(&q), up->eql);
	if(q->use.key == 0x55555555)
		panic("eqlock: q %#p, key 5*", q);

	lock(&q->use);
	rwstats.qlock++;
	if(!q->locked) {
		q->locked = 1;
		unlock(&q->use);
		return;
	}
	if(up == nil)
		panic("eqlock");
	if(up->notepending){
		up->notepending = 0;
		unlock(&q->use);
		interrupted();
	}
	rwstats.qlockq++;
	p = q->tail;
	if(p == nil)
		q->head = up;
	else
		p->qnext = up;
	q->tail = up;
	up->eql = q;
	up->qnext = nil;
	up->qpc = getcallerpc(&q);
	up->state = Queueing;
	unlock(&q->use);
	sched();
	if(up->eql == nil){
		up->notepending = 0;
		interrupted();
	}
	up->eql = nil;
}

void
qlock(QLock *q)
{
	Proc *p;

	if(m->ilockdepth != 0)
		print("qlock: %#p: ilockdepth %d\n", getcallerpc(&q), m->ilockdepth);
	if(up != nil && up->nlocks)
		print("qlock: %#p: nlocks %d\n", getcallerpc(&q), up->nlocks);
	if(up != nil && up->eql != nil)
		print("qlock: %#p: eql %p\n", getcallerpc(&q), up->eql);
	if(q->use.key == 0x55555555)
		panic("qlock: q %#p, key 5*", q);
	lock(&q->use);
	rwstats.qlock++;
	if(!q->locked) {
		q->locked = 1;
		unlock(&q->use);
		return;
	}
	if(up == nil)
		panic("qlock");
	rwstats.qlockq++;
	p = q->tail;
	if(p == nil)
		q->head = up;
	else
		p->qnext = up;
	q->tail = up;
	up->eql = nil;
	up->qnext = nil;
	up->state = Queueing;
	up->qpc = getcallerpc(&q);
	unlock(&q->use);
	sched();
}

int
canqlock(QLock *q)
{
	if(!canlock(&q->use))
		return 0;
	if(q->locked){
		unlock(&q->use);
		return 0;
	}
	q->locked = 1;
	unlock(&q->use);
	return 1;
}

void
qunlock(QLock *q)
{
	Proc *p;

	lock(&q->use);
	if (q->locked == 0)
		print("qunlock called with qlock not held, from %#p\n",
			getcallerpc(&q));
	p = q->head;
	if(p != nil){
		q->head = p->qnext;
		if(q->head == nil)
			q->tail = nil;
		unlock(&q->use);
		ready(p);
		return;
	}
	q->locked = 0;
	unlock(&q->use);
}

void
rlock(RWlock *q)
{
	Proc *p;

	lock(&q->use);
	rwstats.rlock++;
	if(q->writer == 0 && q->head == nil){
		/* no writer, go for it */
		q->readers++;
		unlock(&q->use);
		return;
	}

	rwstats.rlockq++;
	p = q->tail;
	if(up == nil)
		panic("rlock");
	if(p == nil)
		q->head = up;
	else
		p->qnext = up;
	q->tail = up;
	up->qnext = nil;
	up->state = QueueingR;
	unlock(&q->use);
	sched();
}

void
runlock(RWlock *q)
{
	Proc *p;

	lock(&q->use);
	p = q->head;
	if(--(q->readers) > 0 || p == nil){
		unlock(&q->use);
		return;
	}

	/* start waiting writer */
	if(p->state != QueueingW)
		panic("runlock");
	q->head = p->qnext;
	if(q->head == nil)
		q->tail = nil;
	q->writer = 1;
	unlock(&q->use);
	ready(p);
}

void
wlock(RWlock *q)
{
	Proc *p;

	lock(&q->use);
	rwstats.wlock++;
	if(q->readers == 0 && q->writer == 0){
		/* noone waiting, go for it */
		q->wpc = getcallerpc(&q);
		q->wproc = up;
		q->writer = 1;
		unlock(&q->use);
		return;
	}

	/* wait */
	rwstats.wlockq++;
	p = q->tail;
	if(up == nil)
		panic("wlock");
	if(p == nil)
		q->head = up;
	else
		p->qnext = up;
	q->tail = up;
	up->qnext = nil;
	up->state = QueueingW;
	unlock(&q->use);
	sched();
}

void
wunlock(RWlock *q)
{
	Proc *p;

	lock(&q->use);
	p = q->head;
	if(p == nil){
		q->writer = 0;
		unlock(&q->use);
		return;
	}
	if(p->state == QueueingW){
		/* start waiting writer */
		q->head = p->qnext;
		if(q->head == nil)
			q->tail = nil;
		unlock(&q->use);
		ready(p);
		return;
	}

	if(p->state != QueueingR)
		panic("wunlock");

	/* waken waiting readers */
	while(q->head != nil && q->head->state == QueueingR){
		p = q->head;
		q->head = p->qnext;
		q->readers++;
		ready(p);
	}
	if(q->head == nil)
		q->tail = nil;
	q->writer = 0;
	unlock(&q->use);
}

/* same as rlock but punts if there are any writers waiting */
int
canrlock(RWlock *q)
{
	lock(&q->use);
	rwstats.rlock++;
	if(q->writer == 0 && q->head == nil){
		/* no writer, go for it */
		q->readers++;
		unlock(&q->use);
		return 1;
	}
	unlock(&q->use);
	return 0;
}
