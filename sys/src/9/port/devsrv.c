#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	"netif.h"

typedef struct Srv Srv;
struct Srv
{
	char	*name;
	char	*owner;
	ulong	perm;
	Chan	*chan;
	Srv	*link;
	ulong	path;
};

typedef struct Fid Fid;
struct Fid
{
	int	ref;
	QLock 	lk;
	Srv 	*tail;
	ulong 	nextpath;
};

enum{
	Qroot,
	Qclone,

	Qend
};

static Fid global;

struct {
	QLock;
	ulong path;
} sessions;

static Srv*
srvlookup(Srv *sp, char *name, ulong qidpath)
{
	qidpath = NETTYPE(qidpath);
	for(; sp != nil; sp = sp->link) {
		if(sp->path == qidpath || (name != nil && strcmp(sp->name, name) == 0))
			return sp;
	}
	return nil;
}

static int
srvgen(Chan *c, char *name, Dirtab*, int, int s, Dir *dp)
{
	Srv *sp;
	Qid q;
	Fid *f;
	ulong id;

	if(s == DEVDOTDOT){
		switch(NETTYPE(c->qid.path)){
		case Qroot:
			mkqid(&q, Qroot, 0, QTDIR);
			devdir(c, q, "#s", 0, eve, 0555|DMDIR, dp);
			break;
		case Qclone:
		default:
			/*
			 * Someone has walked down /srv/clone/clone/.... and
			 * would like back up. We do not allow revisiting 
			 * previous sessions. Dead end
			 */
			error(Enonexist);
			break;
		}
		return 1;
	}

	id = NETID(c->qid.path);
	if(name != nil && strcmp(name, "clone") == 0){
		/* walk; new session */
		qlock(&sessions);
		id = ++sessions.path;
		qunlock(&sessions);

		f = smalloc(sizeof *f);
		f->ref = 1;
		f->nextpath = Qend;

		mkqid(&q, NETQID(id, Qclone), id, QTDIR);
		devdir(c, q, "clone", 0, eve, 0555|DMDIR, dp);
		c->aux = f;
		return 1;
	} else if(name == nil && s == 0) {
		/* stat, dirread; current session */
		mkqid(&q, NETQID(id, Qclone), id, QTDIR);
		devdir(c, q, "clone", 0, eve, 0555|DMDIR, dp);
		return 1;
	}

	f = c->aux;
	qlock(&f->lk);
	if(name != nil)
		sp = srvlookup(f->tail, name, -1);
	else {
		s -= 1;
		for(sp = f->tail; sp != nil && s > 0; sp = sp->link)
			s--;
	}
	if(sp == nil || (name != nil && (strlen(sp->name) >= sizeof(up->genbuf)))) {
		qunlock(&f->lk);
		return -1;
	}
	mkqid(&q, NETQID(id, sp->path), 0, QTFILE);
	/* make sure name string continues to exist after we release lock */
	kstrcpy(up->genbuf, sp->name, sizeof up->genbuf);
	devdir(c, q, up->genbuf, 0, sp->owner, sp->perm, dp);
	qunlock(&f->lk);
	return 1;
}

static void
srvinit(void)
{
	global.nextpath = Qend;
}

static Chan*
srvattach(char *spec)
{
	Chan *c;

	c = devattach('s', spec);
	c->aux = &global;
	return c;
}

static Walkqid*
srvwalk(Chan *c, Chan *nc, char **name, int nname)
{
	Walkqid *wq;
	Fid *f;
	int tripped;
	
	/*
	 * We need to allow for infinite recursions through clone but we
	 * don't need to permit passing multiple clones in a single walk.
	 * This allows us to ensure that only a single clone is alloted
	 * per walk.
	 */
	tripped = 0;
	if(nname > 1){
		nname = 1;
		tripped = 1;
	}
	wq = devwalk(c, nc, name, nname, 0, 0, srvgen);
	if(wq == nil || wq->clone == nil || wq->clone == c)
		return wq;

	if(tripped){
		/*
		 * Our partial walk returned a newly alloc'd clone.
		 * We wll never see a clunk for that partially walked
		 * fid, so just clean it up now.
		 */
		if(NETTYPE(wq->clone->qid.path) == Qclone){
			f = wq->clone->aux;
			assert(f->tail == nil);
			assert(f->ref == 1);
			free(f);
		}
		/* Correct state to indicate failure to walk all names */
		wq->clone->type = 0;
		cclose(wq->clone);
		wq->clone = nil;
		return wq;
	}
	if(wq->clone->aux == &global)
		return wq;

	if(NETID(c->qid.path) != NETID(wq->clone->qid.path))
		return wq;

	assert(c->aux == wq->clone->aux);
	f = c->aux;
	qlock(&f->lk);
	f->ref++;
	qunlock(&f->lk);
	return wq;
}

