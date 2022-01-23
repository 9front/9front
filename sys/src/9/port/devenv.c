#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

enum
{
	Maxenvsize = 1*MB,
	Maxvalsize = Maxenvsize/2,

	DELTAENV = 32,
};

static Egrp	*envgrp(Chan *c);
static int	envwritable(Chan *c);

static Egrp	confegrp;	/* global environment group containing the kernel configuration */

#define PATH(p,i)	((uvlong)(p) << 32 | (i))
#define QID(quidpath)	((uint)(quidpath) & 0x7FFFFFFF)

static Evalue*
envindex(Egrp *eg, uvlong qidpath)
{
	Evalue *e;
	int i;

	i = QID(qidpath);
	if(i >= eg->nent)
		return nil;
	e = eg->ent[i];
	if(e != nil && e->path != qidpath)
		return nil;
	return e;
}

static Evalue**
envhash(Egrp *eg, char *name)
{
	uint c, h = 0;
	while(c = *name++)
		h = h*131 + c;
	return &eg->hash[h % ENVHASH];
}

static Evalue*
lookupname(Evalue *e, char *name)
{
	while(e != nil){
		if(strcmp(e->name, name) == 0)
			break;
		e = e->hash;
	}
	return e;
}

static int
envgen(Chan *c, char *name, Dirtab*, int, int s, Dir *dp)
{
	Egrp *eg;
	Evalue *e;
	Qid q;

	eg = envgrp(c);
	if(s == DEVDOTDOT){
		c->qid.vers = eg->vers;
		devdir(c, c->qid, "#e", 0, eve, 0775, dp);
		return 1;
	}
	rlock(eg);
	if((c->qid.type & QTDIR) == 0) {
		e = envindex(eg, c->qid.path);
		if(e == nil)
			goto Notfound;
	} else if(name != nil) {
		if(strlen(name) >= sizeof(up->genbuf))
			goto Notfound;
		e = lookupname(*envhash(eg, name), name);
		if(e == nil)
			goto Notfound;
	} else if(s < eg->nent) {
		e = eg->ent[s];
		if(e == nil) {
			runlock(eg);
			return 0;	/* deleted, try next */
		}
	} else {
Notfound:
		runlock(eg);
		return -1;
	}
	/* make sure name string continues to exist after we release lock */
	kstrcpy(up->genbuf, e->name, sizeof(up->genbuf));
	mkqid(&q, e->path, e->vers, QTFILE);
	devdir(c, q, up->genbuf, e->len, eve,
		eg == &confegrp || eg != up->egrp ? 0664: 0666, dp);
	runlock(eg);
	return 1;
}

static Chan*
envattach(char *spec)
{
	Chan *c;
	Egrp *egrp = nil;

	if(spec != nil && *spec != '\0') {
		if(strcmp(spec, "c") != 0)
			error(Ebadarg);
		egrp = &confegrp;
	}

	c = devattach('e', spec);
	c->aux = egrp;
	return c;
}

static Walkqid*
envwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, nil, 0, envgen);
}

static int
envstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, nil, 0, envgen);
}

static void*
envrealloc(Egrp *eg, void *old, int newsize)
{
	int oldsize = old != nil ? msize(old) : 0;
	void *new;

	if(newsize == 0){
		eg->alloc -= oldsize;
		free(old);
		return nil;
	}
	if(newsize < 0 || eg != &confegrp && (eg->alloc + newsize) - oldsize > Maxenvsize)
		error(Enomem);
	new = realloc(old, newsize);
	if(new == nil)
		error(Enomem);
	eg->alloc += msize(new) - oldsize;
	setmalloctag(new, getcallerpc(&eg));
	return new;
}

