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
	ulong	path;
	Mhead	umh; /* only lock and mount are used */
	char	*desc; /* contents of file; nil if invalid, rebuild if necessary */
	QLock	desclock;
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
	if(sp->desc != nil)
		free(sp->desc);
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

	mkqid(&q, sp->path, 0, c->dev ? QTFILE : QTDIR);
	/* make sure name string continues to exist after we release lock */
	kstrcpy(up->genbuf, sp->name, sizeof up->genbuf);
	devdir(c, q, up->genbuf, 0, sp->owner, sp->perm & (c->dev ? ~0111 : ~0), dp);
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
shrlookup(char *name, ulong qidpath)
{
	Shr *sp;
	for(sp = shr; sp; sp = sp->link)
		if(sp->path == qidpath || (name && strcmp(sp->name, name) == 0))
			return sp;
	return nil;
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
	qlock(&sp->desclock);
	free(sp->desc);
	sp->desc = nil;
	qunlock(&sp->desclock);
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
				mkqid(&nc->qid, sp->path, 0, c->dev ? QTFILE : QTDIR);
			}
			qunlock(&shrlk);
			if(sp == nil)
				goto Error;
		} else {
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
	return devstat(c, db, n, 0, 0, shrgen);
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
		if(c->qid.type == QTDIR)
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
shrunioncreate(Chan *c, char *name, int omode, ulong perm)
{
	Shr *sp;
	Mount *m;
	Walkqid *wq;
	
	error(Enocreate); /* code below is broken */
	
	qlock(&shrlk);
	sp = shrlookup(nil, c->qid.path);
	if(sp != nil)
		incref(sp);
	qunlock(&shrlk);
	if(sp == nil)
		error(Enonexist);
	if(waserror()){
		shrdecref(sp);
		nexterror();
	}
	for(m = sp->umh.mount; m != nil; m = m->next)
		if(m->mflag & MCREATE)
			break;
	if(m == nil)
		error(Enocreate);

	wq = devtab[m->to->type]->walk(m->to, c, nil, 0);
	if(wq == nil)
		error(Egreg);
	if(wq->clone != c){
		cclose(wq->clone);
		free(wq);
		error(Egreg);
	}
	free(wq);
	devtab[c->type]->create(c, name, omode, perm);
	shrdecref(sp);
	poperror();
}

static void
shrcreate(Chan *c, char *name, int omode, ulong perm)
{
	char *sname;
	Shr *sp;
	
	if(c->qid.path != 0) {
		shrunioncreate(c, name, omode, perm);
		return;
	}
	
	if(c->dev != 1)
		error(Eperm);

	if(omode & OCEXEC)	/* can't happen */
		panic("someone broke namec");

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

	sp->path = qidpath++;
	sp->link = shr;
	strcpy(sname, name);
	sp->name = sname;
	incref(sp);
	c->qid.type = QTFILE;
	c->qid.path = sp->path;
	shr = sp;
	qunlock(&shrlk);
	poperror();

	kstrdup(&sp->owner, up->user);
	sp->perm = (perm&0777) | ((perm&0444)>>2);

	c->flag |= COPEN;
	c->mode = OWRITE;
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
		if(sp->path == c->qid.path)
			break;

		l = &sp->link;
	}
	if(sp == 0)
		error(Enonexist);

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
shrread(Chan *c, void *va, long n, vlong off)
{
	Shr *sp;
	long ret;
	int nn;
	Mount *f;
	char *s, *e;

	if(c->qid.path == 0)
		return devdirread(c, va, n, 0, 0, shrgen);
	
	if(c->qid.type & QTDIR)
		return unionread(c, va, n);
	
	qlock(&shrlk);
	sp = shrlookup(nil, c->qid.path);
	if(sp != nil)
		incref(sp);
	qunlock(&shrlk);
	if(sp == nil)
		error(Enonexist);
	qlock(&sp->desclock);
	if(sp->desc == nil){
		nn = 0;
		rlock(&sp->umh.lock);
		for(f = sp->umh.mount; f != nil; f = f->next)
			nn += 32 + strlen((char*)(f + 1));
		s = sp->desc = smalloc(nn + 1);
		e = s + nn;
		for(f = sp->umh.mount; f != nil; f = f->next)
			s = seprint(s, e, "%lud %s %C %lud %lld\n", f->mountid, (char*)(f + 1), devtab[f->to->mchan->type]->dc, f->to->mchan->dev, f->to->qid.path);
		runlock(&sp->umh.lock);
	}
	ret = readstr(off, va, n, sp->desc);
	qunlock(&sp->desclock);
	shrdecref(sp);
	return ret;
}

static long
shrwrite(Chan *c, void *va, long n, vlong)
{
	Shr *sp;
	char *buf, *p, *desc, *aname;
	int mode, fd, id;
	Chan *bc, *c0;
	Mount *m, *mm;
	struct{
		Chan	*chan;
		Chan	*authchan;
		char	*spec;
		int	flags;
	}bogus;
	
	qlock(&shrlk);
	sp = shrlookup(nil, c->qid.path);
	if(sp != nil)
		incref(sp);
	qunlock(&shrlk);
	if(sp == nil)
		error(Enonexist);
	buf = smalloc(n+1);
	if(waserror()){
		shrdecref(sp);
		free(buf);
		nexterror();
	}
	memmove(buf, va, n);
	buf[n] = 0;
	
	if(*buf == 'u'){
		p = buf + 1;
		while(*p <= ' ' && *p != '\n')
			p++;
		if(*p == 0 || *p == '\n')
			error(Ebadarg);
		id = strtol(p, 0, 10);
		if(shrremovemnt(sp, id) < 0)
			error(Ebadarg);
		shrdecref(sp);
		free(buf);
		poperror();
		return n;
	}
	
	p = buf;
	mode = 0;
	for(; *p > ' '; p++)
		switch(*p) {
		case 'a': mode |= MAFTER; break;
		case 'b': mode |= MBEFORE; break;
		case 'c': mode |= MCREATE; break;
		case 'C': mode |= MCACHE; break;
		default: error(Ebadarg);
		}

	if((mode & (MAFTER|MBEFORE)) == 0 || (mode & (MAFTER|MBEFORE)) == (MAFTER|MBEFORE))
		error(Ebadarg);
	while(*p <= ' ' && *p != '\n')
		p++;
	if(*p == 0 || *p == '\n')
		error(Ebadarg);
	fd = strtol(p, &p, 10);
	while(*p <= ' ' && *p != '\n')
		p++;
	if(*p != 0 && *p != '\n') {
		desc = p;
		p = strchr(desc, '\n');
		if(p != nil)
			*p = 0;
	} else
		desc = "";
	aname = strchr(buf, '\n');
	if(aname != nil && *++aname == 0)
		aname = nil;
	if(strlen(desc) > 128)
		error(Ebadarg);

	bc = fdtochan(fd, ORDWR, 0, 1);
	if(waserror()) {
		cclose(bc);
		nexterror();
	}
	bogus.flags = mode & MCACHE;
	bogus.chan = bc;
	bogus.authchan = nil;
	bogus.spec = aname;
	c0 = devtab[devno('M', 0)]->attach((char*)&bogus);
	cclose(bc);
	poperror();

	m = smalloc(sizeof(Mount) + strlen(desc) + 1);
	strcpy((char*)(m + 1), desc);
	m->to = c0;
	m->mflag = mode;
	qlock(&shrlk);
	m->mountid = ++mntid;
	qunlock(&shrlk);
	wlock(&sp->umh.lock);
	if((mode & MAFTER) != 0 && sp->umh.mount != nil) {
		for(mm = sp->umh.mount; mm->next != nil; mm = mm->next)
			;
		mm->next = m;
	} else {
		m->next = sp->umh.mount;
		sp->umh.mount = m;
	}
	wunlock(&sp->umh.lock);
	qlock(&sp->desclock);
	free(sp->desc);
	sp->desc = nil;
	qunlock(&sp->desclock);
	shrdecref(sp);
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
