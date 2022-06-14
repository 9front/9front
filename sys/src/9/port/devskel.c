#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	"netif.h"

typedef struct Skel Skel;
struct Skel {
	RWlock;
	int ref;
	char name[KNAMELEN];
	char mode;
};

struct
{
	QLock;
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

	f = mallocz(sizeof *f, 1);
	if(f == nil)
		exhausted("memory");
	if(waserror()){
		free(f);
		nexterror();
	}

	if(spec == nil)
		f->mode = 'f';
	else
		f->mode = spec[0];

	eqlock(&skelalloc);
	path = skelalloc.path++;
	qunlock(&skelalloc);

	poperror();
	mkqid(&c->qid, NETQID(path, Qroot), path, QTDIR);
	f->ref = 1;
	c->aux = f;
	return c;
}

static int
step(Chan *c, Dir *dp, int direction)
{
	Skel *f;
	Qid qid;
	ulong perm;
	int path;
	char *name;

	perm = 0555|DMDIR;
	path = NETTYPE(c->qid.path);
	f = c->aux;
	rlock(f);
	if(waserror()){
		runlock(f);
		return -1;
	}
	name = f->name;

	path += direction;
	if(!f->name[0] && path > Qroot)
		error(Enonexist);

	switch(path){
	case Qroot-1:
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
		error(Enonexist);
	}

	qid.vers = NETID(c->qid.path);
	qid.path = NETQID(qid.vers, qid.path);
	devdir(c, qid, name, 0, eve, perm, dp);
	runlock(f);
	poperror();
	return 1;
}


static int
skelgen(Chan *c, char *name, Dirtab*, int, int s, Dir *dp)
{
	Skel *f;

	switch(s){
	case DEVDOTDOT:
		break;
	case 0:
		s++;
		break;
	default:
		return -1;
	}
	f = c->aux;
	if(name && NETTYPE(c->qid.path) == Qroot){
		wlock(f);
		if(!f->name[0] && f->mode != 'e')
			utfecpy(f->name, &f->name[sizeof f->name-1], name);
		wunlock(f);
	}

	return step(c, dp, s);
}

static Walkqid*
skelwalk(Chan *c, Chan *nc, char **name, int nname)
{
	Walkqid *wq;
	Skel *f;

	wq = devwalk(c, nc, name, nname, nil, 0, skelgen);
	if(wq == nil || wq->clone == nil)
		return wq;

	f = c->aux;
	wlock(f);
	if(f->ref <= 0)
		panic("devskel ref");
	f->ref++;
	wunlock(f);
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
	wlock(f);
	f->ref--;
	if(f->ref == 0){
		wunlock(f);
		free(f);
	} else
		wunlock(f);
}

static long
skelread(Chan *c, void *va, long n, vlong)
{
	return devdirread(c, va, n, nil, 0, skelgen);
}

static long
skelwrite(Chan*, void*, long, vlong)
{
	error(Ebadusefd);
	return -1;
}

static int
skelstat(Chan *c, uchar *db, int n)
{
	Dir dir;

	if(step(c, &dir, 0) < 0)
		error(Enonexist);

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