static Chan*
envopen(Chan *c, int omode)
{
	Egrp *eg;
	Evalue *e;

	eg = envgrp(c);
	if(c->qid.type & QTDIR) {
		if(omode != OREAD)
			error(Eperm);
	}
	else {
		int trunc = omode & OTRUNC;
		if(omode != OREAD && !envwritable(c))
			error(Eperm);
		if(trunc)
			wlock(eg);
		else
			rlock(eg);
		e = envindex(eg, c->qid.path);
		if(e == nil) {
			if(trunc)
				wunlock(eg);
			else
				runlock(eg);
			error(Enonexist);
		}
		if(trunc && e->len > 0) {
			e->value = envrealloc(eg, e->value, 0);	/* free */
			e->len = 0;
			e->vers++;
		}
		c->qid.vers = e->vers;
		if(trunc)
			wunlock(eg);
		else
			runlock(eg);
	}
	c->mode = openmode(omode);
	incref(eg);
	c->aux = eg;
	c->offset = 0;
	c->flag |= COPEN;
	return c;
}

static Chan*
envcreate(Chan *c, char *name, int omode, ulong)
{
	Egrp *eg;
	Evalue *e, **h;
	int n, i;

	if(c->qid.type != QTDIR || !envwritable(c))
		error(Eperm);

	n = strlen(name)+1;
	if(n > sizeof(up->genbuf))
		error(Etoolong);

	omode = openmode(omode);
	eg = envgrp(c);
	wlock(eg);
	if(waserror()) {
		wunlock(eg);
		nexterror();
	}

	h = envhash(eg, name);
	if(lookupname(*h, name) != nil)
		error(Eexist);

	for(i = eg->low; i < eg->nent; i++)
		if(eg->ent[i] == nil)
			break;

	if(i >= eg->nent){
		if((eg->nent % DELTAENV) == 0)
			eg->ent = envrealloc(eg, eg->ent, (eg->nent+DELTAENV) * sizeof(Evalue*));
		i = eg->nent++;
		eg->ent[i] = nil;
		eg->low = i;
	}

	e = envrealloc(eg, nil, sizeof(Evalue)+n);
	memmove(e->name, name, n);
	e->value = nil;
	e->len = 0;
	e->vers = 0;
	e->path = PATH(++eg->path, i);
	e->hash = *h, *h = e;
	eg->ent[i] = e;
	eg->low = i+1;
	eg->vers++;
	mkqid(&c->qid, e->path, e->vers, QTFILE);
	wunlock(eg);
	poperror();
	incref(eg);
	c->aux = eg;
	c->offset = 0;
	c->mode = omode;
	c->flag |= COPEN;
	return c;
}

static void
envremove(Chan *c)
{
	Egrp *eg;
	Evalue *e, **h;
	int i;

	if(c->qid.type & QTDIR || !envwritable(c))
		error(Eperm);

	eg = envgrp(c);
	wlock(eg);
	e = envindex(eg, c->qid.path);
	if(e == nil){
		wunlock(eg);
		error(Enonexist);
	}
	for(h = envhash(eg, e->name); *h != nil; h = &(*h)->hash){
		if(*h == e){
			*h = e->hash;
			break;
		}
	}
	i = QID(c->qid.path);
	eg->ent[i] = nil;
	if(i < eg->low)
		eg->low = i;
	eg->vers++;

	/* free */
	envrealloc(eg, e->value, 0);
	envrealloc(eg, e, 0);

	wunlock(eg);
}

static void
envclose(Chan *c)
{
	if(c->flag & COPEN){
		/*
		 * cclose can't fail, so errors from remove will be ignored.
		 * since permissions aren't checked,
		 * envremove can't not remove it if its there.
		 */
		if(c->flag & CRCLOSE && !waserror()){
			envremove(c);
			poperror();
		}
		closeegrp((Egrp*)c->aux);
		c->aux = nil;
	}
}

static long
envread(Chan *c, void *a, long n, vlong off)
{
	Egrp *eg;
	Evalue *e;

	if(c->qid.type & QTDIR)
		return devdirread(c, a, n, nil, 0, envgen);

	eg = envgrp(c);
	rlock(eg);
	if(waserror()){
		runlock(eg);
		nexterror();
	}
	e = envindex(eg, c->qid.path);
	if(e == nil)
		error(Enonexist);
	if(off >= e->len)
		n = 0;
	else if(off + n > e->len)
		n = e->len - off;
	if(n <= 0)
		n = 0;
	else
		memmove(a, e->value+off, n);
	runlock(eg);
	poperror();
	return n;
}

