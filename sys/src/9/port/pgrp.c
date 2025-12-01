#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

enum {
	Whinesecs = 10,		/* frequency of out-of-resources printing */
};

Pgrp*
newpgrp(void)
{
	Pgrp *p;

	p = malloc(sizeof(Pgrp));
	if(p == nil)
		error(Enomem);
	p->ref = 1;
	p->mntordertail = &p->mntorder;
	return p;
}

Rgrp*
newrgrp(void)
{
	Rgrp *r;

	r = malloc(sizeof(Rgrp));
	if(r == nil)
		error(Enomem);
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
pgrpinsert(Pgrp *pg, Mount *m)
{
	m->order = nil;
	*pg->mntordertail = m;
	pg->mntordertail = &m->order;
}

void
pgrpremove(Pgrp *pg, Mount *m)
{
	Mount *f, **l = &pg->mntorder;

	for(f = pg->mntorder; f != nil; f = f->order) {
		if(f == m){
			if((*l = f->order) == nil)
				pg->mntordertail = l;
			f->order = nil;
			return;
		}
		l = &f->order;
	}
}

void
pgrpcpy(Pgrp *to, Pgrp *from, int flag)
{
	Mount *n, *m, **link;
	Mhead *f, **l, *mh;
	int i;

	wlock(&to->ns);
	wlock(&from->ns);	/* must wlock to protect from->mntorder->norder */
	if(waserror()){
		wunlock(&from->ns);
		wunlock(&to->ns);
		nexterror();
	}

	/* always inherit devmask unconditionally */
	memmove(to->devmask, from->devmask, sizeof from->devmask);

	if((flag & RFNAMEG) == 0)
		goto Done;

	for(i = 0; i < MNTHASH; i++) {
		l = &to->mnthash[i];
		for(f = from->mnthash[i]; f != nil; f = f->hash) {
			rlock(&f->lock);
			if(waserror()){
				runlock(&f->lock);
				nexterror();
			}
			mh = newmhead(f->from);
			*l = mh;
			l = &mh->hash;
			link = &mh->mount;
			for(m = f->mount; m != nil; m = m->next) {
				n = newmount(m->to, m->mflag, m->spec);
				n->umh = mh;
				*link = n;
				link = &n->next;
				m->norder = n;	/* for from->mntorder loop below */
			}
			runlock(&f->lock);
			poperror();
		}
	}
	/* add mounts in original mntorder */
	for(m = from->mntorder; m != nil; m = m->order){
		n = m->norder;
		m->norder = nil;
		pgrpinsert(to, n);
	}
Done:
	wunlock(&from->ns);
	wunlock(&to->ns);
	poperror();
}

int
canmount(Pgrp *pgrp)
{
	/*
	 * Devmnt is not usable directly from user procs, so
	 * having it masked is interpreted to block any mounts.
	 */
	return !devmasked(pgrp, devno('M'));
}

int
devmasked(Pgrp *pgrp, int i)
{
	return (pgrp->devmask[i>>3] & 1<<(i&7)) != 0;
}

void
devmask(Pgrp *pgrp, int invert, char *devs)
{
	uchar mask[sizeof pgrp->devmask];
	Rune r;
	int i;

	if(invert)
		invert = 0xFF;

	memset(mask, 0, sizeof mask);		
	while(*devs != '\0') {
		devs += chartorune(&r, devs);
		i = devno(r);
		if(i < 0)
			continue;
		mask[i>>3] |= 1<<(i&7);
	}

	wlock(&pgrp->ns);
	for(i=0; i < sizeof mask; i++)
		pgrp->devmask[i] |= mask[i] ^ invert;
	wunlock(&pgrp->ns);
}

Fgrp*
dupfgrp(Fgrp *f)
{
	Fgrp *new;
	Chan *c;
	int i;

	new = malloc(sizeof(Fgrp));
	if(new == nil)
		error(Enomem);
	new->ref = 1;
	if(f == nil){
		new->nfd = DELTAFD;
		new->fd = malloc(DELTAFD*sizeof(new->fd[0]));
		new->flag = malloc(DELTAFD*sizeof(new->flag[0]));
		if(new->fd == nil || new->flag == nil){
			free(new->flag);
			free(new->fd);
			free(new);
			error(Enomem);
		}
		return new;
	}

	lock(f);
	/* Make new fd list shorter if possible, preserving quantization */
	new->nfd = f->maxfd+1;
	i = new->nfd%DELTAFD;
	if(i != 0)
		new->nfd += DELTAFD - i;
	new->fd = malloc(new->nfd*sizeof(new->fd[0]));
	new->flag = malloc(new->nfd*sizeof(new->flag[0]));
	if(new->fd == nil || new->flag == nil){
		unlock(f);
		free(new->flag);
		free(new->fd);
		free(new);
		error(Enomem);
	}
	new->maxfd = f->maxfd;
	for(i = 0; i <= f->maxfd; i++) {
		if((c = f->fd[i]) != nil){
			new->fd[i] = c;
			new->flag[i] = f->flag[i];
			incref(c);
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

	free(f->flag);
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

	if(spec == nil)
		spec = "";
	m = malloc(sizeof(Mount)+strlen(spec)+1);
	if(m == nil)
		error(Enomem);
	m->to = to;
	incref(to);
	m->mflag = flag;
	strcpy(m->spec, spec);
	setmalloctag(m, getcallerpc(&to));
	return m;
}

void
mountfree(Mount *m)
{
	Mount *f;

	while((f = m) != nil) {
		m = m->next;
		cclose(f->to);
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
		panic("resrcwait: %s", reason);

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
