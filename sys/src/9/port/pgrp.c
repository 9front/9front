#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

enum {
	Whinesecs = 10,		/* frequency of out-of-resources printing */
};

static Ref pgrpid;
static Ref mountid;

void
pgrpnote(ulong noteid, char *a, long n, int flag)
{
	Proc *p, *ep;
	char buf[ERRMAX];

	if(n >= ERRMAX-1)
		error(Etoobig);

	memmove(buf, a, n);
	buf[n] = 0;
	p = proctab(0);
	for(ep = p+conf.nproc; p < ep; p++) {
		if(p->state == Dead)
			continue;
		if(up != p && p->noteid == noteid && p->kp == 0) {
			qlock(&p->debug);
			if(p->noteid == noteid)
				postnote(p, 0, buf, flag);
			qunlock(&p->debug);
		}
	}
}

Pgrp*
newpgrp(void)
{
	Pgrp *p;

	p = smalloc(sizeof(Pgrp));
	p->ref = 1;
	p->pgrpid = incref(&pgrpid);
	return p;
}

Rgrp*
newrgrp(void)
{
	Rgrp *r;

	r = smalloc(sizeof(Rgrp));
	r->ref = 1;
	return r;
}

void
closergrp(Rgrp *r)
{
	if(decref(r) == 0)
		free(r);
}

void
closepgrp(Pgrp *p)
{
	Mhead **h, **e, *f;
	Mount *m;

	if(decref(p))
		return;

	e = &p->mnthash[MNTHASH];
	for(h = p->mnthash; h < e; h++) {
		while((f = *h) != nil){
			*h = f->hash;
			wlock(&f->lock);
			m = f->mount;
			f->mount = nil;
			wunlock(&f->lock);
			mountfree(m);
			putmhead(f);
		}
	}
	free(p);
}

void
pgrpinsert(Mount **order, Mount *m)
{
	Mount *f;

	m->order = nil;
	if(*order == nil) {
		*order = m;
		return;
	}
	for(f = *order; f != nil; f = f->order) {
		if(m->mountid < f->mountid) {
			m->order = f;
			*order = m;
			return;
		}
		order = &f->order;
	}
	*order = m;
}

/*
 * pgrpcpy MUST preserve the mountid allocation order of the parent group
 */
void
pgrpcpy(Pgrp *to, Pgrp *from)
{
	Mount *n, *m, **link, *order;
	Mhead *f, **tom, **l, *mh;
	int i;

	wlock(&to->ns);
	rlock(&from->ns);
	order = nil;
	tom = to->mnthash;
	for(i = 0; i < MNTHASH; i++) {
		l = tom++;
		for(f = from->mnthash[i]; f != nil; f = f->hash) {
			rlock(&f->lock);
			mh = newmhead(f->from);
			*l = mh;
			l = &mh->hash;
			link = &mh->mount;
			for(m = f->mount; m != nil; m = m->next) {
				n = smalloc(sizeof(Mount));
				n->mountid = m->mountid;
				n->mflag = m->mflag;
				n->to = m->to;
				incref(n->to);
				if(m->spec != nil)
					kstrdup(&n->spec, m->spec);
				pgrpinsert(&order, n);
				*link = n;
				link = &n->next;
			}
			runlock(&f->lock);
		}
	}
	/*
	 * Allocate mount ids in the same sequence as the parent group
	 */
	for(m = order; m != nil; m = m->order)
		m->mountid = incref(&mountid);
	runlock(&from->ns);
	wunlock(&to->ns);
}

Fgrp*
dupfgrp(Fgrp *f)
{
	Fgrp *new;
	Chan *c;
	int i;

	new = smalloc(sizeof(Fgrp));
	if(f == nil){
		new->fd = smalloc(DELTAFD*sizeof(Chan*));
		new->nfd = DELTAFD;
		new->ref = 1;
		return new;
	}

	lock(f);
	/* Make new fd list shorter if possible, preserving quantization */
	new->nfd = f->maxfd+1;
	i = new->nfd%DELTAFD;
	if(i != 0)
		new->nfd += DELTAFD - i;
	new->fd = malloc(new->nfd*sizeof(Chan*));
	if(new->fd == nil){
		unlock(f);
		free(new);
		error("no memory for fgrp");
	}
	new->ref = 1;

	new->maxfd = f->maxfd;
	for(i = 0; i <= f->maxfd; i++) {
		if((c = f->fd[i]) != nil){
			incref(c);
			new->fd[i] = c;
		}
	}
	unlock(f);

	return new;
}

void
closefgrp(Fgrp *f)
{
	int i;
	Chan *c;

	if(f == nil || decref(f))
		return;

	/*
	 * If we get into trouble, forceclosefgrp
	 * will bail us out.
	 */
	up->closingfgrp = f;
	for(i = 0; i <= f->maxfd; i++)
		if((c = f->fd[i]) != nil){
			f->fd[i] = nil;
			cclose(c);
		}
	up->closingfgrp = nil;

	free(f->fd);
	free(f);
}

/*
 * Called from interrupted() because up is in the middle
 * of closefgrp and just got a kill ctl message.
 * This usually means that up has wedged because
 * of some kind of deadly embrace with mntclose
 * trying to talk to itself.  To break free, hand the
 * unclosed channels to the close queue.  Once they
 * are finished, the blocked cclose that we've 
 * interrupted will finish by itself.
 */
void
forceclosefgrp(void)
{
	int i;
	Chan *c;
	Fgrp *f;

	if(up->procctl != Proc_exitme || up->closingfgrp == nil){
		print("bad forceclosefgrp call");
		return;
	}

	f = up->closingfgrp;
	for(i = 0; i <= f->maxfd; i++)
		if((c = f->fd[i]) != nil){
			f->fd[i] = nil;
			ccloseq(c);
		}
}


Mount*
newmount(Chan *to, int flag, char *spec)
{
	Mount *m;

	m = smalloc(sizeof(Mount));
	m->to = to;
	incref(to);
	m->mountid = incref(&mountid);
	m->mflag = flag;
	if(spec != nil)
		kstrdup(&m->spec, spec);

	return m;
}

void
mountfree(Mount *m)
{
	Mount *f;

	while((f = m) != nil) {
		m = m->next;
		cclose(f->to);
		free(f->spec);
		free(f);
	}
}

void
resrcwait(char *reason)
{
	static ulong lastwhine;
	ulong now;
	char *p;

	if(up == nil)
		panic("resrcwait");

	p = up->psstate;
	if(reason != nil) {
		if(waserror()){
			up->psstate = p;
			nexterror();
		}
		up->psstate = reason;
		now = seconds();
		/* don't tie up the console with complaints */
		if(now - lastwhine > Whinesecs) {
			lastwhine = now;
			print("%s\n", reason);
		}
	}
	tsleep(&up->sleep, return0, 0, 100+nrand(200));
	if(reason != nil) {
		up->psstate = p;
		poperror();
	}
}
