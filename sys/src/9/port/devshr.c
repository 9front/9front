#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

typedef struct Shr Shr;

struct Shr
{
	Ref;
	char	*name;
	char	*owner;
	ulong	perm;
	Shr	*link;
	uvlong	path;
	Mhead	umh; /* only lock and mount are used */
};

static QLock	shrlk;
static Shr	*shr;
static int	qidpath;
static int	mntid;

static void
shrdecref(Shr *sp)
{
	Mount *m, *mm;
	int n;

	qlock(&shrlk);
	n = decref(sp);
	qunlock(&shrlk);
	if(n != 0)
		return;
	
	for(m = sp->umh.mount; m != nil; m = mm) {
		cclose(m->to);
		mm = m->next;
		free(m);
	}
	free(sp->owner);
	free(sp->name);
	free(sp);
}

static int
shrgen(Chan *c, char*, Dirtab*, int, int s, Dir *dp)
{
	Shr *sp;
	Qid q;

	if(s == DEVDOTDOT){
		devdir(c, c->qid, "#σ", 0, eve, 0555, dp);
		return 1;
	}

	qlock(&shrlk);
	for(sp = shr; sp && s; sp = sp->link)
		s--;

	if(sp == 0) {
		qunlock(&shrlk);
		return -1;
	}

	mkqid(&q, sp->path, 0, QTDIR);
	/* make sure name string continues to exist after we release lock */
	kstrcpy(up->genbuf, sp->name, sizeof up->genbuf);
	devdir(c, q, up->genbuf, 0, sp->owner, sp->perm, dp);
	qunlock(&shrlk);
	return 1;
}

static void
shrinit(void)
{
	qidpath = 1;
}

static Chan*
shrattach(char *spec)
{
	Chan *c;
	
	if(!(spec[0] == 'c' && spec[1] == 0 || spec[0] == 0))
		error(Enoattach);

	c = devattach(L'σ', spec);
	if(spec[0] == 'c')
		c->dev = 1;
	else
		c->dev = 0;
	return c;
}

static Shr*
shrlookup(char *name, uvlong qidpath)
{
	Shr *sp;
	qidpath &= 0xFFFFFFFF00000000ULL;
	for(sp = shr; sp; sp = sp->link)
		if(sp->path == qidpath || (name && strcmp(sp->name, name) == 0))
			return sp;
	return nil;
}

static Mount*
mntlookup(Shr *sp, char *name, uvlong qidpath)
{
	Mount *m;
	qidpath &= 0xFFFFFFFFULL;
	for(m = sp->umh.mount; m != nil; m = m->next)
		if(m->mountid == qidpath || (name && strcmp((char*)(m + 1), name) == 0))
			return m;
	return nil;
}

static int
shrcnfgen(Chan *c, char*, Dirtab*, int, int s, Dir *dp)
{
	Shr *sp;
	Mount *m;
	Qid q;
	
	qlock(&shrlk);
	sp = shrlookup(nil, c->qid.path);
	if(sp == nil){
		qunlock(&shrlk);
		return -1;
	}
	rlock(&sp->umh.lock);
	for(m = sp->umh.mount; m != nil && s > 0; m = m->next)
		s--;
	if(m == nil){
		runlock(&sp->umh.lock);
		qunlock(&shrlk);
		return -1;
	}
	kstrcpy(up->genbuf, (char*)(m + 1), sizeof up->genbuf);
	mkqid(&q, sp->path | m->mountid, 0, QTFILE);
	devdir(c, q, up->genbuf, 0, sp->owner, sp->perm, dp);
	runlock(&sp->umh.lock);
	qunlock(&shrlk);
	return 1;
}

static int
shrremovemnt(Shr *sp, int id)
{
	Mount *m, **l;

	wlock(&sp->umh.lock);
	l = &sp->umh.mount;
	for(m = *l; m; m = m->next){
		if(m->mountid == id){
			cclose(m->to);
			*l = m->next;
			free(m);
			break;
		}
		l = &m->next;
	}

	if(m == nil){
		wunlock(&sp->umh.lock);
		return -1;
	}
	wunlock(&sp->umh.lock);
	return 0;
}