static long
envwrite(Chan *c, void *a, long n, vlong off)
{
	Egrp *eg;
	Evalue *e;
	int diff;

	eg = envgrp(c);
	wlock(eg);
	if(waserror()){
		wunlock(eg);
		nexterror();
	}
	e = envindex(eg, c->qid.path);
	if(e == nil)
		error(Enonexist);
	if(off > Maxvalsize || n > (Maxvalsize - off))
		error(Etoobig);
	diff = (off+n) - e->len;
	if(diff > 0)
		e->value = envrealloc(eg, e->value, e->len+diff);
	else
		diff = 0;
	memmove(e->value+off, a, n);	/* might fault */
	if(off > e->len)
		memset(e->value+e->len, 0, off-e->len);
	e->len += diff;
	e->vers++;
	eg->vers++;
	wunlock(eg);
	poperror();
	return n;
}

Dev envdevtab = {
	'e',
	"env",

	devreset,
	devinit,
	devshutdown,
	envattach,
	envwalk,
	envstat,
	envopen,
	envcreate,
	envclose,
	envread,
	devbread,
	envwrite,
	devbwrite,
	envremove,
	devwstat,
};

void
envcpy(Egrp *to, Egrp *from)
{
	Evalue *e, *ne, **h;
	int i, n;

	rlock(from);
	if(waserror()){
		runlock(from);
		nexterror();
	}
	to->nent = 0;
	to->ent = envrealloc(to, nil, ROUND(from->nent, DELTAENV) * sizeof(Evalue*));
	for(i = 0; i < from->nent; i++){
		e = from->ent[i];
		if(e == nil)
			continue;
		h = envhash(to, e->name);
		n = strlen(e->name)+1;
		ne = envrealloc(to, nil, sizeof(Evalue)+n);
		memmove(ne->name, e->name, n);
		ne->value = nil;
		ne->len = 0;
		ne->vers = 0;
		ne->path = PATH(++to->path, to->nent);
		ne->hash = *h, *h = ne;
		to->ent[to->nent++] = ne;
		if(e->len > 0){
			ne->value = envrealloc(to, ne->value, e->len);
			memmove(ne->value, e->value, e->len);
			ne->len = e->len;
		}
	}
	to->low = to->nent;
	runlock(from);
	poperror();
}

void
closeegrp(Egrp *eg)
{
	Evalue *e;
	int i;

	if(decref(eg) || eg == &confegrp)
		return;
	for(i = 0; i < eg->nent; i++){
		e = eg->ent[i];
		if(e == nil)
			continue;
		free(e->value);
		free(e);
	}
	free(eg->ent);
	free(eg);
}

static Egrp*
envgrp(Chan *c)
{
	if(c->aux == nil)
		return up->egrp;
	return c->aux;
}

static int
envwritable(Chan *c)
{
	return c->aux == nil || c->aux == up->egrp || iseve();
}

/*
 *  to let the kernel set environment variables
 */
void
ksetenv(char *ename, char *eval, int conf)
{
	Chan *c;
	char buf[2*KNAMELEN];
	
	snprint(buf, sizeof(buf), "#e%s/%s", conf?"c":"", ename);
	c = namec(buf, Acreate, OWRITE, 0666);
	devtab[c->type]->write(c, eval, strlen(eval), 0);
	cclose(c);
}

/*
 * Return a copy of configuration environment as a sequence of strings.
 * The strings alternate between name and value.  A zero length name string
 * indicates the end of the list
 */
char *
getconfenv(void)
{
	Egrp *eg = &confegrp;
	Evalue *e;
	char *p, *q;
	int i, n;

	rlock(eg);
	n = 1;
	for(i = 0; i < eg->nent; i++){
		e = eg->ent[i];
		if(e == nil)
			continue;
		n += strlen(e->name)+e->len+2;
	}
	p = malloc(n);
	if(p == nil){
		runlock(eg);
		error(Enomem);
	}
	q = p;
	for(i = 0; i < eg->nent; i++){
		e = eg->ent[i];
		if(e == nil)
			continue;
		n = strlen(e->name)+1;
		memmove(q, e->name, n);
		q += n;
		memmove(q, e->value, e->len);
		q[e->len] = 0;
		/* move up to the first null */
		q += strlen(q) + 1;
	}
	*q = '\0';
	runlock(eg);

	return p;
}
