#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>

static int
_reqqueuenote(void *, char *note)
{
	return strcmp(note, "flush") == 0;
}

static void
_reqqueueproc(void *v)
{
	Reqqueue *q;
	Req *r;
	void (*f)(Req *);
	
	q = v;
	*threaddata() = q;
	rfork(RFNOTEG);
	threadnotify(_reqqueuenote, 1);
	for(;;){
		qlock(q);
		q->cur = nil;
		while(q->next == q)
			rsleep(q);
		r = (Req*)(((char*)q->next) - ((char*)&((Req*)0)->qu));
		r->qu.next->prev = r->qu.prev;
		r->qu.prev->next = r->qu.next;
		f = r->qu.f;
		qlock(&r->lk);
		memset(&r->qu, 0, sizeof(r->qu));
		qunlock(&r->lk);
		q->cur = r;
		qunlock(q);
		f(r);
	}
}

Reqqueue *
reqqueuecreate(void)
{
	Reqqueue *q;

	q = emalloc9p(sizeof(*q));
	memset(q, 0, sizeof(*q));
	q->l = q;
	q->next = q->prev = q;
	q->pid = threadpid(proccreate(_reqqueueproc, q, mainstacksize));
	print("%d\n", q->pid);
	return q;
}

void
reqqueuepush(Reqqueue *q, Req *r, void (*f)(Req *))
{
	qlock(q);
	r->qu.f = f;
	r->qu.next = q;
	r->qu.prev = q->prev;
	q->prev->next = &r->qu;
	q->prev = &r->qu;
	rwakeupall(q);
	qunlock(q);
}

void
reqqueueflush(Reqqueue *q, Req *r)
{
	qlock(q);
	if(q->cur == r){
		postnote(PNPROC, q->pid, "flush");
		qunlock(q);
	}else{
		if(r->qu.next != nil){
			r->qu.next->prev = r->qu.prev;
			r->qu.prev->next = r->qu.next;
		}
		qlock(&r->lk);
		memset(&r->qu, 0, sizeof(r->qu));
		qunlock(&r->lk);
		qunlock(q);
		respond(r, "interrupted");
	}
}
