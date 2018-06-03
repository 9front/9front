#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

enum
{
	DELTAENV = 32,
	Maxenvsize = 16300,
};

static Egrp	*envgrp(Chan *c);
static int	envwriteable(Chan *c);

static Egrp	confegrp;	/* global environment group containing the kernel configuration */

static Evalue*
envlookup(Egrp *eg, char *name, ulong qidpath)
{
	Evalue *e, *ee;

	e = eg->ent;
	for(ee = e + eg->nent; e < ee; e++){
		if(e->qid.path == qidpath
		|| (name != nil && name[0] == e->name[0] && strcmp(e->name, name) == 0))
			return e;
	}
	return nil;
}

static int
envgen(Chan *c, char *name, Dirtab*, int, int s, Dir *dp)
{
	Egrp *eg;
	Evalue *e;

	if(s == DEVDOTDOT){
		devdir(c, c->qid, "#e", 0, eve, 0775, dp);
		return 1;
	}

	eg = envgrp(c);
	rlock(eg);
	if(name != nil)
		e = envlookup(eg, name, -1);
	else if(s < eg->nent)
		e = &eg->ent[s];
	else
		e = nil;
	if(e == nil || name != nil && (strlen(e->name) >= sizeof(up->genbuf))) {
		runlock(eg);
		return -1;
	}

	/* make sure name string continues to exist after we release lock */
	kstrcpy(up->genbuf, e->name, sizeof up->genbuf);
	devdir(c, e->qid, up->genbuf, e->len, eve,
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
	return devwalk(c, nc, name, nname, 0, 0, envgen);
}

static int
envstat(Chan *c, uchar *db, int n)
{
	if(c->qid.type & QTDIR)
		c->qid.vers = envgrp(c)->vers;
	return devstat(c, db, n, 0, 0, envgen);
}

static Chan*
envopen(Chan *c, int omode)
{
	Egrp *eg;
	Evalue *e;
	int trunc;

	eg = envgrp(c);
	if(c->qid.type & QTDIR) {
		if(omode != OREAD)
			error(Eperm);
	}
	else {
		trunc = omode & OTRUNC;
		if(omode != OREAD && !envwriteable(c))
			error(Eperm);
		if(trunc)
			wlock(eg);
		else
			rlock(eg);
		e = envlookup(eg, nil, c->qid.path);
		if(e == nil) {
			if(trunc)
				wunlock(eg);
			else
				runlock(eg);
			error(Enonexist);
		}
		if(trunc && e->value != nil) {
			e->qid.vers++;
			free(e->value);
			e->value = nil;
			e->len = 0;
		}
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
	Evalue *e;

	if(c->qid.type != QTDIR || !envwriteable(c))
		error(Eperm);

	if(strlen(name) >= sizeof(up->genbuf))
		error(Etoolong);

	omode = openmode(omode);
	eg = envgrp(c);
	wlock(eg);
	if(waserror()) {
		wunlock(eg);
		nexterror();
	}

	if(envlookup(eg, name, -1) != nil)
		error(Eexist);

	if(eg->nent == eg->ment){
		Evalue *tmp;

		eg->ment += DELTAENV;
		if((tmp = realloc(eg->ent, sizeof(eg->ent[0])*eg->ment)) == nil){
			eg->ment -= DELTAENV;
			error(Enomem);
		}
		eg->ent = tmp;
	}
	eg->vers++;
	e = &eg->ent[eg->nent++];
	e->value = nil;
	e->len = 0;
	e->name = smalloc(strlen(name)+1);
	strcpy(e->name, name);
	mkqid(&e->qid, ++eg->path, 0, QTFILE);
	c->qid = e->qid;

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
	Evalue *e;

	if(c->qid.type & QTDIR || !envwriteable(c))
		error(Eperm);

	eg = envgrp(c);
	wlock(eg);
	e = envlookup(eg, nil, c->qid.path);
	if(e == nil){
		wunlock(eg);
		error(Enonexist);
	}
	free(e->name);
	free(e->value);
	*e = eg->ent[--eg->nent];
	eg->vers++;
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
	ulong offset = off;

	if(c->qid.type & QTDIR)
		return devdirread(c, a, n, 0, 0, envgen);

	eg = envgrp(c);
	rlock(eg);
	if(waserror()){
		runlock(eg);
		nexterror();
	}

	e = envlookup(eg, nil, c->qid.path);
	if(e == nil)
		error(Enonexist);
	if(offset >= e->len || e->value == nil)
		n = 0;
	else if(offset + n > e->len)
		n = e->len - offset;
	if(n <= 0)
		n = 0;
	else
		memmove(a, e->value+offset, n);

	runlock(eg);
	poperror();
	return n;
}

static long
envwrite(Chan *c, void *a, long n, vlong off)
{
	char *s;
	ulong len;
	Egrp *eg;
	Evalue *e;
	ulong offset = off;

	if(n <= 0)
		return 0;
	if(offset > Maxenvsize || n > (Maxenvsize - offset))
		error(Etoobig);

	eg = envgrp(c);
	wlock(eg);
	if(waserror()){
		wunlock(eg);
		nexterror();
	}

	e = envlookup(eg, nil, c->qid.path);
	if(e == nil)
		error(Enonexist);

	len = offset+n;
	if(len > e->len) {
		s = realloc(e->value, len);
		if(s == nil)
			error(Enomem);
		memset(s+offset, 0, n);
		e->value = s;
		e->len = len;
	}
	memmove(e->value+offset, a, n);
	e->qid.vers++;
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
	Evalue *e, *ee, *ne;

	rlock(from);
	to->ment = ROUND(from->nent, DELTAENV);
	to->ent = smalloc(to->ment*sizeof(to->ent[0]));
	ne = to->ent;
	e = from->ent;
	for(ee = e + from->nent; e < ee; e++, ne++){
		ne->name = smalloc(strlen(e->name)+1);
		strcpy(ne->name, e->name);
		if(e->value != nil){
			ne->value = smalloc(e->len);
			memmove(ne->value, e->value, e->len);
			ne->len = e->len;
		}
		mkqid(&ne->qid, ++to->path, 0, QTFILE);
	}
	to->nent = from->nent;
	runlock(from);
}

void
closeegrp(Egrp *eg)
{
	Evalue *e, *ee;

	if(decref(eg) == 0 && eg != &confegrp){
		e = eg->ent;
		for(ee = e + eg->nent; e < ee; e++){
			free(e->name);
			free(e->value);
		}
		free(eg->ent);
		free(eg);
	}
}

static Egrp*
envgrp(Chan *c)
{
	if(c->aux == nil)
		return up->egrp;
	return c->aux;
}

static int
envwriteable(Chan *c)
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
	Evalue *e, *ee;
	char *p, *q;
	int n;

	rlock(eg);
	if(waserror()) {
		runlock(eg);
		nexterror();
	}
	
	/* determine size */
	n = 0;
	e = eg->ent;
	for(ee = e + eg->nent; e < ee; e++)
		n += strlen(e->name) + e->len + 2;

	p = malloc(n + 1);
	if(p == nil)
		error(Enomem);
	q = p;
	e = eg->ent;
	for(ee = e + eg->nent; e < ee; e++){
		strcpy(q, e->name);
		q += strlen(q) + 1;
		memmove(q, e->value, e->len);
		q[e->len] = 0;
		/* move up to the first null */
		q += strlen(q) + 1;
	}
	*q = '\0';
	
	runlock(eg);
	poperror();
	return p;
}
