#include <u.h>
#include <libc.h>
#include <regexp.h>
#include "regimpl.h"

typedef struct RethreadQ RethreadQ;
struct RethreadQ
{
	Rethread *head;
	Rethread **tail;
};

int
rregexec(Reprog *prog, Rune *str, Resub *sem, int msize)
{
	RethreadQ lists[2], *clist, *nlist, *tmp;
	Rethread *t, *next, *pooltop, *avail;
	Reinst *curinst;
	Rune *rsp, *rep, endr, last;
	int match, first, gen, pri, matchpri;

	if(msize > NSUBEXPM)
		msize = NSUBEXPM;

	if(prog->startinst->gen != 0) {
		for(curinst = prog->startinst; curinst < prog->startinst + prog->len; curinst++)
			curinst->gen = 0;
	}

	clist = lists;
	clist->head = nil;
	clist->tail = &clist->head;
	nlist = lists + 1;
	nlist->head = nil;
	nlist->tail = &nlist->head;

	pooltop = prog->threads + prog->nthr;
	avail = nil;

	pri = matchpri = gen = match = 0;
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
	last = 1;
	for(; last != L'\0'; rsp++) {
		gen++;
		last = *rsp;
		first = 1;
		t = clist->head;
		if(t == nil)
			goto Start;
		curinst = t->pc;
Again:
		if(curinst->gen == gen)
			goto Done;
		curinst->gen = gen;
		switch(curinst->op) {
		case ORUNE:
			if(*rsp != curinst->r)
				goto Done;
		case OANY: /* fallthrough */
		Any:
			next = t->next;
			t->pc = curinst + 1;
			t->next = nil;
			*nlist->tail = t;
			nlist->tail = &t->next;
			if(next == nil)
				break;
			t = next;
			curinst = t->pc;
			goto Again;
		case OCLASS:
		Class:
			if(*rsp < curinst->r)
				goto Done;
			if(*rsp > curinst->r1) {
				curinst++;
				goto Class;
			}
			next = t->next;
			t->pc = curinst->a;
			t->next = nil;
			*nlist->tail = t;
			nlist->tail = &t->next;
			if(next == nil)
				break;
			t = next;
			curinst = t->pc;
			goto Again;
		case ONOTNL:
			if(*rsp != L'\n') {
				curinst++;
				goto Again;
			}
			goto Done;
		case OBOL:
			if(rsp == str || rsp[-1] == L'\n') {
				curinst++;
				goto Again;
			}
			goto Done;
		case OEOL:
			if(*rsp == L'\0' && rep == nil) {
				curinst++;
				goto Again;
			}
			if(*rsp == '\n')
				goto Any;
			goto Done;
		case OJMP:
			curinst = curinst->a;
			goto Again;
		case OSPLIT:
			if(avail == nil)
				next = --pooltop;
			else {
				next = avail;
				avail = avail->next;
			}
			next->pc = curinst->b;
			if(msize > 0)
				memcpy(next->sem, t->sem, sizeof(Resub)*msize);
			next->pri = t->pri;
			next->next = t->next;
			t->next = next;
			curinst = curinst->a;
			goto Again;
		case OSAVE:
			if(curinst->sub < msize)
				t->sem[curinst->sub].rsp = rsp;
			curinst++;
			goto Again;
		case OUNSAVE:
			if(curinst->sub == 0) {
				/* "Highest" priority is the left-most longest. */
				if (t->pri > matchpri)
					goto Done;
				match = 1;
				matchpri = t->pri;
				if(sem != nil && msize > 0) {
					memcpy(sem, t->sem, sizeof(Resub)*msize);
					sem->rep = rsp;
				}
				goto Done;
			}
			if(curinst->sub < msize)
				t->sem[curinst->sub].rep = rsp;
			curinst++;
			goto Again;
		Done:
			next = t->next;
			t->next = avail;
			avail = t;
			if(next == nil)
				break;
			t = next;
			curinst = t->pc;
			goto Again;
		}
Start:
		/* Start again once if we haven't found anything. */
		if(first == 1 && match == 0) {
			first = 0;
			if(avail == nil)
				t = --pooltop;
			else {
				t = avail;
				avail = avail->next;
			}
			if(msize > 0)
				memset(t->sem, 0, sizeof(Resub)*msize);
			/* "Lower" priority thread */
			t->pri = matchpri = pri++;
			t->next = nil;
			curinst = prog->startinst;
			goto Again;
		}
		/* If we have a match and no extant threads, we are done. */
		if(match == 1 && nlist->head == nil)
			break;
		tmp = clist;
		clist = nlist;
		nlist = tmp;
		nlist->head = nil;
		nlist->tail = &nlist->head;
	}
	if(rep != nil)
		*rep = endr;
	return match;
}
