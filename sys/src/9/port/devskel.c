#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	"netif.h"

typedef struct Skel Skel;
struct Skel {
	int ref;
	QLock lk;
	char name[KNAMELEN];
	char mode;
};

struct
{
	QLock lk;
	ulong path;
} skelalloc;

enum{
	Qroot,
	Qdir,
	Qskel,
};

static Chan*
skelattach(char *spec)
{
	Chan *c;
	Skel *f;
	uvlong path;

	c = devattach('z', spec);

	f = smalloc(sizeof *f);
	if(spec != nil && spec[0] != '\0' && strchr("de", spec[0]) != nil)
		f->mode = spec[0];
	else
		f->mode = 'f';

	f->ref = 1;

	qlock(&skelalloc.lk);
	path = skelalloc.path++;
	qunlock(&skelalloc.lk);

	mkqid(&c->qid, NETQID(path, Qroot), 0, QTDIR);
	c->aux = f;
	return c;
}

static int
step(Chan *c, Dir *dp, int direction)
{
	Skel *f;
	Qid qid;
	ulong perm;
	uvlong path;
	char *name;

	perm = 0555|DMDIR;
	path = NETTYPE(c->qid.path);
	f = c->aux;
	name = f->name;

	path += direction;
	if(!f->name[0] && path != Qroot)
		return -1;

	switch(path){
	case Qroot:
		mkqid(&qid, Qroot, 0, QTDIR);
		name = "#z";
		break;
	case Qdir:
		mkqid(&qid, Qdir, 0, QTDIR);
		break;
	case Qskel:
		switch(f->mode){
		case 'd':
			mkqid(&qid, Qskel, 0, QTDIR);
			break;
		case 'f':
		default:
			mkqid(&qid, Qskel, 0, QTFILE);
			perm = 0666;
			break;
		}
		break;
	default:
		return -1;
	}

	qid.path = NETQID(NETID(c->qid.path), qid.path);
	devdir(c, qid, name, 0, eve, perm, dp);
	return 1;
}


static int
skelgen(Chan *c, char *name, Dirtab *, int, int s, Dir *dp)
{
	Skel *f;

	f = c->aux;
	//First walk away from root
	if(name && !f->name[0] && f->mode != 'e' && NETTYPE(c->qid.path) == Qroot)
		utfecpy(f->name, &f->name[sizeof f->name-1], name);

	if(s != DEVDOTDOT)
		s++;

	return step(c, dp, s);
}

static Walkqid*
skelwalk(Chan *c, Chan *nc, char **name, int nname)
{
	Walkqid *wq;
	Skel *f;

	f = c->aux;
	qlock(&f->lk);
	if(waserror()){
		qunlock(&f->lk);
		nexterror();
	}

	wq = devwalk(c, nc, name, nname, nil, 0, skelgen);
	if(wq != nil && wq->clone != nil && wq->clone != c){
		if(f->ref <= 0)
			panic("devskel ref");
		f->ref++;
	}
	qunlock(&f->lk);
	poperror();
	return wq;
}

static Chan*
skelopen(Chan *c, int omode)
{
	if(!(c->qid.type & QTDIR))
		error(Eperm);
	if(omode != OREAD)
		error(Ebadarg);

	c->mode = omode;
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
skelclose(Chan *c)
{
	Skel *f;

	f = c->aux;
	qlock(&f->lk);
	f->ref--;
	if(f->ref == 0){
		qunlock(&f->lk);
		free(f);
	} else
		qunlock(&f->lk);
}

static long
skelread(Chan *c, void *va, long n, vlong)
{
	Skel *f;
	long nout;

	if(!(c->qid.type & QTDIR))
		error(Eperm);

	f = c->aux;
	qlock(&f->lk);
	if(waserror()){
		qunlock(&f->lk);
		nexterror();
	}
	nout = devdirread(c, va, n, nil, 0, skelgen);
	qunlock(&f->lk);
	poperror();
	return nout;
}

static long
skelwrite(Chan *, void *, long, vlong)
{
	error(Eperm);
	return 0;
}

static int
skelstat(Chan *c, uchar *db, int n)
{
	Skel *f;
	Dir dir;

	f = c->aux;
	qlock(&f->lk);
	step(c, &dir, 0);
	qunlock(&f->lk);

	n = convD2M(&dir, db, n);
	if(n < BIT16SZ)
		error(Eshortstat);
	return n;
}

Dev skeldevtab = {
	'z',
	"skel",

	devreset,
	devinit,
	devshutdown,
	skelattach,
	skelwalk,
	skelstat,
	skelopen,
	devcreate,
	skelclose,
	skelread,
	devbread,
	skelwrite,
	devbwrite,
	devremove,
	devwstat,
};
