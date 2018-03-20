#include <u.h>
#include <libc.h>
#include <sat.h>
#include "impl.h"

/* the solver follows Algorithm C from Knuth's The Art of Computer Programming, Vol. 4, Fascicle 6 */

#define verbosestate 0
#define verboseforcing 0
#define verboseconflict 0
#define paranoia 0
#define sanity(s) if(paranoia) satsanity(s)

void
sataddtrail(SATSolve *s, int l)
{
	s->trail[s->ntrail++] = l;
	s->lit[l].val = 1;
	s->lit[NOT(l)].val = 0;
	s->var[VAR(l)].lvl = s->lvl;
	s->agility -= s->agility >> 13;
	if(((s->var[VAR(l)].flags ^ l) & 1) != 0)
		s->agility += 1<<19;
	if(verbosestate) satprintstate(s);
}

/* compute watchlists from scratch */
static void
rewatch(SATSolve *s)
{
	SATLit *l;
	SATClause *c;
	int i, j, x;
	
	for(l = s->lit; l < s->lit + 2*s->nvar; l++)
		l->watch = nil;
	for(c = s->cl; c != nil; c = c->next)
		for(i = 0; i < 2; i++){
			if(s->lit[c->l[i]].val == 0)
				for(j = 2; j < c->n; j++)
					if(s->lit[c->l[j]].val != 0){
						x = c->l[i], c->l[i] = c->l[j], c->l[j] = x;
						break;
					}
			c->watch[i] = s->lit[c->l[i]].watch;
			s->lit[c->l[i]].watch = c;
		}
}

/* jump back to decision level d */
void
satbackjump(SATSolve *s, int d)
{
	int l;
	SATVar *v;

	if(s->lvl == d) return;
	while(s->ntrail > s->decbd[d + 1]){
		l = s->trail[--s->ntrail];
		v = &s->var[VAR(l)];
		if((v->flags & VARUSER) != 0){ /* don't delete user assignments */
			s->ntrail++;
			break;
		}
		s->lit[l].val = -1;
		s->lit[NOT(l)].val = -1;
		v->flags = v->flags & ~1 | l & 1;
		v->lvl = -1;
		v->reason = nil;
		v->isbinreason = 0;
		if(v->heaploc < 0)
			satheapput(s, v);
	}
	s->lvl = d;
	if(s->forptr > s->ntrail) s->forptr = s->ntrail;
	if(s->binptr > s->ntrail) s->binptr = s->ntrail;
	if(verbosestate) satprintstate(s);
}

static void
solvinit(SATSolve *s)
{
	satdebuginit(s);
	satheapreset(s);
	s->decbd = satrealloc(s, s->decbd, (s->nvar + 1) * sizeof(int));
	s->decbd[0] = 0;
	s->trail = satrealloc(s, s->trail, sizeof(int) * s->nvar);
	s->fullrlits = satrealloc(s, s->fullrlits, sizeof(int) * s->nvar);
	s->lvlstamp = satrealloc(s, s->lvlstamp, sizeof(int) * s->nvar);
	memset(s->lvlstamp, 0, sizeof(int) * s->nvar);
	if(s->cflclalloc == 0){
		s->cflcl = satrealloc(s, s->cflcl, CFLCLALLOC * sizeof(int));
		s->cflclalloc = CFLCLALLOC;
	}
	rewatch(s);
	
	s->conflicts = 0;
	s->nextpurge = s->purgeΔ;
	s->purgeival = s->purgeΔ;
	s->nextflush = 1;
	s->flushu = 1;
	s->flushv = 1;
	s->flushθ = s->flushψ;
	s->agility = 0;
	
	satbackjump(s, 0);
	s->forptr = 0;
	s->binptr = 0;
}

void
satcleanup(SATSolve *s, int all)
{
	SATBlock *b, *bn;
	
	if(all){
		*s->lastp[0] = nil;
		s->learncl = nil;
		s->lastp[1] = &s->learncl;
		s->ncl = s->ncl0;
	}
	for(b = s->bl[1].next; b != &s->bl[1]; b = bn){
		bn = b->next;
		if(b->last != nil && !all) continue;
		b->next->prev = b->prev;
		b->prev->next = b->next;
		free(b);
	}
	s->lastbl = s->bl[1].prev;
	free(s->fullrlits);
	s->fullrlits = nil;
	free(s->lvlstamp);
	s->lvlstamp = nil;
	free(s->cflcl);
	s->cflcl = nil;
	s->cflclalloc = 0;
}

