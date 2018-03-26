#include <u.h>
#include <libc.h>
#include <sat.h>
#include "impl.h"

int
satmore(SATSolve *s)
{
	int *a, i, n;

	if(s == nil) return 1;
	s->scrap = a = satrealloc(s, nil, s->nvar * sizeof(int));
	n = 0;
	for(i = 0; i < s->nvar; i++){
		if((s->var[i].flags & VARUSER) != 0) continue;
		switch(s->lit[2*i].val){
		case 0: a[n++] = i+1; break;
		case 1: a[n++] = -(i+1); break;
		}
	}
	if(n > 0)
		satadd1(s, a, n);
	if(n == 1)
		s->var[abs(a[0])-1].flags &= ~VARUSER;
	free(a);
	s->scrap = nil;
	return satsolve(s);
}
