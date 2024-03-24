#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	"netif.h"

typedef struct Pipe	Pipe;
struct Pipe
{
	QLock;
	int	ref;
	Queue	*q[2];
	int	qref[2];
};

struct
{
	Lock;
	ulong	path;
} pipealloc;

enum
{
	Qdir,
	Qdata0,
	Qdata1,
};

Dirtab pipedir[] =
{
	".",		{Qdir,0,QTDIR},	0,		DMDIR|0500,
	"data",		{Qdata0},	0,		0666,
	"data1",	{Qdata1},	0,		0666,
};

static void
pipeinit(void)
{
	if(conf.pipeqsize == 0)
		conf.pipeqsize = 256*1024;
	/* make pipeqsize a multiple of our i/o unit */
	conf.pipeqsize = qiomaxatomic * ((conf.pipeqsize + qiomaxatomic-1)/qiomaxatomic);
}

/*
 *  create a pipe, no streams are created until an open
 */
static Chan*
pipeattach(char *spec)
{
	Pipe *p;
	Chan *c;
	ulong path;

	c = devattach('|', spec);
	if(waserror()){
		chanfree(c);
		nexterror();
	}
	p = malloc(sizeof(Pipe));
	if(p == 0)
		exhausted("memory");
	p->ref = 1;

	p->q[0] = qopen(conf.pipeqsize, 0, 0, 0);
	if(p->q[0] == 0){
		free(p);
		exhausted("memory");
	}
	p->q[1] = qopen(conf.pipeqsize, 0, 0, 0);
	if(p->q[1] == 0){
		qfree(p->q[0]);
		free(p);
		exhausted("memory");
	}
	poperror();

	lock(&pipealloc);
	path = ++pipealloc.path;
	unlock(&pipealloc);

	mkqid(&c->qid, NETQID(path, Qdir), 0, QTDIR);
	c->aux = p;
	c->dev = 0;
	return c;
}

static int
pipegen(Chan *c, char*, Dirtab *tab, int ntab, int i, Dir *dp)
{
	Qid q;
	int len;
	Pipe *p;

	if(i == DEVDOTDOT){
		devdir(c, c->qid, "#|", 0, eve, 0555, dp);
		return 1;
	}
	i++;	/* skip . */
	if(tab==0 || i>=ntab)
		return -1;

	tab += i;
	p = c->aux;
	switch((ulong)tab->qid.path){
	case Qdata0:
		len = qlen(p->q[0]);
		break;
	case Qdata1:
		len = qlen(p->q[1]);
		break;
	default:
		len = tab->length;
		break;
	}
	mkqid(&q, NETQID(NETID(c->qid.path), tab->qid.path), 0, QTFILE);
	devdir(c, q, tab->name, len, eve, tab->perm, dp);
	return 1;
}


static Walkqid*
pipewalk(Chan *c, Chan *nc, char **name, int nname)
{
	Walkqid *wq;
	Pipe *p;

	wq = devwalk(c, nc, name, nname, pipedir, nelem(pipedir), pipegen);
	if(wq != nil && wq->clone != nil && wq->clone != c){
		p = c->aux;
		qlock(p);
		p->ref++;
		qunlock(p);
	}
	return wq;
}

static int
pipestat(Chan *c, uchar *db, int n)
{
	Pipe *p;
	Dir dir;
	ulong path;
	Dirtab *d;

	p = c->aux;
	path = NETTYPE(c->qid.path);
	d = pipedir + path;

	switch(path){
	case Qdir:
		devdir(c, c->qid, d->name, 0, eve, d->perm, &dir);
		break;
	case Qdata0:
		devdir(c, c->qid, d->name, qlen(p->q[0]), eve, d->perm, &dir);
		break;
	case Qdata1:
		devdir(c, c->qid, d->name, qlen(p->q[1]), eve, d->perm, &dir);
		break;
	default:
		panic("pipestat");
	}
	n = convD2M(&dir, db, n);
	if(n < BIT16SZ)
		error(Eshortstat);
	return n;
}

/*
 *  if the stream doesn't exist, create it
 */
static Chan*
pipeopen(Chan *c, int omode)
{
	Pipe *p;

	if(c->qid.type & QTDIR){
		if(omode != OREAD)
			error(Ebadarg);
		c->mode = omode;
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}

	p = c->aux;
	qlock(p);
	switch(NETTYPE(c->qid.path)){
	case Qdata0:
		p->qref[0]++;
		break;
	case Qdata1:
		p->qref[1]++;
		break;
	}
	qunlock(p);

	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	c->iounit = qiomaxatomic;
	return c;
}