static int
srvstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, 0, 0, srvgen);
}

char*
srvname(Chan *c)
{
	Srv *sp;
	Fid *f;
	char *s;

	s = nil;
	f = &global;
	qlock(&f->lk);
	for(sp = f->tail; sp != nil; sp = sp->link) {
		if(sp->chan == c){
			s = malloc(3+strlen(sp->name)+1);
			if(s != nil)
				sprint(s, "#s/%s", sp->name);
			break;
		}
	}
	qunlock(&f->lk);
	return s;
}

static Chan*
srvopen(Chan *c, int omode)
{
	Srv *sp;
	Fid *f;
	Chan *nc;

	if(c->qid.type == QTDIR){
		if(omode & ORCLOSE)
			error(Eperm);
		if(omode != OREAD)
			error(Eisdir);
		c->mode = omode;
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}
	f = c->aux;
	qlock(&f->lk);
	if(waserror()){
		qunlock(&f->lk);
		nexterror();
	}

	sp = srvlookup(f->tail, nil, c->qid.path);
	if(sp == nil || sp->chan == nil)
		error(Eshutdown);

	if(omode&OTRUNC)
		error(Eexist);
	if(omode&ORCLOSE)
		error(Eperm);
	if(openmode(omode)!=sp->chan->mode && sp->chan->mode!=ORDWR)
		error(Eperm);
	devpermcheck(sp->owner, sp->perm, omode);

	nc = sp->chan;
	incref(nc);

	qunlock(&f->lk);
	poperror();

	cclose(c);
	return nc;
}

static Chan*
srvcreate(Chan *c, char *name, int omode, ulong perm)
{
	Srv *sp;
	Fid *f;

	if(openmode(omode) != OWRITE)
		error(Eperm);

	if(strlen(name) >= sizeof(up->genbuf))
		error(Etoolong);

	f = c->aux;
	sp = smalloc(sizeof *sp);
	kstrdup(&sp->name, name);
	kstrdup(&sp->owner, up->user);

	qlock(&f->lk);
	if(waserror()){
		qunlock(&f->lk);
		free(sp->owner);
		free(sp->name);
		free(sp);
		nexterror();
	}
	if(srvlookup(f->tail, name, -1) != nil)
		error(Eexist);

	sp->perm = perm&0777;
	sp->path = f->nextpath++;

	c->qid.path = NETQID(NETID(c->qid.path), sp->path);
	c->qid.type = QTFILE;

	sp->link = f->tail;
	f->tail = sp;

	qunlock(&f->lk);
	poperror();

	c->flag |= COPEN;
	c->mode = OWRITE;

	return c;
}

static void
srvremove(Chan *c)
{
	Srv *sp, **l;
	Fid *f;
	ulong id;

	if(c->qid.type == QTDIR)
		error(Eperm);

	f = c->aux;

	qlock(&f->lk);
	if(waserror()){
		qunlock(&f->lk);
		nexterror();
	}
	l = &f->tail;
	for(sp = *l; sp != nil; sp = *l) {
		if(sp->path == NETTYPE(c->qid.path))
			break;
		l = &sp->link;
	}
	if(sp == nil)
		error(Enonexist);

	/*
	 * Only eve can remove system services.
	 */
	if(strcmp(sp->owner, eve) == 0 && !iseve())
		error(Eperm);

	/*
	 * No removing personal services.
	 */
	if((sp->perm&7) != 7 && strcmp(sp->owner, up->user) && !iseve())
		error(Eperm);

	*l = sp->link;
	sp->link = nil;
	id = NETID(c->qid.path);

	poperror();
	if(sp->chan != nil)
		cclose(sp->chan);
	free(sp->owner);
	free(sp->name);
	free(sp);

	if(f == &global){
		qunlock(&f->lk);
		return;
	}

	f->ref--;
	if(f->ref == 0){
		assert(f->tail == nil);
		qunlock(&f->lk);
		free(f);
	} else if(f->ref < 0)
		panic("srv ref rm %d id %uld", f->ref, id);
	else
		qunlock(&f->lk);
}

