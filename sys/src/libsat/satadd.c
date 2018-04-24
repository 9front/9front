#include <u.h>
#include <libc.h>
#include <sat.h>
#include "impl.h"

static SATBlock *
newblock(SATSolve *s, int learned)
{
	SATBlock *b;
	
	b = calloc(1, SATBLOCKSZ);
	if(b == nil)
		saterror(s, "malloc: %r");
	b->prev = s->bl[learned].prev;
	b->next = &s->bl[learned];
	b->next->prev = b;
	b->prev->next = b;
	b->end = (void*) b->data;
	return b;
}

SATClause *
satnewclause(SATSolve *s, int n, int learned)
{
	SATBlock *b;
	SATClause *c;
	int f, sz;
	
	sz = sizeof(SATClause) + (n - 1) * sizeof(int);
	assert(sz <= SATBLOCKSZ);
	if(learned)
		b = s->lastbl;
	else
		b = s->bl[0].prev;
	for(;;){
		f = (uchar*)b + SATBLOCKSZ - (uchar*)b->end;
		if(f >= sz) break;
		b = b->next;
		if(b == &s->bl[learned])
			b = newblock(s, learned);
	}
	c = b->end;
	memset(c, 0, sizeof(SATClause));
	b->end = (void *)((uintptr)b->end + sz + CLAUSEALIGN - 1 & -CLAUSEALIGN);
	b->last = c;
	if(learned){
		if(s->lastp[1] == &s->learncl)
			*s->lastp[0] = c;
		s->lastbl = b;
	}else
		c->next = s->learncl;
	*s->lastp[learned] = c;
	s->lastp[learned] = &c->next;
	s->ncl++;
	return c;
}

/* this is currently only used to subsume clauses, i.e. n is guaranteed to be less than the last n */
SATClause *
satreplclause(SATSolve *s, int n)
{
	SATBlock *b;
	SATClause *c, **wp;
	int f, sz, i, l;
	
	assert(s->lastbl != nil && s->lastbl->last != nil);
	b = s->lastbl;
	c = b->last;
	f = (uchar*)b + SATBLOCKSZ - (uchar*)c;
	sz = sizeof(SATClause) + (n - 1) * sizeof(int);
	assert(f >= sz);
	b->end = (void *)((uintptr)c + sz + CLAUSEALIGN - 1 & -CLAUSEALIGN);
	for(i = 0; i < 2; i++){
		l = c->l[i];
		for(wp = &s->lit[l].watch; *wp != nil && *wp != c; wp = &(*wp)->watch[(*wp)->l[1] == l])
			;
		assert(*wp != nil);
		*wp = c->watch[i];
	}
	memset(c, 0, sizeof(SATClause));
	return c;
}

static int
litconv(SATSolve *s, int l)
{
	int v, m, n;
	SATVar *vp;
	SATLit *lp;
	
	m = l >> 31;
	v = (l + m ^ m) - 1;
	if(v >= s->nvaralloc){
		n = -(-(v+1) & -SATVARALLOC);
		s->var = vp = satrealloc(s, s->var, n * sizeof(SATVar));
		s->lit = lp = satrealloc(s, s->lit, 2 * n * sizeof(SATLit));
		memset(vp += s->nvaralloc, 0, (n - s->nvaralloc) * sizeof(SATVar));
		memset(lp += 2*s->nvaralloc, 0, 2 * (n - s->nvaralloc) * sizeof(SATLit));
		for(; vp < s->var + n; vp++){
			vp->lvl = -1;
			vp->flags = VARPHASE;
		}
		for(; lp < s->lit + 2 * n; lp++)
			lp->val = -1;
		s->nvaralloc = n;
	}
	if(v >= s->nvar)
		s->nvar = v + 1;
	return v << 1 | m & 1;
}

static void
addbimp(SATSolve *s, int l0, int l1)
{
	SATLit *lp;
	
	lp = &s->lit[NOT(l0)];
	lp->bimp = satrealloc(s, lp->bimp, (lp->nbimp + 1) * sizeof(int));
	lp->bimp[lp->nbimp++] = l1;
}

static SATSolve *
satadd1special(SATSolve *s, int *a, int n)
{
	int i, l0, l1;
	
	if(n == 0){
		s->unsat = 1;
		return s;
	}
	l0 = a[0];
	l1 = 0;
	for(i = 1; i < n; i++)
		if(a[i] != l0){
			l1 = a[i];
			break;
		}
	if(l1 == 0){
		l0 = litconv(s, l0);
		assert(s->lvl == 0);
		switch(s->lit[l0].val){
		case 0:
			s->unsat = 1;
			return s;
		case -1:
			s->trail = satrealloc(s, s->trail, sizeof(int) * s->nvar);
			memmove(&s->trail[1], s->trail, sizeof(int) * s->ntrail);
			s->trail[0] = l0;
			s->ntrail++;
			s->var[VAR(l0)].flags |= VARUSER;
			s->var[VAR(l0)].lvl = 0;
			s->lit[l0].val = 1;
			s->lit[NOT(l0)].val = 0;
		}
		return s;
	}
	if(l0 + l1 == 0) return s;
	l0 = litconv(s, l0);
	l1 = litconv(s, l1);
	addbimp(s, l0, l1);
	addbimp(s, l1, l0);
	return s;
}

SATSolve *
satadd1(SATSolve *s, int *a, int n)
{
	SATClause *c;
	int i, j, l, u;
	SATVar *v;

	if(s == nil){
		s = satnew();
		if(s == nil)
			saterror(nil, "satnew: %r");
	}
	if(n < 0)
		for(n = 0; a[n] != 0; n++)
			;
	for(i = 0; i < n; i++)
		if(a[i] == 0)
			saterror(s, "satadd1(%p, %p, %d): a[%d]==0, callerpc=%p", s, a, n, i, getcallerpc(&s));
	satbackjump(s, 0);
	if(n <= 2)
		return satadd1special(s, a, n);
	/* use stamps to detect repeated literals and tautological clauses */
	if(s->stamp >= (uint)-6){
		for(i = 0; i < s->nvar; i++)
			s->var[i].stamp = 0;
		s->stamp = 1;
	}else
		s->stamp += 3;
	u = 0;
	for(i = 0; i < n; i++){
		l = litconv(s, a[i]);
		v = &s->var[VAR(l)];
		if(v->stamp < s->stamp) u++;
		if(v->stamp == s->stamp + (~l & 1))
			return s; /* tautological */
		v->stamp = s->stamp + (l & 1);
	}
	if(u <= 2)
		return satadd1special(s, a, n);
	s->stamp += 3;
	c = satnewclause(s, u, 0);
	c->n = u;
	for(i = 0, j = 0; i < n; i++){
		l = litconv(s, a[i]);
		v = &s->var[VAR(l)];
		if(v->stamp < s->stamp){
			c->l[j++] = l;
			v->stamp = s->stamp;
		}
	}
	assert(j == u);
	s->ncl0++;
	return s;
}

void
satvafix(va_list va)
{
	int *d;
	uintptr *s;

	if(sizeof(int)==sizeof(uintptr)) return;
	d = (int *) va;
	s = (uintptr *) va;
	do
		*d++ = *s;
	while((int)*s++ != 0);
		
}

SATSolve *
sataddv(SATSolve *s, ...)
{
	va_list va;
	
	va_start(va, s);
	/* horrible hack */
	satvafix(va);
	s = satadd1(s, (int*)va, -1);
	va_end(va);
	return s;
}
