#include <u.h>
#include <libc.h>
#include <sat.h>
#include "impl.h"

SATSolve *
satnew(void)
{
	SATSolve *s;
	
	s = calloc(1, sizeof(SATSolve));
	if(s == nil) return nil;
	s->bl[0].next = s->bl[0].prev = &s->bl[0];
	s->bl[1].next = s->bl[1].prev = &s->bl[1];
	s->bl[0].end = (uchar*)&s->bl[0] + SATBLOCKSZ; /* this block is "full" */
	s->bl[1].end = (uchar*)&s->bl[1] + SATBLOCKSZ;
	s->lastp[0] = &s->cl;
	s->lastp[1] = &s->learncl;
	s->lastbl = &s->bl[1];
	s->randfn = (long(*)(void*)) lrand;
	
	s->goofprob = 0.02 * (1UL<<31);
	s->varρ = 1/0.9;
	s->clauseρ = 1/0.999;
	s->trivlim = 10;
	s->purgeΔ = 10000;
	s->purgeδ = 100;
	s->purgeα = 0.2;
	s->flushψ = (1ULL<<32)*0.05;
	
	s->Δactivity = 1;
	s->Δclactivity = 1;
	
	return s;
}

void
satfree(SATSolve *s)
{
	SATBlock *b, *bb;
	int i;
	
	if(s == nil) return;
	for(i = 0; i < 2; i++)
		for(b = s->bl[i].next; b != &s->bl[i]; b = bb){
			bb = b->next;
			free(b);
		}
	for(i = 0; i < 2 * s->nvar; i++)
		free(s->lit[i].bimp);
	free(s->heap);
	free(s->trail);
	free(s->decbd);
	free(s->var);
	free(s->lit);
	free(s->cflcl);
	free(s->fullrcfl);
	free(s->fullrlits);
	free(s->scrap);
	free(s);
}

void
saterror(SATSolve *s, char *msg, ...)
{
	char buf[ERRMAX];
	va_list va;
	
	va_start(va, msg);
	vsnprint(buf, sizeof(buf), msg, va);
	va_end(va);
	s->scratched = 1;
	if(s != nil && s->errfun != nil)
		s->errfun(buf, s->erraux);
	sysfatal("%s", buf);
}

int
satval(SATSolve *s, int l)
{
	int m, v;
	
	if(s->unsat) return -1;
	m = l >> 31;
	v = (l + m ^ m) - 1;
	if(v < 0 || v >= s->nvar) return -1;
	return s->lit[2*v+(m&1)].val;
}

int
satnrand(SATSolve *s, int n)
{
	long slop, v;

	if(n <= 1) return 0;
	slop = 0x7fffffff % n;
	do
		v = s->randfn(s->randaux);
	while(v <= slop);
	return v % n;
}

void *
satrealloc(SATSolve *s, void *v, ulong n)
{
	v = realloc(v, n);
	if(v == nil)
		saterror(s, "realloc: %r");
	setmalloctag(v, getcallerpc(&s));
	return v;
}

#define LEFT(x) (2*(x)+1)
#define RIGHT(x) (2*(x)+2)
#define UP(x) ((x)-1>>1)

static SATVar *
heapswap(SATSolve *s, int i, int j)
{
	SATVar *r;
	
	if(i == j) return s->heap[i];
	r = s->heap[i];
	s->heap[i] = s->heap[j];
	s->heap[j] = r;
	s->heap[i]->heaploc = i;
	s->heap[j]->heaploc = j;
	return r;
}

static void
heapup(SATSolve *s, int i)
{
	int m;
	
	m = i;
	for(;;){
		if(LEFT(i) < s->nheap && s->heap[LEFT(i)]->activity > s->heap[m]->activity)
			m = LEFT(i);
		if(RIGHT(i) < s->nheap && s->heap[RIGHT(i)]->activity > s->heap[m]->activity)
			m = RIGHT(i);
		if(i == m) break;
		heapswap(s, m, i);
		i = m;
	}
}

static void
heapdown(SATSolve *s, int i)
{
	int p;

	for(; i > 0 && s->heap[p = UP(i)]->activity < s->heap[i]->activity; i = p)
		heapswap(s, i, p);
}

SATVar *
satheaptake(SATSolve *s)
{
	SATVar *r;
	
	assert(s->nheap > 0);
	r = heapswap(s, 0, --s->nheap);
	heapup(s, 0);
	r->heaploc = -1;
	return r;
}

void
satheapput(SATSolve *s, SATVar *v)
{
	assert(s->nheap < s->nvar);
	v->heaploc = s->nheap;
	s->heap[s->nheap++] = v;
	heapdown(s, s->nheap - 1);
}

void
satreheap(SATSolve *s, SATVar *v)
{
	int i;
	
	i = v->heaploc;
	if(i < 0) return;
	heapup(s, i);
	heapdown(s, i);
}

void
satheapreset(SATSolve *s)
{
	int i, n, j;
	
	s->heap = satrealloc(s, s->heap, s->nvar * sizeof(SATVar *));
	n = s->nvar;
	s->nheap = n;
	for(i = 0; i < n; i++){
		s->heap[i] = &s->var[i];
		s->heap[i]->heaploc = i;
	}
	for(i = 0; i < n - 1; i++){
		j = i + satnrand(s, n - i);
		heapswap(s, i, j);
		heapdown(s, i);
	}
	heapdown(s, n - 1);
}
