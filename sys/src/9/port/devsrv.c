#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	"netif.h"

typedef struct Link Link;
struct Link
{
	void 	*link;
	char 	*name;
	ulong 	path;
};

typedef struct Srv Srv;
struct Srv
{
	Link;

	char	*owner;
	ulong	perm;
	Chan	*chan;
};

typedef struct Board Board;
struct Board
{
	Link;
	RWlock;
	Ref;

	Board 	*parent;
	Board 	*child;
	Srv 	*srv;
	long	id;
	int	qidpath;
	int 	closed;	
};

struct{
	QLock;
	long path;
} boards;

enum{
	Qroot,
	Qclone,
	Qlease,

	Qend,
};

Board root;

static char Eexpired[] = "expired lease";

static void*
lookup(Link *l, char *name, ulong qidpath)
{
	Link *lp;

	if(qidpath != ~0UL)
		qidpath = NETTYPE(qidpath);
	for(lp = l; lp != nil; lp = lp->link){
		if(qidpath != ~0UL && lp->path == qidpath)
			return lp;
		if(name != nil && strcmp(lp->name, name) == 0)
			return lp;
	}
	return nil;
}

static void*
remove(Link **l, char *name, ulong qidpath)
{
	Link *lp;
	Link **last;

	if(qidpath != ~0UL)
		qidpath = NETTYPE(qidpath);
	last = l;
	for(lp = *l; lp != nil; lp = lp->link){
		if(qidpath != ~0UL && lp->path == qidpath)
			break;
		if(name != nil && strcmp(lp->name, name) == 0)
			break;
		last = &lp->link;
	}
	if(lp == nil)
		return nil;

	*last = lp->link;
	lp->link = nil;
	return lp;
}

static void
boardclunk(Board *b, int close)
{
	Srv *sp, *prv;
	Board *ch;
	long ref;

	/* caller holds a wlock */
	if(b == &root){
		wunlock(b);
		return;
	}

	if(close){
		assert(b->closed == 0);
		b->closed++;
		for(sp = b->srv; sp != nil; sp = prv){
			prv = sp->link;
			free(sp->owner);
			free(sp->name);
			if(sp->chan != nil)
				cclose(sp->chan);
			free(sp);
		}
		b->srv = nil;
	}
	ref = decref(b);

	/*
	 * All boards must be walkable from root. So a board
	 * is allowed to sit at zero references as long as it
	 * still has active children. For leaf nodes we then
	 * have to walk up the tree to clear now empty parents.
	 */
	while(b->closed && b->child == nil && ref == 0){
		//Root should never be closed
		assert(b->parent != nil);
		wlock(b->parent);
		ch = remove((Link**)&b->parent->child, b->name, b->path);
		assert(ch == b);

		b = ch->parent;
		free(ch->name);
		wunlock(ch);
		free(ch);
	}
	wunlock(b);
}

static int
srvgen(Chan *c, char *name, Dirtab*, int, int s, Dir *dp)
{
	Srv *sp;
	Board *b, *ch;
	Qid q;

	if(name != nil && strlen(name) >= sizeof(up->genbuf))
		return -1;

	b = c->aux;
	ch = nil;
	mkqid(&q, ~0L, 0, QTFILE);
	rlock(b);
	if(waserror()){
		runlock(b);
		return -1;
	}
	if(s == DEVDOTDOT){
		ch = b->parent;
		if(ch == nil)
			ch = &root;
		goto Child;
		
	}
	if(name != nil){
		if(strcmp("clone", name) == 0)
			goto Clone;

		sp = lookup(b->srv, name, ~0UL);
		if(sp == nil)
			ch = lookup(b->child, name, ~0UL);
	} else {
		if(s == 0)
			goto Clone;
		s--;
		for(sp = b->srv; sp != nil && s > 0; sp = sp->link)
			s--;
		for(ch = b->child; ch != nil && s > 0; ch = ch->link)
			s--;
	}
	if(sp != nil){
		kstrcpy(up->genbuf, sp->name, sizeof up->genbuf);
		q.vers = NETID(c->qid.path);
		q.path = NETQID(q.vers, sp->path);
		devdir(c, q, up->genbuf, 0, sp->owner, sp->perm, dp);
	} else if(ch != nil){
Child:
		kstrcpy(up->genbuf, ch->name, sizeof up->genbuf);
		q.vers = ch->id;
		q.path = NETQID(q.vers, ch->path);
		q.type = QTDIR;
		devdir(c, q, up->genbuf, 0, eve, 0555|DMDIR, dp);
		/* dirread's and stats shouldn't alter c->aux */
		if(name != nil)
			c->aux = ch;
	} else if(0){
Clone:
		q.vers = NETID(c->qid.path);
		q.path = NETQID(q.vers, Qclone);
		devdir(c, q, "clone", 0, eve, 0444, dp);
	} else
		error(Enonexist);

	runlock(b);
	poperror();
	return 1;
}