static Walkqid*
shrwalk(Chan *c, Chan *nc, char **name, int nname)
{
	Walkqid *wq, *wq2;
	Shr *sp;
	int alloc, j;
	char *n;
	Mount *f;
	
	if(nname > 0)
		isdir(c);

	alloc = 0;
	wq = smalloc(sizeof(Walkqid) + (nname - 1) * sizeof(Qid));
	if(waserror()){
		if(alloc && wq->clone != nil)
			cclose(wq->clone);
		free(wq);
		return nil;
	}
	if(nc == nil){
		nc = devclone(c);
		nc->type = 0;
		alloc = 1;
	}
	nc->aux = nil;
	wq->clone = nc;
	for(j = 0; j < nname; j++){
		if(!(nc->qid.type & QTDIR)){
			if(j == 0)
				error(Enotdir);
			kstrcpy(up->errstr, Enotdir, ERRMAX);
			goto Done;
		}
		n = name[j];
		if(n[0] == '.' && n[1] == 0)
			USED(n);
		else if(n[0] == '.' && n[1] == '.' && n[2] == 0){
			if(nc->qid.path != 0)
				nc->qid.path = 0;
			nc->qid.type = QTDIR;
		} else if(nc->qid.path == 0) {
			qlock(&shrlk);
			sp = shrlookup(n, -1);
			if(sp != nil){
				if(waserror()){
					qunlock(&shrlk);
					if(j == 0)
						nexterror();
					goto Done;
				}
				devpermcheck(sp->owner, sp->perm, OEXEC);
				poperror();
				mkqid(&nc->qid, sp->path, 0, QTDIR);
			}
			qunlock(&shrlk);
			if(sp == nil)
				goto Error;
		} else if(c->dev == 0) {
			qlock(&shrlk);
			sp = shrlookup(nil, nc->qid.path);
			if(sp != nil)
				incref(sp);
			qunlock(&shrlk);
			if(sp == nil)
				goto Error;
			wq2 = nil;
			rlock(&sp->umh.lock);
			for(f = sp->umh.mount; f != nil && wq2 == nil; f = f->next) {
				if(waserror())
					continue;
				wq2 = devtab[f->to->type]->walk(f->to, nil, name + j, nname - j);
				poperror();
			}
			runlock(&sp->umh.lock);
			shrdecref(sp);
			if(wq2 == nil)
				goto Error;
			memmove(wq->qid + wq->nqid, wq2->qid, wq2->nqid);
			wq->nqid += wq2->nqid;
			if(alloc)
				cclose(wq->clone);
			wq->clone = wq2->clone;
			free(wq2);
			poperror();
			return wq;
		}
		else{
			qlock(&shrlk);
			sp = shrlookup(nil, nc->qid.path);
			if(sp == nil){
				qunlock(&shrlk);
				goto Error;
			}
			rlock(&sp->umh.lock);
			f = mntlookup(sp, n, -1);
			if(f == nil){
				runlock(&sp->umh.lock);
				qunlock(&shrlk);
				goto Error;
			}
			nc->qid.path |= f->mountid;
			nc->qid.type = QTFILE;
			runlock(&sp->umh.lock);
			qunlock(&shrlk);
		}
		wq->qid[wq->nqid++] = nc->qid;
	}

	goto Done;
Error:
	if(j == 0)
		error(Enonexist);
	kstrcpy(up->errstr, Enonexist, ERRMAX);
Done:
	poperror();
	if(wq->nqid < nname) {
		if(alloc)
			cclose(wq->clone);
		wq->clone = nil;
	} else if(wq->clone)
		wq->clone->type = c->type;
	return wq;
}

static int
shrstat(Chan *c, uchar *db, int n)
{
	Shr *sp;
	Mount *f;
	Dir dir;
	int rc;

	if(c->qid.path == 0)
		devdir(c, c->qid, c->dev ? "#σc" : "#σ", 0, eve, 0555, &dir);
	else {
		qlock(&shrlk);
		if(waserror()){
			qunlock(&shrlk);
			nexterror();
		}
		sp = shrlookup(nil, c->qid.path);
		if(sp == nil)
			error(Enonexist);
		if((c->qid.path & 0xFFFF) == 0){
			kstrcpy(up->genbuf, sp->name, sizeof up->genbuf);
			devdir(c, c->qid, up->genbuf, 0, sp->owner, sp->perm, &dir);
		}else{
			rlock(&sp->umh.lock);
			f = mntlookup(sp, nil, c->qid.path);
			if(f == nil){
				runlock(&sp->umh.lock);
				error(Enonexist);
			}
			kstrcpy(up->genbuf, (char*)(f + 1), sizeof up->genbuf);
			devdir(c, c->qid, up->genbuf, 0, sp->owner, sp->perm, &dir);
			runlock(&sp->umh.lock);
		}
		qunlock(&shrlk);
		poperror();
	}
	rc = convD2M(&dir, db, n);
	if(rc == 0)
		error(Ebadarg);
	return rc;
}

