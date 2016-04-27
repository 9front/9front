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
	Rethread *t, *nextthr, **availthr;
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

	for(i = 0; i < prog->nthr; i++)
		prog->thrpool[i] = prog->threads + i;
	availthr = prog->thrpool + prog->nthr;

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
		Any:
			nextthr = t->next;
			t->pc = curinst + 1;
			t->next = nil;
			*nlist->tail = t;
			nlist->tail = &t->next;
			if(nextthr == nil)
				break;
			t = nextthr;
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
			nextthr = t->next;
			t->pc = curinst->a;
			t->next = nil;
			*nlist->tail = t;
			nlist->tail = &t->next;
			if(nextthr == nil)
				break;
			t = nextthr;
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
			if(r == L'\0' && ep == nil) {
				curinst++;
				goto Again;
			}
			if(r == L'\n')
				goto Any;
			goto Done;
		case OJMP:
			curinst = curinst->a;
			goto Again;
		case OSPLIT:
			nextthr = *--availthr;
			nextthr->pc = curinst->b;
			if(msize > 0)
				memcpy(nextthr->sem, t->sem, sizeof(Resub)*msize);
			nextthr->pri = t->pri;
			nextthr->next = t->next;
			t->next = nextthr;
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
			*availthr++ = t;
			t = t->next;
			if(t == nil)
				break;
			curinst = t->pc;
			goto Again;
		}
Start:
		/* Start again once if we haven't found anything. */
		if(first == 1 && match == 0) {
			first = 0;
			t = *--availthr;
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