static void
srvinit(void)
{
	root.qidpath = Qend;
	root.name = "#s";
}

static Chan*
srvattach(char *spec)
{
	Chan *c;

	c = devattach('s', spec);
	c->aux = &root;
	return c;
}

static Walkqid*
srvwalk(Chan *c, Chan *nc, char **name, int nname)
{
	Board *b;
	Walkqid *wq;

	wq = devwalk(c, nc, name, nname, 0, 0, srvgen);
	if(wq == nil || wq->clone == nil)
		return wq;

	b = wq->clone->aux;
	if(b == &root)
		return wq;

	incref(b);
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
	Board *b;
	Srv *sp;
	char *s;

	s = nil;
	b = &root;
	rlock(b);
	for(sp = b->srv; sp != nil; sp = sp->link) {
		if(sp->chan == c){
			s = malloc(3+strlen(sp->name)+1);
			if(s != nil)
				sprint(s, "#s/%s", sp->name);
			break;
		}
	}
	runlock(b);
	return s;
}

static Chan*
srvopen(Chan *c, int omode)
{
	Board *b, *ch;
	Srv *sp;
	Chan *nc;
	char buf[64];

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
	if(omode&OTRUNC)
		error(Eexist);
	if(omode&ORCLOSE)
		error(Eperm);

	b = c->aux;
	if(NETTYPE(c->qid.path) == Qclone){;
		wlock(b);
		if(b->closed){
			wunlock(b);
			error(Eexpired);
		}
		ch = smalloc(sizeof *ch);
		ch->qidpath = Qend;
		ch->ref = 1;
		do {
			qlock(&boards);
			ch->id = ++boards.path;
			qunlock(&boards);
			snprint(buf, sizeof buf, "%ld", ch->id);
		} while(lookup(b->srv, buf, ~0UL) != nil);

		ch->parent = b;
		ch->path = b->qidpath++;
		kstrdup(&ch->name, buf);

		ch->link = b->child;
		b->child = ch;
		c->aux = ch;
		c->qid.vers = ch->id;
		c->qid.path = NETQID(ch->id, Qlease);
		boardclunk(b, 0); //unlock
		return c;
	}

	rlock(b);
	if(waserror()){
		runlock(b);
		nexterror();
	}
	if(b->closed)
		error(Eexpired);

	sp = lookup(b->srv, nil, c->qid.path);
	if(sp == nil || sp->chan == nil)
		error(Eshutdown);

	if(openmode(omode)!=sp->chan->mode && sp->chan->mode!=ORDWR)
		error(Eperm);
	devpermcheck(sp->owner, sp->perm, omode);

	nc = sp->chan;
	incref(nc);

	runlock(b);
	poperror();

	cclose(c);
	return nc;
}

static Chan*
srvcreate(Chan *c, char *name, int omode, ulong perm)
{
	Board *b;
	Srv *sp;

	if(openmode(omode) != OWRITE)
		error(Eperm);

	if(strlen(name) >= sizeof(up->genbuf))
		error(Etoolong);

	sp = smalloc(sizeof *sp);
	kstrdup(&sp->name, name);
	kstrdup(&sp->owner, up->user);

	b = c->aux;
	wlock(b);
	if(waserror()){
		wunlock(b);
		free(sp->owner);
		free(sp->name);
		free(sp);
		nexterror();
	}
	if(b->closed)
		error(Eexpired);
	if(lookup(b->srv, name, ~0UL) != nil)
		error(Eexist);
	if(lookup(b->child, name, ~0UL) != nil)
		error(Eexist);

	sp->perm = perm&0777;
	sp->path = b->qidpath++;

	c->qid.path = NETQID(b->id, sp->path);
	c->qid.vers = b->id;
	c->qid.type = QTFILE;

	sp->link = b->srv;
	b->srv = sp;

	wunlock(b);
	poperror();

	c->flag |= COPEN;
	c->mode = OWRITE;

	return c;
}

