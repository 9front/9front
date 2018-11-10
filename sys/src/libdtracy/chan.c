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
	free(ch);
}

int
dtcaddgr(DTChan *c, DTName name, DTActGr *gr)
{
	DTProbe **l, *p;
	DTEnab *ep;
	int i, nl, n;
	
	if(dtgverify(gr) < 0)
		return -1;
	gr->chan = c;
	
	nl = dtpmatch(name, &l);
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

static int
dtnamesplit(char *s, DTName *rp)
{
	char *p;
	
	p = strchr(s, ':');
	if(p == nil) return -1;
	rp->provider = dtmalloc(p - s + 1);
	memcpy(rp->provider, s, p - s);
	s = p + 1;
	p = strchr(s, ':');
	if(p == nil){
		free(rp->provider);
		rp->provider = nil;
		return -1;
	}
	rp->function = dtmalloc(p - s + 1);
	memcpy(rp->function, s, p - s);
	s = p + 1;
	if(strchr(s, ':') != nil){
		free(rp->provider);
		rp->provider = nil;
		free(rp->function);
		rp->function = nil;
		return -1;
	}
	rp->name = dtstrdup(s);
	return 0;
}

int
dtcaddcl(DTChan *c, DTClause *cl)
{
	DTName n;
	int i, rc;

	rc = 0;
	for(i = 0; i < cl->nprob; i++){
		if(dtnamesplit(cl->probs[i], &n) < 0){
			werrstr("invalid probe name '%s'", cl->probs[i]);
			return -1;
		}
		rc += dtcaddgr(c, n, cl->gr);
		dtfree(n.provider);
		dtfree(n.function);
		dtfree(n.name);
	}
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

void
dtcreset(DTChan *c)
{
	DTEnab *ep, *eq;
	
	for(ep = c->enab; ep != nil; ep = ep->channext){
		/* careful! has to look atomic for etptrigger */
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