static void
stampoverflow(SATSolve *s)
{
	int i;
	
	for(i = 0; i < s->nvar; i++){
		s->var[i].stamp = 0;
		s->lvlstamp[i] = 0;
	}
	s->stamp = -2;
}

/* "bump" the variable, i.e. increase its activity score. reduce all score when one exceeds MAXACTIVITY (1e100) */
static void
varbump(SATSolve *s, SATVar *v)
{
	v->activity += s->Δactivity;
	satreheap(s, v);
	if(v->activity < MAXACTIVITY) return;
	for(v = s->var; v < s->var + s->nvar; v++)
		if(v->activity != 0){
			v->activity /= MAXACTIVITY;
			if(v->activity < ε)
				v->activity = ε;
		}
	s->Δactivity /= MAXACTIVITY;
}

/* ditto for clauses */
static void
clausebump(SATSolve *s, SATClause *c)
{
	c->activity += s->Δclactivity;
	if(c->activity < MAXACTIVITY) return;
	for(c = s->cl; c != nil; c = c->next)
		if(c->activity != 0){
			c->activity /= MAXACTIVITY;
			if(c->activity < ε)
				c->activity = ε;
		}
	s->Δclactivity /= MAXACTIVITY;
}

/* pick a literal. normally we pick the variable with highest activity from the heap. sometimes we goof and pick a random one. */
static void
decision(SATSolve *s)
{
	SATVar *v;

	s->decbd[++s->lvl] = s->ntrail;
	if((uint)s->randfn(s->randaux) < s->goofprob){
		v = s->heap[satnrand(s, s->nheap)];
		if(v->lvl < 0)
			goto gotv;
	}
	do
		v = satheaptake(s);
	while(v->lvl >= 0);
gotv:
	sataddtrail(s, 2 * (v - s->var) + (v->flags & VARPHASE));
}

/* go through the watchlist of a literal that just turned out false. */
/* full == 1 records the first conflict and goes on rather than aborting immediately */
static SATClause *
forcing(SATSolve *s, int l, int full)
{
	SATClause **cp, *rc, *c, *xp;
	int v0;
	int x, j;
	
	cp = &s->lit[l].watch;
	rc = nil;
	if(verboseforcing) print("forcing literal %d\n", signf(l));
	while(c = *cp, c != nil){
		if(l == c->l[0]){
			/* this swap implies that the reason r for a literal l always has r->l[0]==l */
			x = c->l[1], c->l[1] = c->l[0], c->l[0] = x;
			xp = c->watch[1], c->watch[1] = c->watch[0], c->watch[0] = xp;
		}
		assert(c->l[1] == l);
		v0 = s->lit[c->l[0]].val;
		if(v0 > 0) /* the clause is true anyway */
			goto next;
		for(j = 2; j < c->n; j++)
			if(s->lit[c->l[j]].val != 0){
				/* found another literal to watch for this clause */
				if(verboseforcing) print("moving clause %+Γ onto watchlist %d\n", c, signf(c->l[j]));
				*cp = c->watch[1];
				x = c->l[j], c->l[j] = c->l[1], c->l[1] = x;
				c->watch[1] = s->lit[x].watch;
				s->lit[x].watch = c;
				goto cont;
			}
		if(v0 == 0){
			/* conflict */
			if(!full) return c;
			if(rc == nil) rc = c;
			goto next;
		}
		if(verboseforcing) print("inferring %d using clause %+Γ\n", signf(c->l[0]), c);
		sataddtrail(s, c->l[0]);
		s->var[VAR(c->l[0])].reason = c;
	next:
		cp = &c->watch[1];
	cont: ;
	}
	return rc;
}

/* forcing() for binary implications */
static uvlong
binforcing(SATSolve *s, int l, int full)
{
	SATLit *lp;
	int i, m;
	uvlong rc;
	
	lp = &s->lit[l];
	rc = 0;
	if(verboseforcing && lp->nbimp > 0) print("forcing literal %d (binary)\n", signf(l));
	for(i = 0; i < lp->nbimp; i++){
		m = lp->bimp[i];
		switch(s->lit[m].val){
		case -1:
			if(verboseforcing) print("inferring %d using binary clause (%d) ∨ %d\n", signf(m), -signf(l), signf(m));
			sataddtrail(s, m);
			s->var[VAR(m)].binreason = NOT(l);
			s->var[VAR(m)].isbinreason = 1;
			break;
		case 0:
			if(verboseforcing) print("conflict (%d) ∨ (%d)\n", -signf(l), signf(m));
			if(rc == 0) rc = (uvlong)NOT(l) << 32 | (uint)m;
			if(!full) return rc;
			break;
		}
	}
	return rc;
}

