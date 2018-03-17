#include <u.h>
#include <libc.h>
#include <sat.h>
#include "impl.h"

static SATSolve *globsat;

static int
satclausefmt(Fmt *f)
{
	SATClause *c;
	char *s;
	int i, fl;
	
	fl = f->flags;
	c = va_arg(f->args, SATClause *);
	if(c == nil)
		return fmtstrcpy(f, "Λ");
	if(c->n == 0)
		return fmtstrcpy(f, "ε");
	s = "%s%d";
	for(i = 0; i < c->n; i++){
		if((fl & FmtSign) != 0)
			switch(globsat->lit[c->l[i]].val){
			case 1: s = "%s[%d]"; break;
			case 0: s = "%s(%d)"; break;
			case -1: s = "%s%d"; break;
			default: abort();
			}
		fmtprint(f, s, i != 0 ? " ∨ " : "", signf(c->l[i]));
	}
	return 0;
}

void
satprintstate(SATSolve *s)
{
	int i;
	Fmt f;
	char buf[512];
	SATVar *v;
	
	fmtfdinit(&f, 1, buf, sizeof(buf));
	fmtprint(&f, "trail:\n");
	for(i = 0; i < s->ntrail; i++){
		v = &s->var[VAR(s->trail[i])];
		fmtprint(&f, "%c%-8d %- 8d %-8d ", i == s->forptr ? '*' : ' ', i, signf(s->trail[i]), v->lvl);
		if(v->isbinreason)
			fmtprint(&f, "%d ∨ %d\n", signf(s->trail[i]), signf(v->binreason));
		else
			fmtprint(&f, "%+Γ\n", v->reason);
	}
	fmtrune(&f, '\n');
	fmtfdflush(&f);
}

void
satsanity(SATSolve *s)
{
	int i, j, k, m, tl, s0, s1;
	SATVar *v;
	SATLit *l;
	SATClause *c;
	
	for(c = s->cl; c != nil; c = c->next){
		assert(c->n >= 2);
		assert((uint)((uchar*)c->next - (uchar*)c) >= sizeof(SATClause) + (c->n - 1) * sizeof(int));
		for(j = 0; j < c->n; j++)
			assert((uint)c->l[j] < 2*s->nvar);
		for(i = 0; i < 2; i++)
			c->watch[i] = (void*)((uintptr)c->watch[i] | 1);
	}
	for(i = 0; i < s->nvar; i++){
		tl = -1;
		for(j = 0; j < s->ntrail; j++)
			if(VAR(s->trail[j]) == i){
				assert(tl == -1);
				tl = j;
			}
		v = &s->var[i];
		l = &s->lit[2*i];
		if(l->val >= 0){
			assert(l->val <= 1);
			assert(l[0].val + l[1].val == 1);
			assert((uint)v->lvl <= s->lvl);
			assert(tl != -1);
			assert(s->trail[tl] == 2*i+l[1].val);
			assert(tl >= s->decbd[v->lvl]);
			assert(v->lvl == s->lvl || tl < s->decbd[v->lvl+1]);
		}else{
			assert(l[0].val == -1 && l[1].val == -1);
			assert(v->lvl == -1);
			assert(v->heaploc >= 0);
			assert(tl == -1);
		}
		assert(v->heaploc == -1 || (uint)v->heaploc <= s->nheap && s->heap[v->heaploc] == v);
		for(j = 0; j < 2; j++){
			m = 2 * i + j;
			for(c = l[j].watch; c != nil; c = c->watch[k]){
				k = c->l[1] == m;
				assert(k || c->l[0] == m);
				assert((uintptr)c->watch[k] & 1);
				c->watch[k] = (void*)((uintptr)c->watch[k] & ~1);
			}
		}
	}
	for(c = s->cl; c != nil; c = c->next)
		for(i = 0; i < 2; i++)
			assert(((uintptr)c->watch[i] & 1) == 0);
	if(s->forptr == s->ntrail)
		for(c = s->cl; c != nil; c = c->next){
			s0 = s->lit[c->l[0]].val;
			s1 = s->lit[c->l[1]].val;
			if(s0 != 0 && s1 != 0 || s0 == 1 || s1 == 1)
				continue;
			for(i = 2; i < c->n; i++)
				if(s->lit[c->l[i]].val != 0){
					satprintstate(s);
					print("watchlist error: %+Γ\n", c);
					assert(0);
				}
		}
}

void
satdebuginit(SATSolve *s)
{
	globsat = s;
	fmtinstall(L'Γ', satclausefmt);
}
