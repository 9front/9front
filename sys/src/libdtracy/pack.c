#include <u.h>
#include <libc.h>
#include <dtracy.h>

static void
dtepack(Fmt *f, DTExpr *e)
{
	int i;

	fmtprint(f, "e%d\n", e->n);
	for(i = 0; i < e->n; i++)
		fmtprint(f, "%#.8ux\n", e->b[i]);
}

void
dtgpack(Fmt *f, DTActGr *g)
{
	int i;

	fmtprint(f, "g%ud\n", g->id);
	if(g->pred != nil){
		fmtprint(f, "p");
		dtepack(f, g->pred);
	}
	fmtprint(f, "a%d\n", g->nact);
	for(i = 0; i < g->nact; i++){
		fmtprint(f, "t%d\n", g->acts[i].type);
		fmtprint(f, "s%d\n", g->acts[i].size);
		dtepack(f, g->acts[i].p);
		switch(g->acts[i].type){
		case ACTAGGKEY:
		case ACTAGGVAL:
			fmtprint(f, "A%#.8ux\n", g->acts[i].agg.id);
			break;
		}
	}
	fmtprint(f, "G");
}

void
dtclpack(Fmt *f, DTClause *c)
{
	int i;

	fmtprint(f, "c%d\n", c->nprob);
	for(i = 0; i < c->nprob; i++)
		fmtprint(f, "%s\n", c->probs[i]);
	dtgpack(f, c->gr);
}

static char *
u32unpack(char *s, u32int *np)
{
	char *r;
	
	*np = strtoul(s, &r, 0);
	if(r == s || *r != '\n') return nil;
	return r + 1;
}

static char *
dteunpack(char *s, DTExpr **rp)
{
	int i;
	u32int n;
	DTExpr *e;

	*rp = nil;
	if(*s++ != 'e') return nil;
	s = u32unpack(s, &n);
	if(s == nil) return nil;
	e = dtmalloc(sizeof(DTExpr) + n * sizeof(u32int));
	e->n = n;
	e->b = (void*)(e + 1);
	for(i = 0; i < n; i++){
		s = u32unpack(s, &e->b[i]);
		if(s == nil){
			dtfree(e);
			return nil;
		}
	}
	*rp = e;
	return s;
}

void
dtgfree(DTActGr *g)
{
	int i;

	if(g == nil) return;
	for(i = 0; i < g->nact; i++)
		dtfree(g->acts[i].p);
	dtfree(g->acts);
	dtfree(g->pred);
	dtfree(g);

}

char *
dtgunpack(char *s, DTActGr **rp)
{
	DTActGr *g;
	u32int n;
	int i;

	*rp = nil;
	g = dtmalloc(sizeof(DTActGr));
	g->reclen = 12;
	g->ref = 1;
	if(*s++ != 'g') goto fail;
	s = u32unpack(s, &g->id);
	if(s == nil) goto fail;
	for(;;)
		switch(*s++){
		case 'p':
			s = dteunpack(s, &g->pred);
			if(s == nil) goto fail;
			break;
		case 'a':
			s = u32unpack(s, &n);
			if(s == nil) goto fail;
			g->acts = dtmalloc(n * sizeof(DTAct));
			g->nact = n;
			for(i = 0; i < n; i++){
				if(*s++ != 't') goto fail;
				s = u32unpack(s, (u32int *) &g->acts[i].type);
				if(s == nil) goto fail;
				if(*s++ != 's') goto fail;
				s = u32unpack(s, (u32int *) &g->acts[i].size);
				if(s == nil) goto fail;
				s = dteunpack(s, &g->acts[i].p);
				if(s == nil) goto fail;
				switch(g->acts[i].type){
				case ACTTRACE:
					g->reclen += g->acts[i].size;
					break;
				case ACTTRACESTR:
					g->reclen += g->acts[i].size;
					break;
				case ACTAGGKEY:
					if(*s++ != 'A') goto fail;
					s = u32unpack(s, (u32int *) &g->acts[i].agg.id);
					if(s == nil) goto fail;
					break;
				case ACTAGGVAL:
					if(*s++ != 'A') goto fail;
					s = u32unpack(s, (u32int *) &g->acts[i].agg.id);
					if(s == nil) goto fail;
					break;
				case ACTCANCEL:
					break;
				default:
					goto fail;
				}
			}
			break;
		case 'G':
			*rp = g;
			return s;
		default: goto fail;
		}
fail:
	dtgfree(g);
	return nil;
}

char *
dtclunpack(char *s, DTClause **rp)
{
	DTClause *c;
	char *e;
	int i;
	
	*rp = nil;
	c = dtmalloc(sizeof(DTClause));
	if(*s++ != 'c') goto fail;
	s = u32unpack(s, (u32int*) &c->nprob);
	if(s == nil) goto fail;
	c->probs = dtmalloc(sizeof(char *) * c->nprob);
	for(i = 0; i < c->nprob; i++){
		e = strchr(s, '\n');
		if(e == nil) goto fail;
		c->probs[i] = dtmalloc(e - s + 1);
		memmove(c->probs[i], s, e - s);
		s = e + 1;
	}
	s = dtgunpack(s, &c->gr);
	if(s == nil) goto fail;
	*rp = c;
	return s;
fail:
	dtclfree(c);
	return nil;
}

void
dtclfree(DTClause *c)
{
	int i;

	if(c == nil) return;
	if(c->gr != nil && --c->gr->ref == 0)
		dtgfree(c->gr);
	for(i = 0; i < c->nprob; i++)
		free(c->probs[i]);
	free(c->probs);
	free(c);
}