/* check if we can discard the previously learned clause because the current one subsumes it */
static int
checkdiscard(SATSolve *s)
{
	SATClause *c;
	SATVar *v;
	int q, j;
	
	if(s->lastp[1] == &s->learncl) return 0;
	c = (SATClause*) ((uchar*) s->lastp[1] - (uchar*) &((SATClause*)0)->next);
	if(s->lit[c->l[0]].val >= 0) return 0; /* clause is a reason, hands off */
	q = s->ncflcl;
	for(j = c->n - 1; q > 0 && j >= q; j--){
		v = &s->var[VAR(c->l[j])];
		/* check if literal is in the current clause */
		if(c->l[j] == s->cflcl[0] || (uint)v->lvl <= s->cfllvl && v->stamp == s->stamp)
			q--;
	}
	return q == 0;
}

/* add the clause we just learned to our collection */
static SATClause *
learn(SATSolve *s, int notriv)
{
	SATClause *r;
	int i, l, triv;
	
	/* clauses that are too complicated are not worth it. learn the trivial clause (all decisions negated) instead */
	if(triv = !notriv && s->ncflcl > s->lvl + s->trivlim){
		assert(s->lvl + 1 <= s->cflclalloc);
		for(i = 1; i <= s->lvl; i++)
			s->cflcl[i] = NOT(s->trail[s->decbd[s->lvl + 1 - i]]);
		s->ncflcl = s->lvl + 1;
	}
	if(s->ncflcl == 1) /* unit clauses are handled by putting them on the trail in conflict() */
		return nil;
	if(!triv && checkdiscard(s))
		r = satreplclause(s, s->ncflcl);
	else
		r = satnewclause(s, s->ncflcl, 1);
	r->n = s->ncflcl;
	memcpy(r->l, s->cflcl, s->ncflcl * sizeof(int));
	for(i = 0; i < 2; i++){
		l = r->l[i];
		r->watch[i] = s->lit[l].watch;
		s->lit[l].watch = r;
	}
	return r;
}

/* recursive procedure to determine if a literal is redundant.
 * to avoid repeated work, each known redundant literal is stamped with stamp+1
 * and each known nonredundant literal is stamped with stamp+2.
 */
static int
redundant(SATSolve *s, int l)
{
	SATVar *v, *w;
	SATClause *c;
	int i, r;
	
	v = &s->var[VAR(l)];
	if(v->isbinreason){
		/* stupid special case code */
		r = v->binreason;
		w = &s->var[VAR(r)];
		if(w->lvl != 0){
			if(w->stamp == s->stamp + 2)
				return 0;
			if(w->stamp < s->stamp && (s->lvlstamp[w->lvl] < s->stamp || !redundant(s, r))){
				w->stamp = s->stamp + 2;
				return 0;
			}
		}
		v->stamp = s->stamp + 1;
		return 1;
	}
	if(v->reason == nil) return 0; /* decision literals are never redundant */
	c = v->reason;
	for(i = 0; i < c->n; i++){
		if(c->l[i] == NOT(l)) continue;
		w = &s->var[VAR(c->l[i])];
		if(w->lvl == 0)
			continue; /* literals at level 0 are redundant */
		if(w->stamp == s->stamp + 2)
			return 0;
		/* if the literal is not in the clause or known redundant, check if it is redundant */
		/* we can skip the check if the level is not stamped: */
		/* if there are no literals at the same level in the clause, it must be nonredundant */
		if(w->stamp < s->stamp && (s->lvlstamp[w->lvl] < s->stamp || !redundant(s, c->l[i]))){
			w->stamp = s->stamp + 2;
			return 0;
		}
	}
	v->stamp = s->stamp + 1;
	return 1;
}

/* "blitting" a literal means to either add it to the conflict clause
 * (if v->lvl < s->lvl) or to increment the counter of literals to be
 * resolved, plus some bookkeeping.  */
