#include <u.h>
#include <libc.h>
#include <dtracy.h>

int dtnmach;

void
dtinit(int nmach)
{
	DTProvider **p;

	dtnmach = nmach;
	
	/* sanity */
	for(p = dtproviders; *p != nil; p++){
		assert((*p)->name != nil);
		assert((*p)->provide != nil);
		assert((*p)->enable != nil);
		assert((*p)->disable != nil);
	}
}

void
dtsync(void)
{
	int i;
	
	for(i = 0; i < dtnmach; i++){
		dtmachlock(i);
		dtmachunlock(i);
	}
}

DTChan *
dtcnew(void)
{
	DTChan *c;
	int i;
	
	c = dtmalloc(sizeof(DTChan));
	c->rdbufs = dtmalloc(sizeof(DTBuf *) * dtnmach);
	c->wrbufs = dtmalloc(sizeof(DTBuf *) * dtnmach);
	for(i = 0; i < dtnmach; i++){
		c->rdbufs[i] = dtmalloc(sizeof(DTBuf));
		c->wrbufs[i] = dtmalloc(sizeof(DTBuf));
	}
	c->aggrdbufs = dtmalloc(sizeof(DTBuf *) * dtnmach);
	c->aggwrbufs = dtmalloc(sizeof(DTBuf *) * dtnmach);
	for(i = 0; i < dtnmach; i++){
		c->aggrdbufs[i] = dtmalloc(sizeof(DTBuf));
		c->aggwrbufs[i] = dtmalloc(sizeof(DTBuf));
		memset(c->aggrdbufs[i]->data, -1, DTBUFSZ);
		memset(c->aggwrbufs[i]->data, -1, DTBUFSZ);
	}
	return c;
}

void
dtcfree(DTChan *ch)
{
	int i;

	if(ch == nil) return;

	dtcrun(ch, DTCSTOP);
	dtcreset(ch);
	dtsync();
	for(i = 0; i < dtnmach; i++){
		free(ch->rdbufs[i]);
		free(ch->wrbufs[i]);
	}
	free(ch->rdbufs);
	free(ch->wrbufs);
	for(i = 0; i < dtnmach; i++){
		free(ch->aggrdbufs[i]);
		free(ch->aggwrbufs[i]);
	}
	free(ch->aggrdbufs);
	free(ch->aggwrbufs);
	free(ch);
}

int
dtcaddgr(DTChan *c, char *name, DTActGr *gr)
{
	DTProbe **l, *p;
	DTEnab *ep;
	int i, nl, n;
	
	if(dtgverify(c, gr) < 0)
		return -1;
	gr->chan = c;
	
	nl = dtpmatch(name, &l);
	if(nl == 0){
		dtfree(l);
		werrstr("no match for %s", name);
		return -1;
	}
	n = 0;
	for(i = 0; i < nl; i++){
		p = l[i];
		if(p->nenable == 0)
			if(p->prov->enable(p) < 0)
				continue;
		ep = dtmalloc(sizeof(DTEnab));
		ep->epid = c->epidalloc++;
		ep->gr = gr;
		ep->prob = p;
		ep->probnext = &p->enablist;
		ep->probprev = p->enablist.probprev;
		ep->probnext->probprev = ep;
		ep->channext = c->enab;
		c->enab = ep;
		gr->ref++;
		n++;
		p->nenable++;
		/* careful, has to be atomic for dtptrigger */
		dtcoherence();
		ep->probprev->probnext = ep;
	}
	dtfree(l);
	return n;
}

int
dtcaddcl(DTChan *c, DTClause *cl)
{
	int i, rc;

	rc = 0;
	for(i = 0; i < cl->nprob; i++)
		rc += dtcaddgr(c, cl->probs[i], cl->gr);
	return rc;
}

static void
dtcbufswap(DTChan *c, int n)
{
	DTBuf *z;

	dtmachlock(n);
	z = c->rdbufs[n];
	c->rdbufs[n] = c->wrbufs[n];
	c->wrbufs[n] = z;
	dtmachunlock(n);
}

int
dtcread(DTChan *c, void *buf, int n)
{
	int i, swapped;
	
	if(c->state == DTCFAULT){
		werrstr("%s", c->errstr);
		return -1;
	}
	for(i = 0; i < dtnmach; i++){
		if(swapped = c->rdbufs[i]->wr == 0)
			dtcbufswap(c, i);
		if(c->rdbufs[i]->wr != 0){
			if(c->rdbufs[i]->wr > n){
				werrstr("short read");
				return -1;
			}
			n = c->rdbufs[i]->wr;
			memmove(buf, c->rdbufs[i]->data, n);
			c->rdbufs[i]->wr = 0;
			if(!swapped)
				dtcbufswap(c, i);
			return n;
		}
	}
	return 0;
}

static void
dtcaggbufswap(DTChan *c, int n)
{
	DTBuf *z;

	dtmachlock(n);
	z = c->aggrdbufs[n];
	c->aggrdbufs[n] = c->aggwrbufs[n];
	c->aggwrbufs[n] = z;
	dtmachunlock(n);
}

int
dtcaggread(DTChan *c, void *buf, int n)
{
	int i, swapped;
	
	if(c->state == DTCFAULT){
		werrstr("%s", c->errstr);
		return -1;
	}
	for(i = 0; i < dtnmach; i++){
		if(swapped = c->aggrdbufs[i]->wr == 0)
			dtcaggbufswap(c, i);
		if(c->aggrdbufs[i]->wr != 0){
			if(c->aggrdbufs[i]->wr > n){
				werrstr("short read");
				return -1;
			}
			n = c->aggrdbufs[i]->wr;
			memmove(buf, c->aggrdbufs[i]->data, n);
			c->aggrdbufs[i]->wr = 0;
			memset(c->aggrdbufs[i]->data + DTABUCKETS, -1, 4 * DTANUMBUCKETS);
			if(!swapped)
				dtcaggbufswap(c, i);
			return n;
		}
	}
	return 0;
}

void
dtcreset(DTChan *c)
{
	DTEnab *ep, *eq;
	
	for(ep = c->enab; ep != nil; ep = ep->channext){
		/* careful! has to look atomic for dtptrigger */
		ep->probprev->probnext = ep->probnext;
		ep->probnext->probprev = ep->probprev;
	}
	dtsync();
	for(ep = c->enab; ep != nil; eq = ep->channext, free(ep), ep = eq){
		if(--ep->gr->ref == 0)
			dtgfree(ep->gr);
		if(--ep->prob->nenable == 0)
			ep->prob->prov->disable(ep->prob);
	}
	c->enab = nil;
}

void
dtcrun(DTChan *c, int newstate)
{
	assert(newstate == DTCSTOP || newstate == DTCGO);
	c->state = newstate;
}
