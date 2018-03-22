#include <u.h>
#include <libc.h>
#include <sat.h>
#include "impl.h"

int
satget(SATSolve *s, int i, int *t, int n)
{
	SATClause *c;
	SATLit *l;
	int j, k, m;
	
	for(c = s->cl; c != s->learncl; c = c->next)
		if(i-- == 0){
			for(j = 0; j < n && j < c->n; j++)
				t[j] = signf(c->l[j]);
			return c->n;
		}
	for(l = s->lit; l < s->lit + 2 * s->nvar; l++)
		for(k = 0; k < l->nbimp; k++)
			if(l - s->lit < l->bimp[k] && i-- == 0){
				if(n > 0) t[0] = -signf(l - s->lit);
				if(n > 1) t[1] = signf(l->bimp[k]);
				return 2;
			}
	m = s->lvl == 0 ? s->ntrail : s->decbd[1];
	for(k = 0; k < m; k++)
		if(i-- == 0){
			if(n > 0) t[0] = signf(s->trail[k]);
			return 1;
		}
	for(; c != 0; c = c->next)
		if(i-- == 0){
			for(j = 0; j < n && j < c->n; j++)
				t[j] = signf(c->l[j]);
			return c->n;
		}
	return -1;
}