static void
blit(SATSolve *s, int l)
{
	SATVar *v;
	int p;
	
	v = &s->var[VAR(l)];
	if(v->stamp == s->stamp) return;
	v->stamp = s->stamp;
	p = v->lvl;
	if(p == 0) return;
	if(verboseconflict) print("stamp %d %s (ctr=%d)\n", signf(l), p == s->lvl ? "and increment" : "and collect", s->cflctr);
	varbump(s, v);
	if(p == s->lvl){
		s->cflctr++;
		return;
	}
	if(s->ncflcl >= s->cflclalloc){
		s->cflcl = satrealloc(s, s->cflcl, (s->cflclalloc + CFLCLALLOC) * sizeof(int));
		s->cflclalloc += CFLCLALLOC;
	}
	s->cflcl[s->ncflcl++] = l;
	if(p > s->cfllvl) s->cfllvl = p;
	/* lvlstamp[p] == stamp if there is exactly one literal and ==stamp+1 if there is more than one literal on level p */
	if(s->lvlstamp[p] <= s->stamp)
		s->lvlstamp[p] = s->stamp + (s->lvlstamp[p] == s->stamp);
}

/* to resolve a conflict, we start with the conflict clause and use
 * resolution (a ∨ b and ¬a ∨ c imply b ∨ c) with the reasons for the
 * literals to remove all but one literal at the current level.  this
 * gives a new "learned" clause with all literals false and we jump back
 * to the second-highest level in it.  at this point, the clause implies
 * the one remaining literal and we can continue.
 * to do this quickly, rather than explicitly apply resolution, we keep a
 * counter of literals at the highest level (unresolved literals) and an
 * array with all other literals (which will become the learned clause). */
static void
conflict(SATSolve *s, SATClause *c, uvlong bin, int full)
{
	int i, j, l, p, *nl, found;
	SATVar *v;
	SATClause *r;

	if(verboseconflict) satprintstate(s);
	/* choose a new unique stamp value */
	if(s->stamp >= (uint)-3)
		stampoverflow(s);
	s->stamp += 3;
	s->ncflcl = 1;
	s->cflctr = 0;
	s->cfllvl = 0;
	/* we start by blitting each literal in the conflict clause */
	if(c != nil){
		clausebump(s, c);
		for(i = 0; i < c->n; i++)
			blit(s, c->l[i]);
		/* if there is only one literal l at the current level, we should have inferred ¬l at a lower level (bug). */
		if(s->cflctr <= 1){
			satprintstate(s);
			print("conflict clause %+Γ\n", c);
			assert(s->cflctr > 1);
		}
	}else{
		blit(s, bin);
		blit(s, bin>>32);
		if(s->cflctr <= 1){
			satprintstate(s);
			print("binary conflict clause %d ∨ %d\n", (int)(bin>>32), (int)bin);
			assert(s->cflctr > 1);
		}
	}
	/* now we go backwards through the trail, decrementing the unresolved literal counter at each stamped literal */
	/* and blitting the literals in their reason */
	for(i = s->ntrail; --i >= 0; ){
		v = &s->var[VAR(s->trail[i])];
		if(v->stamp != s->stamp) continue;
		if(verboseconflict) print("trail literal %d\n", signf(s->trail[i]));
		if(--s->cflctr == 0) break;
		if(v->isbinreason)
			blit(s, v->binreason);
		else if((r = v->reason) != nil){
			clausebump(s, r);
			for(j = 0; j < r->n; j++)
				blit(s, r->l[j]);
		}
	}
	/* i should point to the one remaining literal at the current level */
	assert(i >= 0);
	nl = s->cflcl;
	nl[0] = NOT(s->trail[i]);
	found = 0;
	/* delete redundant literals. note we must watch a literal at cfllvl, so put it in l[1]. */
	for(i = 1, j = 1; i < s->ncflcl; i++){
		l = nl[i];
		p = s->var[VAR(nl[i])].lvl;
		/* lvlstamp[p] != s->stamp + 1 => only one literal at level p => must be nonredundant */
		if(s->lvlstamp[p] != s->stamp + 1 || !redundant(s, l))
			if(found || p < s->cfllvl)
				nl[j++] = nl[i];
			else{
				/* watch this literal */
				l = nl[i], nl[j++] = nl[1], nl[1] = l;
				found = 1;
			}
	}
	s->ncflcl = j;
	if(!full){
		/* normal mode: jump back and add to trail right away */
		satbackjump(s, s->cfllvl);
		sataddtrail(s, nl[0]);
	}else{
		/* purging: record minimum cfllvl and literals at that level */
		if(s->cfllvl < s->fullrlvl){
			s->fullrlvl = s->cfllvl;
			s->nfullrlits = 0;
		}
		s->fullrlits[s->nfullrlits++] = nl[0];
	}
	r = learn(s, full);
	if(!full && r != nil)
		s->var[VAR(nl[0])].reason = r;
	if(verboseconflict)
		if(r != nil)
			print("learned %+Γ\n", r);
		else
			print("learned %d\n", signf(nl[0]));
	s->Δactivity *= s->varρ;
	s->Δclactivity *= s->clauseρ;
	s->conflicts++;
}

