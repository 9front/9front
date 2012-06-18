#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include "dat.h"
#include "fns.h"

static void
kickwqr(Buq *q, Req *r)
{
	Buf **bb, *b;
	int l;

	for(bb = &q->bh; q->nwq > 0; bb = &b->next){
		if((b = *bb) == nil)
			break;
		if(b->wreq == nil || (b->wreq != r && r != nil))
			continue;
		l = b->ep - b->rp;
		b = realloc(b, sizeof(*b) + l);
		*bb = b;
		if(b->next == nil)
			q->bt = &b->next;
		memmove(b->end, b->rp, l);
		b->rp = b->end;
		b->ep = b->rp + l;
		b->wreq->ofcall.count = b->wreq->ifcall.count;
		respond(b->wreq, q->error);
		b->wreq = nil;
		q->nwq--;
	}
}

static void
matchreq(Buq *q)
{
	Req *r;
	Buf *b;
	int l;

	while(r = q->rh){
		if((b = q->bh) == nil){
			if(q->closed){
				if((q->rh = r->aux) == nil)
					q->rt = &q->rh;
				if(r->ifcall.type == Tread)
					r->ofcall.count = 0;
				respond(r, q->error);
				continue;
			}
			break;
		}
		if((q->rh = r->aux) == nil)
			q->rt = &q->rh;
		if(r->ifcall.type == Topen){
			respond(r, nil);
			continue;
		}
		l = b->ep - b->rp;
		if(l > r->ifcall.count)
			l = r->ifcall.count;
		memmove(r->ofcall.data, b->rp, l);
		r->ofcall.count = l;
		respond(r, nil);
		b->rp += l;
		q->size -= l;
		if(b->rp >= b->ep){
			if((q->bh = b->next) == nil)
				q->bt = &q->bh;
			if(r = b->wreq){
				r->ofcall.count = r->ifcall.count;
				respond(r, nil);
				q->nwq--;
			}
			free(b);
		}
	}
	if(q->closed && q->nwq > 0)
		kickwqr(q, nil);
	rwakeupall(&q->rz);
}

int
buread(Buq *q, void *v, int l)
{
	Req *r;
	Buf *b;

	qlock(q);
	while((b = q->bh) == nil){
		if(q->closed){
			l = 0;
			if(q->error){
				werrstr("%s", q->error);
				l = -1;
			}
			qunlock(q);
			return l;
		}
		rsleep(&q->rz);
	}
	if(l > (b->ep - b->rp))
		l = b->ep - b->rp;
	memmove(v, b->rp, l);
	b->rp += l;
	q->size -= l;
	rwakeup(&q->rz);
	if(b->rp < b->ep){
		qunlock(q);
		return l;
	}
	if((q->bh = b->next) == nil)
		q->bt = &q->bh;
	qunlock(q);
	if(r = b->wreq){
		r->ofcall.count = r->ifcall.count;
		respond(r, nil);
	}
	free(b);
	return l;
}

int
buwrite(Buq *q, void *v, int l)
{
	Buf *b;

	b = emalloc(sizeof(*b) + l);
	b->wreq = nil;
	b->rp = b->end;
	b->ep = b->rp + l;
	memmove(b->rp, v, l);
	b->next = nil;
	qlock(q);
	if(q->closed){
		l = 0;
		if(q->error){
			werrstr("%s", q->error);
			l = -1;
		}
		qunlock(q);
		free(b);
		return l;
	}
	*q->bt = b;
	q->bt = &b->next;
	q->size += l;
	matchreq(q);
	while(!q->closed && q->size >= q->limit)
		rsleep(&q->rz);
	qunlock(q);
	return l;
}

void
buclose(Buq *q, char *error)
{
	if(q == nil)
		return;
	qlock(q);
	if(!q->closed){
		if(error)
			q->error = estrdup9p(error);
		q->closed = 1;
	}
	matchreq(q);
	qunlock(q);
}

Buq*
bualloc(int limit)
{
	Buq *q;

	q = emalloc(sizeof(*q));
	q->limit = limit;
	q->rt = &q->rh;
	q->bt = &q->bh;
	q->rz.l = q;
	incref(q);
	return q;
}

void
bufree(Buq *q)
{
	Buf *b;
	Key *k;

	if(q == nil || decref(q))
		return;
	while(b = q->bh){
		q->bh = b->next;
		free(b);
	}
	freeurl(q->url);
	while(k = q->hdr){
		q->hdr = k->next;
		free(k);
	}
	free(q->error);
	free(q);
}

void
bureq(Buq *q, Req *r)
{
	Buf *b;
	int l;

	switch(r->ifcall.type){
	default:
		respond(r, "bug in bureq");
		return;
	case Twrite:
		l = r->ifcall.count;
		if(!q->closed && (q->size + l) < q->limit){
			r->ofcall.count = buwrite(q, r->ifcall.data, r->ifcall.count);
			respond(r, nil);
			return;
		}
		b = emalloc(sizeof(*b));
		b->wreq = r;
		b->rp = (uchar*)r->ifcall.data;
		b->ep = b->rp + l;
		b->next = nil;
		qlock(q);
		*q->bt = b;
		q->bt = &b->next;
		q->size += l;
		q->nwq++;
		break;
	case Tread:
	case Topen:
		r->aux = nil;
		qlock(q);
		*q->rt = r;
		q->rt = (Req**)&r->aux;
		break;
	}
	matchreq(q);
	qunlock(q);
}

void
buflushreq(Buq *q, Req *r)
{
	Req **rr;

	switch(r->ifcall.type){
	default:
		respond(r, "bug in bufflushreq");
		return;
	case Twrite:
		qlock(q);
		kickwqr(q, r);
		break;
	case Topen:
	case Tread:
		qlock(q);
		for(rr = &q->rh; *rr; rr = (Req**)&((*rr)->aux)){
			if(*rr != r)
				continue;
			if((*rr = r->aux) == nil)
				q->rt = rr;
			respond(r, "interrupted");
			break;
		}
		break;
	}
	qunlock(q);
}
