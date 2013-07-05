#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"


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

static QLock	srvlk;
static Srv	*srv;
static int	qidpath;

static Srv*
srvlookup(char *name, ulong qidpath)
{
	Srv *sp;

	for(sp = srv; sp != nil; sp = sp->link) {
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

	if(s == DEVDOTDOT){
		devdir(c, c->qid, "#s", 0, eve, 0555, dp);
		return 1;
	}

	qlock(&srvlk);
	if(name != nil)
		sp = srvlookup(name, -1);
	else {
		for(sp = srv; sp != nil && s > 0; sp = sp->link)
			s--;
	}
	if(sp == nil || (name != nil && (strlen(sp->name) >= sizeof(up->genbuf)))) {
		qunlock(&srvlk);
		return -1;
	}
	mkqid(&q, sp->path, 0, QTFILE);
	/* make sure name string continues to exist after we release lock */
	kstrcpy(up->genbuf, sp->name, sizeof up->genbuf);
	devdir(c, q, up->genbuf, 0, sp->owner, sp->perm, dp);
	qunlock(&srvlk);
	return 1;
}

static void
srvinit(void)
{
	qidpath = 1;
}

static Chan*
srvattach(char *spec)
{
	return devattach('s', spec);
}

static Walkqid*
srvwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, 0, 0, srvgen);
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
	char *s;

	s = nil;
	qlock(&srvlk);
	for(sp = srv; sp != nil; sp = sp->link) {
		if(sp->chan == c){
			s = malloc(3+strlen(sp->name)+1);
			if(s != nil)
				sprint(s, "#s/%s", sp->name);
			break;
		}
	}
	qunlock(&srvlk);
	return s;
}

static Chan*
srvopen(Chan *c, int omode)
{
	Srv *sp;
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
	qlock(&srvlk);
	if(waserror()){
		qunlock(&srvlk);
		nexterror();
	}

	sp = srvlookup(nil, c->qid.path);
	if(sp == nil || sp->chan == nil)
		error(Eshutdown);

	if(omode&OTRUNC)
		error(Eexist);
	if(openmode(omode)!=sp->chan->mode && sp->chan->mode!=ORDWR)
		error(Eperm);
	devpermcheck(sp->owner, sp->perm, omode);

	nc = sp->chan;
	incref(nc);

	qunlock(&srvlk);
	poperror();

	cclose(c);
	return nc;
}

static Chan*
srvcreate(Chan *c, char *name, int omode, ulong perm)
{
	Srv *sp;

	if(openmode(omode) != OWRITE)
		error(Eperm);

	if(strlen(name) >= sizeof(up->genbuf))
		error(Etoolong);

	sp = smalloc(sizeof *sp);
	kstrdup(&sp->name, name);
	kstrdup(&sp->owner, up->user);

	qlock(&srvlk);
	if(waserror()){
		qunlock(&srvlk);
		free(sp->owner);
		free(sp->name);
		free(sp);
		nexterror();
	}
	if(srvlookup(name, -1) != nil)
		error(Eexist);

	sp->perm = perm&0777;
	sp->path = qidpath++;

	c->qid.path = sp->path;
	c->qid.type = QTFILE;

	sp->link = srv;
	srv = sp;

	qunlock(&srvlk);
	poperror();

	c->flag |= COPEN;
	c->mode = OWRITE;

	return c;
}

static void
srvremove(Chan *c)
{
	Srv *sp, **l;

	if(c->qid.type == QTDIR)
		error(Eperm);

	qlock(&srvlk);
	if(waserror()){
		qunlock(&srvlk);
		nexterror();
	}
	l = &srv;
	for(sp = *l; sp != nil; sp = *l) {
		if(sp->path == c->qid.path)
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

	qunlock(&srvlk);
	poperror();

	if(sp->chan != nil)
		cclose(sp->chan);
	free(sp->owner);
	free(sp->name);
	free(sp);
}

static int
srvwstat(Chan *c, uchar *dp, int n)
{
	char *strs;
	Srv *sp;
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

	qlock(&srvlk);
	if(waserror()){
		qunlock(&srvlk);
		nexterror();
	}

	sp = srvlookup(nil, c->qid.path);
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

	qunlock(&srvlk);
	poperror();

	free(strs);
	poperror();

	return n;
}

static void
srvclose(Chan *c)
{
	/*
	 * in theory we need to override any changes in removability
	 * since open, but since all that's checked is the owner,
	 * which is immutable, all is well.
	 */
	if(c->flag & CRCLOSE){
		if(waserror())
			return;
		srvremove(c);
		poperror();
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
	Chan *c1;
	int fd;
	char buf[32];

	if(n >= sizeof buf)
		error(Etoobig);
	memmove(buf, va, n);	/* so we can NUL-terminate */
	buf[n] = 0;
	fd = strtoul(buf, 0, 0);

	c1 = fdtochan(fd, -1, 0, 1);	/* error check and inc ref */

	qlock(&srvlk);
	if(waserror()) {
		qunlock(&srvlk);
		cclose(c1);
		nexterror();
	}
	if(c1->flag & (CCEXEC|CRCLOSE))
		error("posted fd has remove-on-close or close-on-exec");
	if(c1->qid.type & QTAUTH)
		error("cannot post auth file in srv");
	sp = srvlookup(nil, c->qid.path);
	if(sp == nil)
		error(Enonexist);

	if(sp->chan != nil)
		error(Ebadusefd);

	sp->chan = c1;

	qunlock(&srvlk);
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

	qlock(&srvlk);
	for(sp = srv; sp != nil; sp = sp->link) {
		if(sp->owner != nil && strcmp(old, sp->owner) == 0)
			kstrdup(&sp->owner, new);
	}
	qunlock(&srvlk);
}