/* to purge, we need a fullrun that assigns values to all variables.
 * during this we record the first conflict at each level, to be resolved
 * later.  otherwise this is just a copy of the main loop which never
 * purges or flushes. */
static int
fullrun(SATSolve *s)
{
	int l;
	uvlong b;
	SATClause *c;

	while(s->ntrail < s->nvar){
		decision(s);
	re:
		while(s->binptr < s->ntrail){
			l = s->trail[s->binptr++];
			b = binforcing(s, l, 1);
			if(b != 0){
				if(s->lvl == 0){
					s->unsat = 1;
					return -1;
				}
				if(s->nfullrcfl == 0 || s->lvl > CFLLVL(s->fullrcfl[s->nfullrcfl-1])){
					s->fullrcfl = satrealloc(s, s->fullrcfl, sizeof(SATConflict) * (s->nfullrcfl + 1));
					s->fullrcfl[s->nfullrcfl].lvl = 1<<31 | s->lvl;
					s->fullrcfl[s->nfullrcfl++].b = b;
				}
			}
			sanity(s);
		}
		while(s->forptr < s->ntrail){
			l = s->trail[s->forptr++];
			c = forcing(s, NOT(l), 1);
			if(c != nil){
				if(s->lvl == 0){
					s->unsat = 1;
					return -1;
				}
				if(s->nfullrcfl == 0 || s->lvl > CFLLVL(s->fullrcfl[s->nfullrcfl-1])){
					s->fullrcfl = satrealloc(s, s->fullrcfl, sizeof(SATConflict) * (s->nfullrcfl + 1));
					s->fullrcfl[s->nfullrcfl].lvl = s->lvl;
					s->fullrcfl[s->nfullrcfl++].c = c;
				}
			}
		}
		if(s->binptr < s->ntrail) goto re;
	}
	return 0;
}

/* assign range scores to all clauses.
 * p == number of levels that have positive literals in the clause.
 * r == number of levels that have literals in the clause.
 * range == min(floor(16 * (p + α (r - p))), 255) with magic constant α. */
static void
ranges(SATSolve *s)
{
	SATClause *c;
	int p, r, k, l, v;
	uint ci;

	ci = 2;
	memset(s->lvlstamp, 0, sizeof(int) * s->nvar);
	memset(s->rangecnt, 0, sizeof(s->rangecnt));
	for(c = s->learncl; c != nil; c = c->next, ci += 2){
		if(!s->var[VAR(c->l[0])].binreason && s->var[VAR(c->l[0])].reason == c){
			c->range = 0;
			s->rangecnt[0]++;
			continue;
		}
		p = 0;
		r = 0;
		for(k = 0; k < c->n; k++){
			l = c->l[k];
			v = s->var[VAR(l)].lvl;
			if(v == 0){
				if(s->lit[l].val == 1){
					c->range = 256;
					goto next;
				}
			}else{
				if(s->lvlstamp[v] < ci){
					s->lvlstamp[v] = ci;
					r++;
				}
				if(s->lvlstamp[v] == ci && s->lit[l].val == 1){
					s->lvlstamp[v] = ci + 1;
					p++;
				}
			}
		}
		r = 16 * (p + s->purgeα * (r - p));
		if(r > 255) r = 255;
		c->range = r;
		s->rangecnt[r]++;
	next: ;
	}
}