static void
pipeclose(Chan *c)
{
	Pipe *p;

	p = c->aux;
	qlock(p);

	if(c->flag & COPEN){
		/*
		 *  closing either side hangs up the stream
		 */
		switch(NETTYPE(c->qid.path)){
		case Qdata0:
			p->qref[0]--;
			if(p->qref[0] == 0){
				qhangup(p->q[1], 0);
				qclose(p->q[0]);
			}
			break;
		case Qdata1:
			p->qref[1]--;
			if(p->qref[1] == 0){
				qhangup(p->q[0], 0);
				qclose(p->q[1]);
			}
			break;
		}
	}


	/*
	 *  if both sides are closed, they are reusable
	 */
	if(p->qref[0] == 0 && p->qref[1] == 0){
		qreopen(p->q[0]);
		qreopen(p->q[1]);
	}

	/*
	 *  free the structure on last close
	 */
	p->ref--;
	if(p->ref == 0){
		qunlock(p);
		free(p->q[0]);
		free(p->q[1]);
		free(p);
	} else
		qunlock(p);
}

static long
piperead(Chan *c, void *va, long n, vlong)
{
	Pipe *p;

	p = c->aux;

	switch(NETTYPE(c->qid.path)){
	case Qdir:
		return devdirread(c, va, n, pipedir, nelem(pipedir), pipegen);
	case Qdata0:
		return qread(p->q[0], va, n);
	case Qdata1:
		return qread(p->q[1], va, n);
	default:
		panic("piperead");
	}
}

static Block*
pipebread(Chan *c, long n, ulong offset)
{
	Pipe *p;

	p = c->aux;

	switch(NETTYPE(c->qid.path)){
	case Qdata0:
		return qbread(p->q[0], n);
	case Qdata1:
		return qbread(p->q[1], n);
	}

	return devbread(c, n, offset);
}

/*
 *  a write to a closed pipe causes a note to be sent to
 *  the process.
 */
static long
pipewrite(Chan *c, void *va, long n, vlong)
{
	Pipe *p;

	if(!islo())
		print("pipewrite hi %#p\n", getcallerpc(&c));
	if(waserror()) {
		/* avoid notes when pipe is a mounted queue */
		if((c->flag & CMSG) == 0)
			postnote(up, 1, "sys: write on closed pipe", NUser);
		nexterror();
	}

	p = c->aux;

	switch(NETTYPE(c->qid.path)){
	case Qdata0:
		n = qwrite(p->q[1], va, n);
		break;

	case Qdata1:
		n = qwrite(p->q[0], va, n);
		break;

	default:
		panic("pipewrite");
	}

	poperror();
	return n;
}

static long
pipebwrite(Chan *c, Block *bp, ulong)
{
	long n;
	Pipe *p;

	if(waserror()) {
		/* avoid notes when pipe is a mounted queue */
		if((c->flag & CMSG) == 0)
			postnote(up, 1, "sys: write on closed pipe", NUser);
		nexterror();
	}

	p = c->aux;
	switch(NETTYPE(c->qid.path)){
	case Qdata0:
		n = qbwrite(p->q[1], bp);
		break;

	case Qdata1:
		n = qbwrite(p->q[0], bp);
		break;

	default:
		panic("pipebwrite");
	}

	poperror();
	return n;
}

static int
pipewstat(Chan *c, uchar *dp, int n)
{
	Dir d;
	Pipe *p;

	n = convM2D(dp, n, &d, nil);
	if(n == 0)
		error(Eshortstat);

	p = c->aux;
	switch(NETTYPE(c->qid.path)){
	case Qdir:
		error(Eperm);
	case Qdata0:
	case Qdata1:
		if((uvlong)d.length > conf.pipeqsize)
			error(Ebadarg);
		qsetlimit(p->q[0], d.length);
		qsetlimit(p->q[1], d.length);
		break;
	default:
		panic("pipewstat");
	}

	return n;
}

Dev pipedevtab = {
	'|',
	"pipe",

	devreset,
	pipeinit,
	devshutdown,
	pipeattach,
	pipewalk,
	pipestat,
	pipeopen,
	devcreate,
	pipeclose,
	piperead,
	pipebread,
	pipewrite,
	pipebwrite,
	devremove,
	pipewstat,
};
