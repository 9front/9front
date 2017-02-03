#include <u.h>
#include <libc.h>
#include <regexp.h>
#include "regimpl.h"

typedef struct RethreadQ RethreadQ;
struct RethreadQ {
	Rethread *head;
	Rethread **tail;
};

int
rregexec(Reprog *p, Rune *str, Resub *sem, int msize)
{
	RethreadQ lists[2], *clist, *nlist, *tmp;
	Rethread *t, *next, *pool, *avail;
	Reinst *ci;
	Rune *rsp, *rep, endr, r;
	int matchgen, gen;

	memset(p->threads, 0, sizeof(Rethread)*p->nthr);
	if(msize > NSUBEXPM)
		msize = NSUBEXPM;
	if(p->startinst->gen != 0) {
		for(ci = p->startinst; ci < p->startinst + p->len; ci++)
			ci->gen = 0;
	}

	clist = lists;
	clist->head = nil;
	clist->tail = &clist->head;
	nlist = lists + 1;
	nlist->head = nil;
	nlist->tail = &nlist->head;

	pool = p->threads;
	avail = nil;
	gen = matchgen = 0;

	rsp = str;
	rep = nil;
	endr = L'\0';
	if(sem != nil && msize > 0) {
		if(sem->rsp != nil)
			rsp = sem->rsp;
		if(sem->rep != nil && *sem->rep != L'\0') {
			rep = sem->rep;
			endr = *sem->rep;
			*sem->rep = '\0';
		}
	}

	for(r = L'☺'; r != L'\0'; rsp++) {
		r = *rsp;
		gen++;
		if(matchgen == 0) {
			if(avail == nil) {
				assert(pool < p->threads + p->nthr);
				t = pool++;
			} else {
				t = avail;
				avail = avail->next;
			}
			t->i = p->startinst;
			if(msize > 0)
				memset(t->sem, 0, sizeof(Resub)*msize);
			t->next = nil;
			t->gen = gen;
			*clist->tail = t;
			clist->tail = &t->next;
		}
		t = clist->head;
		if(t == nil)
			break;
		ci = t->i;
Again:
		if(ci->gen == gen)
			goto Done;
		ci->gen = gen;
		switch(ci->op) {
		case ORUNE:
			if(r != ci->r)
				goto Done;
		case OANY: /* fallthrough */
			next = t->next;
			t->i = ci + 1;
			t->next = nil;
			*nlist->tail = t;
			nlist->tail = &t->next;
			goto Next;
		case OCLASS:
		Class:
			if(r < ci->r)
				goto Done;
			if(r > ci->r1) {
				ci++;
				goto Class;
			}
			next = t->next;
			t->i = ci->a;
			t->next = nil;
			*nlist->tail = t;
			nlist->tail = &t->next;
			goto Next;
		case ONOTNL:
			if(r != L'\n') {
				ci++;
				goto Again;
			}
			goto Done;
		case OBOL:
			if(rsp == str || rsp[-1] == L'\n') {
				ci++;
				goto Again;
			}
			goto Done;
		case OEOL:
			if(r == L'\n' || r == L'\0' && rep == nil) {
				ci++;
				goto Again;
			}
			goto Done;
		case OJMP:
			ci = ci->a;
			goto Again;
		case OSPLIT:
			if(avail == nil) {
				assert(pool < p->threads + p->nthr);
				next = pool++;
			} else {
				next = avail;
				avail = avail->next;
			}
			next->i = ci->b;
			if(msize > 0)
				memcpy(next->sem, t->sem, sizeof(Resub)*msize);
			next->next = t->next;
			next->gen = t->gen;
			t->next = next;
			ci = ci->a;
			goto Again;
		case OSAVE:
			if(ci->sub < msize)
				t->sem[ci->sub].rsp = rsp;
			ci++;
			goto Again;
		case OUNSAVE:
			if(ci->sub == 0) {
				matchgen = t->gen;
				if(sem != nil && msize > 0) {
					memcpy(sem, t->sem, sizeof(Resub)*msize);
					sem->rep = rsp;
				}
				goto Done;
			}
			if(ci->sub < msize)
				t->sem[ci->sub].rep = rsp;
			ci++;
			goto Again;
		Done:
			next = t->next;
			t->next = avail;
			avail = t;
		Next:
			if(next == nil)
				break;
			if(matchgen && next->gen > matchgen) {
				*clist->tail = avail;
				avail = next;
				break;
			}
			t = next;
			ci = t->i;
			goto Again;
		}
		tmp = clist;
		clist = nlist;
		nlist = tmp;
		nlist->head = nil;
		nlist->tail = &nlist->head;
	}
	if(rep != nil)
		*rep = endr;
	return matchgen > 0 ? 1 : 0;
}