/* resolve conflicts found during fullrun() */
static void
fullrconflicts(SATSolve *s)
{
	SATConflict *cfl;
	int i;
	
	s->fullrlvl = s->lvl;
	s->nfullrlits = 0;
	for(cfl = &s->fullrcfl[s->nfullrcfl - 1]; cfl >= s->fullrcfl; cfl--){
		satbackjump(s, CFLLVL(*cfl));
		if(cfl->lvl < 0)
			conflict(s, nil, cfl->b, 1);
		else
			conflict(s, cfl->c, 0, 1);
	}
	satbackjump(s, 0);
	if(s->fullrlvl == 0)
		for(i = 0; i < s->nfullrlits; i++)
			sataddtrail(s, s->fullrlits[i]);
	free(s->fullrcfl);
	s->fullrcfl = nil;
}

/* note that nil > *, this simplifies the algorithm by having nil "bubble" to the top */
static int
actgt(SATClause *a, SATClause *b)
{
	if(b == nil) return 0;
	if(a == nil) return 1;
	return a->activity > b->activity || a->activity == b->activity && a > b;
}

/* select n clauses to keep
 * first we find the upper limit j on the range score
 * to get the exact number, we move htot clauses from j to j+1
 * to this end, we put them in a max-heap of size htot, sorted by activity,
 * continually replacing the largest element if we find a less active clause.
 * the heap starts out filled with nil and the nil are replaced during the first
 * htot iterations. */
#define LEFT(i) (2*(i)+1)
#define RIGHT(i) (2*(i)+2)
static int
judgement(SATSolve *s, int n)
{
	int cnt, i, j, htot, m;
	SATClause *c, **h, *z;
	
	cnt = 0;
	for(j = 0; j < 256; j++){
		cnt += s->rangecnt[j];
		if(cnt >= n) break;
	}
	if(j == 256) return j;
	if(cnt > n){
		htot = cnt - n;
		h = satrealloc(s, nil, sizeof(SATClause *) * htot);
		memset(h, 0, sizeof(SATClause *) * htot);
		for(c = s->learncl; c != nil; c = c->next){
			if(c->range != j || actgt(c, h[0])) continue;
			h[0] = c;
			m = 0;
			for(;;){
				i = m;
				if(LEFT(i) < htot && actgt(h[LEFT(i)], h[m])) m = LEFT(i);
				if(RIGHT(i) < htot && actgt(h[RIGHT(i)], h[m])) m = RIGHT(i);
				if(i == m) break;
				z = h[i], h[i] = h[m], h[m] = z;
			}
		}
		for(i = 0; i < htot; i++)
			if(h[i] != nil)
				h[i]->range = j + 1;
		free(h);
	}
	return j;
}

/* during purging we remove permanently false literals from learned clauses.
 * returns 1 if the clause can be deleted entirely. */
static int
cleanupclause(SATSolve *s, SATClause *c)
{
	int i, k;
	
	for(i = 0; i < c->n; i++)
		if(s->lit[c->l[i]].val == 0)
			break;
	if(i == c->n) return 0;
	for(k = i; i < c->n; i++)
		if(s->lit[c->l[i]].val != 0)
			c->l[k++] = c->l[i];
	c->n = k;
	if(k > 1) return 0;
	if(k == 0)
		s->unsat = 1;
	else if(s->lit[c->l[0]].val < 0)
		sataddtrail(s, c->l[0]);
	return 1;
}

/* delete clauses by overwriting them. don't delete empty blocks; we're going to fill them up soon enough again. */
static void
execution(SATSolve *s, int j)
{
	SATClause *c, *n, **cp, *p;
	SATBlock *b;
	SATVar *v0;
	int f, sz;

	b = s->bl[1].next;
	p = (SATClause*) b->data;
	s->ncl = s->ncl0;
	cp = &s->learncl;
	for(c = p; c != nil; c = n){
		n = c->next;
		if(c->range > j || cleanupclause(s, c))
			continue;
		sz = sizeof(SATClause) + (c->n - 1) * sizeof(int);
		f = (uchar*)b + SATBLOCKSZ - (uchar*)p;
		if(f < sz){
			memset(p, 0, f);
			b = b->next;
			assert(b != &s->bl[1]);
			p = (SATClause *) b->data;
		}
		b->last = p;
		/* update reason field of the first variable (if applicable) */
		v0 = &s->var[VAR(c->l[0])];
		if(!v0->isbinreason && v0->reason == c)
			v0->reason = p;
		memmove(p, c, sz);
		*cp = p;
		cp = &p->next;
		p = (void*)((uintptr)p + sz + CLAUSEALIGN - 1 & -CLAUSEALIGN);
		b->end = p;
		s->ncl++;
	}
	*cp = nil;
	*s->lastp[0] = s->learncl;
	s->lastp[1] = cp;
	s->lastbl = b;
	f = (uchar*)b + SATBLOCKSZ - (uchar*)p;
	memset(p, 0, f);
	for(b = b->next; b != &s->bl[1]; b = b->next){
		b->last = nil;
		b->end = b->data;
	}
}

