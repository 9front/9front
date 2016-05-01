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
regexec(Reprog *prog, char *str, Resub *sem, int msize)
{
	RethreadQ lists[2], *clist, *nlist, *tmp;
	Rethread *t, *next, *pooltop, *avail;
	Reinst *curinst;
	Rune r;
	char *sp, *ep, endc;
	int i, match, first, gen, matchpri, pri;

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
	sp = str;
	ep = nil;
	endc = '\0';
	if(sem != nil && msize > 0) {
		if(sem->sp != nil)
			sp = sem->sp;
		if(sem->ep != nil && *sem->ep != '\0') {
			ep = sem->ep;
			endc = *sem->ep;
			*sem->ep = '\0';
		}
	}
	r = Runemax + 1; 
	for(; r != L'\0'; sp += i) {
		gen++;
		i = chartorune(&r, sp);
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
			if(r != curinst->r)
				goto Done;
		case OANY: /* fallthrough */
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
			if(r < curinst->r)
				goto Done;
			if(r > curinst->r1) {
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
			if(r != L'\n') {
				curinst++;
				goto Again;
			}
			goto Done;
		case OBOL:
			if(sp == str || sp[-1] == '\n') {
				curinst++;
				goto Again;
			}
			goto Done;
		case OEOL:
			if(r == L'\n' || r == L'\0' && ep == nil) {
				curinst++;
				goto Again;
			}
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
				t->sem[curinst->sub].sp = sp;
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
					sem->ep = sp;
				}
				goto Done;
			}
			if(curinst->sub < msize)
				t->sem[curinst->sub].ep = sp;
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
	if(ep != nil)
		*ep = endc;
	return match;
}
