#define _LOCK_EXTENSION
#define _QLOCK_EXTENSION
#define _RESEARCH_SOURCE
#include <u.h>
#include <lock.h>
#include <qlock.h>
#include <stdlib.h>
#include "sys9.h"

#define rendezvous _RENDEZVOUS
#define _rendezvousp rendezvous
#define _tas tas
#define nelem(x) (sizeof(x)/sizeof((x)[0]))

static struct {
	QLp	*p;
	QLp	x[1024];
} ql = {
	ql.x
};

enum
{
	Queuing,
};

/* find a free shared memory location to queue ourselves in */
static QLp*
getqlp(void)
{
	QLp *p, *op;

	op = ql.p;
	for(p = op+1; ; p++){
		if(p == &ql.x[nelem(ql.x)])
			p = ql.x;
		if(p == op)
			abort();
		if(_tas(&(p->inuse)) == 0){
			ql.p = p;
			break;
		}
	}
	p->next = nil;
	return p;
}

void
qlock(QLock *q)
{
	QLp *p, *mp;

	lock(&q->lock);
	if(!q->locked){
		q->locked = 1;
		unlock(&q->lock);
		return;
	}

	/* chain into waiting list */
	mp = getqlp();
	p = q->tail;
	if(p == nil)
		q->head = mp;
	else
		p->next = mp;
	q->tail = mp;
	mp->state = Queuing;
	unlock(&q->lock);

	/* wait */
	while((*_rendezvousp)(mp, (void*)1) == (void*)~0)
		;
	mp->inuse = 0;
}

void
qunlock(QLock *q)
{
	QLp *p;

	lock(&q->lock);
	p = q->head;
	if(p != nil){
		/* wakeup head waiting process */
		q->head = p->next;
		if(q->head == nil)
			q->tail = nil;
		unlock(&q->lock);
		while((*_rendezvousp)(p, (void*)0x12345) == (void*)~0)
			;
		return;
	}
	q->locked = 0;
	unlock(&q->lock);
}

int
canqlock(QLock *q)
{
	if(!canlock(&q->lock))
		return 0;
	if(!q->locked){
		q->locked = 1;
		unlock(&q->lock);
		return 1;
	}
	unlock(&q->lock);
	return 0;
}