static int
srvwstat(Chan *c, uchar *dp, int n)
{
	char *strs;
	Srv *sp;
	Fid *f;
	Dir d;

	if(c->qid.type & QTDIR)
		error(Eperm);

	strs = smalloc(n);
	if(waserror()){
		free(strs);
		nexterror();
	}
	n = convM2D(dp, n, &d, strs);
	if(n == 0)
		error(Eshortstat);

	f = c->aux;

	qlock(&f->lk);
	if(waserror()){
		qunlock(&f->lk);
		nexterror();
	}

	sp = srvlookup(f->tail, nil, c->qid.path);
	if(sp == nil)
		error(Enonexist);

	if(strcmp(sp->owner, up->user) != 0 && !iseve())
		error(Eperm);

	if(d.name != nil && *d.name && strcmp(sp->name, d.name) != 0) {
		if(strchr(d.name, '/') != nil)
			error(Ebadchar);
		if(strlen(d.name) >= sizeof(up->genbuf))
			error(Etoolong);
		kstrdup(&sp->name, d.name);
	}
	if(d.uid != nil && *d.uid)
		kstrdup(&sp->owner, d.uid);
	if(d.mode != ~0UL)
		sp->perm = d.mode & 0777;

	qunlock(&f->lk);
	poperror();

	free(strs);
	poperror();

	return n;
}

static void
srvclose(Chan *c)
{
	Fid *f;

	/*
	 * in theory we need to override any changes in removability
	 * since open, but since all that's checked is the owner,
	 * which is immutable, all is well.
	 */
	if((c->flag & COPEN) && (c->flag & CRCLOSE)){
		if(waserror())
			goto ref;

		srvremove(c);
		poperror();
	} else {

ref:
		f = c->aux;
		if(f == &global)
			return;

		qlock(&f->lk);
		f->ref--;
		if(f->ref == 0){
			assert(f->tail == nil);
			qunlock(&f->lk);
			free(f);
		} else if(f->ref < 0)
			panic("srvref close %d %uld", f->ref, NETID(c->qid.path));
		else
			qunlock(&f->lk);
	}
}

static long
srvread(Chan *c, void *va, long n, vlong)
{
	isdir(c);
	return devdirread(c, va, n, 0, 0, srvgen);
}

static long
srvwrite(Chan *c, void *va, long n, vlong)
{
	Srv *sp;
	Fid *f;
	Chan *c1;
	int fd;
	char buf[32];

	if(n >= sizeof buf)
		error(Etoobig);
	memmove(buf, va, n);	/* so we can NUL-terminate */
	buf[n] = 0;
	fd = strtoul(buf, 0, 0);

	c1 = fdtochan(fd, -1, 0, 1);	/* error check and inc ref */

	f = c->aux;

	qlock(&f->lk);
	if(waserror()) {
		qunlock(&f->lk);
		cclose(c1);
		nexterror();
	}
	if(c1->qid.type & QTAUTH)
		error("cannot post auth file in srv");
	sp = srvlookup(f->tail, nil, c->qid.path);
	if(sp == nil)
		error(Enonexist);

	if(sp->chan != nil)
		error(Ebadusefd);

	sp->chan = c1;

	qunlock(&f->lk);
	poperror();
	return n;
}

Dev srvdevtab = {
	's',
	"srv",

	devreset,
	srvinit,	
	devshutdown,
	srvattach,
	srvwalk,
	srvstat,
	srvopen,
	srvcreate,
	srvclose,
	srvread,
	devbread,
	srvwrite,
	devbwrite,
	srvremove,
	srvwstat,
};

void
srvrenameuser(char *old, char *new)
{
	Srv *sp;
	Fid *f;

	f = &global;
	qlock(&f->lk);
	for(sp = f->tail; sp != nil; sp = sp->link) {
		if(sp->owner != nil && strcmp(old, sp->owner) == 0)
			kstrdup(&sp->owner, new);
	}
	qunlock(&f->lk);
}