static void
thepurge(SATSolve *s)
{
	int nkeep, i, j;
	SATVar *v;
	
	s->purgeival += s->purgeδ;
	s->nextpurge = s->conflicts + s->purgeival;
	nkeep = (s->ncl - s->ncl0) / 2;
	for(i = 0; i < s->ntrail; i++){
		v = &s->var[VAR(s->trail[i])];
		if(!v->isbinreason && v->reason != nil)
			nkeep++;
	}
	if(nkeep <= 0) return; /* shouldn't happen */
	s->nfullrcfl = 0;
	if(fullrun(s) < 0){ /* accidentally determined UNSAT during fullrun() */
		free(s->fullrcfl);
		s->fullrcfl = nil;
		return;
	}
	ranges(s);
	fullrconflicts(s);
	j = judgement(s, nkeep);
	execution(s, j);
	rewatch(s);
}

/* to avoid getting stuck, flushing backs up the trail to remove low activity variables.
 * don't worry about throwing out high activity ones, they'll get readded quickly. */
static void
theflush(SATSolve *s)
{
	double actk;
	int dd, l;

	/* "reluctant doubling" wizardry to determine when to flush */
	if((s->flushu & -s->flushu) == s->flushv){
		s->flushu++;
		s->flushv = 1;
		s->flushθ = s->flushψ;
	}else{
		s->flushv *= 2;
		s->flushθ += s->flushθ >> 4;
	}
	s->nextflush = s->conflicts + s->flushv;
	if(s->agility > s->flushθ) return; /* don't flush when we're too busy */
	/* clean up the heap so that a free variable is at the top */
	while(s->nheap > 0 && s->heap[0]->lvl >= 0)
		satheaptake(s);
	if(s->nheap == 0) return; /* shouldn't happen */
	actk = s->heap[0]->activity;
	for(dd = 0; dd < s->lvl; dd++){
		l = s->trail[s->decbd[dd+1]];
		if(s->var[VAR(l)].activity < actk)
			break;
	}
	satbackjump(s, dd);
}

int
satsolve(SATSolve *s)
{
	int l;
	SATClause *c;
	uvlong b;

	if(s == nil) return 1;
	if(s->scratched) return -1;
	if(s->nvar == 0) return 1;
	solvinit(s);
	
	while(!s->unsat){
	re:
		while(s->binptr < s->ntrail){
			l = s->trail[s->binptr++];
			b = binforcing(s, l, 0);
			sanity(s);
			if(b != 0){
				if(s->lvl == 0) goto unsat;
				conflict(s, nil, b, 0);
				sanity(s);
			}
		}
		while(s->forptr < s->ntrail){
			l = s->trail[s->forptr++];
			c = forcing(s, NOT(l), 0);
			sanity(s);
			if(c != nil){
				if(s->lvl == 0) goto unsat;
				conflict(s, c, 0, 0);
				sanity(s);
			}
		}
		if(s->binptr < s->ntrail) goto re;
		if(s->ntrail == s->nvar) goto out;
		if(s->conflicts >= s->nextpurge)
			thepurge(s);
		else if(s->conflicts >= s->nextflush)
			theflush(s);
		else
			decision(s);
	}
unsat:
	s->unsat = 1;
out:
	satcleanup(s, 0);
	return !s->unsat;
}

void
satreset(SATSolve *s)
{
	int i;

	if(s == nil || s->decbd == nil) return;
	satbackjump(s, -1);
	s->lvl = 0;
	for(i = 0; i < s->nvar; i++){
		s->var[i].activity = 0;
		s->var[i].flags |= VARPHASE;
	}
	satcleanup(s, 1);
	s->Δactivity = 1;
	s->Δclactivity = 1;
}