static void
srvremove(Chan *c)
{
	Board *b;
	Srv *sp;

	if(c->qid.type == QTDIR)
		error(Eperm);
	switch(NETTYPE(c->qid.path)){
	case Qlease:
	case Qclone:
		error(Eperm);
	}

	b = c->aux;
	wlock(b);
	if(waserror()){
		wunlock(b);
		nexterror();
	}
	sp = lookup(b->srv, nil, c->qid.path);
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

	remove((Link**)&b->srv, nil, c->qid.path);

	boardclunk(b, 0); //unlock
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
	Board *b;
	char *strs;
	Srv *sp;
	Dir d;

	if(c->qid.type & QTDIR)
		error(Eperm);
	switch(NETTYPE(c->qid.path)){
	case Qlease:
	case Qclone:
		error(Eperm);
	}

	strs = smalloc(n);
	if(waserror()){
		free(strs);
		nexterror();
	}
	n = convM2D(dp, n, &d, strs);
	if(n == 0)
		error(Eshortstat);

	b = c->aux;
	wlock(b);
	if(waserror()){
		wunlock(b);
		nexterror();
	}
	if(b->closed)
		error(Eexpired);

	sp = lookup(b->srv, nil, c->qid.path);
	if(sp == nil)
		error(Enonexist);

	if(strcmp(sp->owner, up->user) != 0 && !iseve())
		error(Eperm);

	if(d.name != nil && *d.name && strcmp(sp->name, d.name) != 0) {
		if(strchr(d.name, '/') != nil)
			error(Ebadchar);
		if(strlen(d.name) >= sizeof(up->genbuf))
			error(Etoolong);
		if(lookup(b->srv, d.name, ~0UL) != nil)
			error(Eexist);
		if(lookup(b->child, d.name, ~0UL) != nil)
			error(Eexist);
		kstrdup(&sp->name, d.name);
	}
	if(d.uid != nil && *d.uid)
		kstrdup(&sp->owner, d.uid);
	if(d.mode != ~0UL)
		sp->perm = d.mode & 0777;

	wunlock(b);
	poperror();

	free(strs);
	poperror();

	return n;
}

static void
srvclose(Chan *c)
{
	Board *b;
	int expired;

	expired = 0;
	if(NETTYPE(c->qid.path) == Qlease)
		expired++;
	else if(c->flag & CRCLOSE){
		/*
		 * in theory we need to override any changes in removability
		 * since open, but since all that's checked is the owner,
	 	 * which is immutable, all is well.
	 	 */
		if(waserror())
			goto Clunk;
		srvremove(c);
		poperror();
		return;
	}
Clunk:
	b = c->aux;
	wlock(b);
	boardclunk(b, expired); //unlock
}

static long
srvread(Chan *c, void *va, long n, vlong off)
{
	Board *b;

	if(NETTYPE(c->qid.path) == Qlease){
		b = c->aux;
		rlock(b);
		n = readstr((ulong)off, va, n, b->name);
		runlock(b);
		return n;
	}
	isdir(c);
	return devdirread(c, va, n, 0, 0, srvgen);
}

static long
srvwrite(Chan *c, void *va, long n, vlong)
{
	Board *b;
	Srv *sp;
	Chan *c1;
	int fd;
	char buf[32];

	if(NETTYPE(c->qid.path) == Qlease)
		error(Eperm);

	if(n >= sizeof buf)
		error(Etoobig);
	memmove(buf, va, n);	/* so we can NUL-terminate */
	buf[n] = 0;
	fd = strtoul(buf, 0, 0);

	c1 = fdtochan(fd, -1, 0, 1);	/* error check and inc ref */

	b = c->aux;
	wlock(b);
	if(waserror()) {
		wunlock(b);
		cclose(c1);
		nexterror();
	}
	if(b->closed)
		error(Eexpired);
	if(c1->qid.type & QTAUTH)
		error("cannot post auth file in srv");
	sp = lookup(b->srv, nil, c->qid.path);
	if(sp == nil)
		error(Enonexist);

	if(sp->chan != nil)
		error(Ebadusefd);

	sp->chan = c1;

	wunlock(b);
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
	Board *b;
	Srv *sp;

	b = &root;
	wlock(b);
	for(sp = b->srv; sp != nil; sp = sp->link) {
		if(sp->owner != nil && strcmp(old, sp->owner) == 0)
			kstrdup(&sp->owner, new);
	}
	wunlock(b);
}
