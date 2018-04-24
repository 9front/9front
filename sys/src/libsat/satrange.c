#include <u.h>
#include <libc.h>
#include <sat.h>
#include "impl.h"

static SATSolve *
satmin(SATSolve *s, int *a, int n, int *id, int *l, int m, int mul)
{
	int i;
	
	if(m > n) return s;
	for(i = 0; i < m; i++)
		id[i] = i;
	for(;;){
		for(i = 0; i < m; i++)
			l[i] = a[id[i]] * mul;
		s = satadd1(s, l, m);
		for(i = m-1; i >= 0; i--){
			if(++id[i] < n+i+1-m)
				break;
			if(i == 0)
				return s;
		}
		while(++i < m)
			id[i] = id[i-1]+1;
	}
}

SATSolve *
satrange1(SATSolve *s, int *a, int n, int min, int max)
{
	int sz, na;
	
	if(s == nil){
		s = satnew();
		if(s == nil)
			saterror(nil, "satnew: %r");
	}
	if(n < 0)
		for(n = 0; a[n] != 0; n++)
			;
	if(min > n || max < 0)
		return sataddv(s, 0);
	if(min < 0) min = 0;
	if(max > n) max = n;
	sz = n+1-min;
	if(min == 0 || max != n && sz < max+1) sz = max+1;
	if(s->cflclalloc < 2*sz){
		na = -(-2*sz & -CFLCLALLOC);
		s->cflcl = satrealloc(s, s->cflcl, na * sizeof(int));
		s->cflclalloc = na;
	}
	s = satmin(s, a, n, s->cflcl, s->cflcl+sz, max+1, -1);
	s = satmin(s, a, n, s->cflcl, s->cflcl+sz, n+1-min, 1);
	return s;
}

SATSolve *
satrangev(SATSolve *s, int min, int max, ...)
{
	va_list va;
	
	va_start(va, max);
	/* horrible hack */
	satvafix(va);
	s = satrange1(s, (int*)va, -1, min, max);
	va_end(va);
	return s;
}