static Chan*
shropen(Chan *c, int omode)
{
	Shr *sp;

	if(c->qid.type == QTDIR && omode != OREAD)
		error(Eisdir);
	if(c->qid.path != 0){
		qlock(&shrlk);
		if(waserror()){
			qunlock(&shrlk);
			nexterror();
		}
		sp = shrlookup(nil, c->qid.path);
		if(sp == nil)
			error(Enonexist);
		if(c->dev == 0)
			c->umh = &sp->umh;
		devpermcheck(sp->owner, sp->perm, openmode(omode));
		qunlock(&shrlk);
		poperror();
	}
	if(omode & ORCLOSE)
		error(Eperm);
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
shrcnfcreate(Chan *c, char *name, int omode, ulong)
{
	Shr *sp;
	Mount *m, *mm;
	
	qlock(&shrlk);
	if(waserror()){
		qunlock(&shrlk);
		nexterror();
	}
	sp = shrlookup(nil, c->qid.path);
	if(sp == nil)
		error(Enonexist);
	wlock(&sp->umh.lock);
	if(mntlookup(sp, name, -1)){
		wunlock(&sp->umh.lock);
		error(Eexist);
	}
	m = smalloc(sizeof(Mount) + strlen(name) + 1);
	m->mountid = ++mntid;
	mkqid(&c->qid, m->mountid | sp->path, 0, QTFILE);
	strcpy((char*)(m + 1), name);
	if(sp->umh.mount != nil) {
		for(mm = sp->umh.mount; mm->next != nil; mm = mm->next)
			;
		mm->next = m;
	} else {
		m->next = sp->umh.mount;
		sp->umh.mount = m;
	}
	wunlock(&sp->umh.lock);
	qunlock(&shrlk);
	poperror();

	c->flag |= COPEN;
	c->mode = openmode(omode);
}

static void
shrcreate(Chan *c, char *name, int omode, ulong perm)
{
	char *sname;
	Shr *sp;
	
	if(omode & OCEXEC)	/* can't happen */
		panic("someone broke namec");

	if(c->dev == 0 && c->qid.path != 0)
		error(Enocreate);
	
	if(c->qid.path != 0){
		shrcnfcreate(c, name, omode, perm);
		return;
	}

	if(c->dev != 1 || (perm & DMDIR) == 0 || openmode(omode) != OREAD)
		error(Eperm);

	sp = smalloc(sizeof *sp);
	sname = smalloc(strlen(name)+1);

	qlock(&shrlk);
	if(waserror()){
		free(sp);
		free(sname);
		qunlock(&shrlk);
		nexterror();
	}
	if(sp == nil || sname == nil)
		error(Enomem);
	if(shrlookup(name, -1))
		error(Eexist);

	sp->path = ((uvlong)qidpath++) << 32;
	sp->link = shr;
	strcpy(sname, name);
	sp->name = sname;
	incref(sp);
	c->qid.type = QTDIR;
	c->qid.path = sp->path;
	shr = sp;
	qunlock(&shrlk);
	poperror();

	kstrdup(&sp->owner, up->user);
	sp->perm = perm & 0777;

	c->flag |= COPEN;
	c->mode = OREAD;
}

static void
shrremove(Chan *c)
{
	Shr *sp, **l;

	if(c->qid.path == 0)
		error(Eperm);

	qlock(&shrlk);
	if(waserror()){
		qunlock(&shrlk);
		nexterror();
	}
	l = &shr;
	for(sp = *l; sp; sp = sp->link) {
		if(sp->path == (c->qid.path & 0xFFFFFFFF00000000ULL))
			break;

		l = &sp->link;
	}
	if(sp == 0)
		error(Enonexist);
	if(c->qid.path & 0xFFFFFFFF){
		if(shrremovemnt(sp, c->qid.path) < 0)
			error(Enonexist);
		qunlock(&shrlk);
		poperror();
		return;
	}

	if(strcmp(sp->owner, eve) == 0 && !iseve())
		error(Eperm);
	if((sp->perm&7) != 7 && strcmp(sp->owner, up->user) != 0 && !iseve())
		error(Eperm);

	*l = sp->link;
	qunlock(&shrlk);
	poperror();

	shrdecref(sp);
}

static int
shrwstat(Chan *c, uchar *dp, int n)
{
	char *strs;
	Dir d;
	Shr *sp;

	if(c->qid.path == 0)
		error(Eperm);

	strs = nil;
	qlock(&shrlk);
	if(waserror()){
		qunlock(&shrlk);
		free(strs);
		nexterror();
	}

	sp = shrlookup(nil, c->qid.path);
	if(sp == 0)
		error(Enonexist);

	if(strcmp(sp->owner, up->user) != 0 && !iseve())
		error(Eperm);

	strs = smalloc(n);
	n = convM2D(dp, n, &d, strs);
	if(n == 0)
		error(Eshortstat);
	if(d.mode != ~0UL)
		sp->perm = d.mode & 0777;
	if(d.uid && *d.uid)
		kstrdup(&sp->owner, d.uid);
	if(d.name && *d.name && strcmp(sp->name, d.name) != 0) {
		if(strchr(d.name, '/') != nil)
			error(Ebadchar);
		kstrdup(&sp->name, d.name);
	}
	qunlock(&shrlk);
	free(strs);
	poperror();
	return n;
}

static void
shrclose(Chan *c)
{
	c->umh = nil;
	if(c->flag & CRCLOSE){
		if(waserror())
			return;
		shrremove(c);
		poperror();
	}
}

static long
shrread(Chan *c, void *va, long n, vlong)
{
	if(c->qid.path == 0)
		return devdirread(c, va, n, 0, 0, shrgen);
	
	if(c->dev == 0)
		return unionread(c, va, n);
	
	if((long)c->qid.path == 0)
		return devdirread(c, va, n, 0, 0, shrcnfgen);
	
	error(Egreg);
	return 0;
}

static long
shrwrite(Chan *c, void *va, long n, vlong)
{
	Shr *sp;
	char *buf, *p, *aname;
	int fd;
	Chan *bc, *c0;
	Mount *m;
	struct{
		Chan	*chan;
		Chan	*authchan;
		char	*spec;
		int	flags;
	}bogus;

	buf = smalloc(n+1);
	if(waserror()){
		free(buf);
		nexterror();
	}
	memmove(buf, va, n);
	buf[n] = 0;
	
	fd = strtol(buf, &p, 10);
	if(p == buf || (*p != 0 && *p != '\n'))
		error(Ebadarg);
	if(*p == '\n' && *(p+1) != 0)
		aname = p + 1;
	else
		aname = nil;
	
	bc = fdtochan(fd, ORDWR, 0, 1);
	if(waserror()) {
		cclose(bc);
		nexterror();
	}
	bogus.flags = 0;
	bogus.chan = bc;
	bogus.authchan = nil;
	bogus.spec = aname;
	c0 = devtab[devno('M', 0)]->attach((char*)&bogus);
	cclose(bc);
	poperror();
	qlock(&shrlk);
	sp = shrlookup(nil, c->qid.path);
	if(sp == nil){
		qunlock(&shrlk);
		cclose(c0);
		error(Enonexist);
	}
	rlock(&sp->umh.lock);
	m = mntlookup(sp, nil, c->qid.path);
	if(m == nil){
		runlock(&sp->umh.lock);
		qunlock(&shrlk);
		cclose(c0);
		error(Enonexist);
	}
	m->to = c0;
	runlock(&sp->umh.lock);
	qunlock(&shrlk);
	free(buf);
	poperror();
	return n;
}

Dev shrdevtab = {
	L'σ',
	"shr",

	devreset,
	shrinit,	
	devshutdown,
	shrattach,
	shrwalk,
	shrstat,
	shropen,
	shrcreate,
	shrclose,
	shrread,
	devbread,
	shrwrite,
	devbwrite,
	shrremove,
	shrwstat,
};

void
shrrenameuser(char *old, char *new)
{
	Shr *sp;

	qlock(&shrlk);
	for(sp = shr; sp; sp = sp->link)
		if(sp->owner!=nil && strcmp(old, sp->owner)==0)
			kstrdup(&sp->owner, new);
	qunlock(&shrlk);
}
