/*
 * sd loopback driver,
 * copyright © 2009-10 erik quanstrom
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/sd.h"
#include "../port/netif.h"

extern	char	Echange[];

enum {
	Maxpath		= 256,
	Devsectsize	= 512,
};

typedef struct Ctlr Ctlr;
struct Ctlr {
	QLock;

	Ctlr	*next;
	SDunit	*unit;

	char	path[Maxpath];
	Chan	*c;

	uint	vers;
	uchar	drivechange;

	uvlong	sectors;
	uint	sectsize;
};

static	Lock	ctlrlock;
static	Ctlr	*head;
static	Ctlr	*tail;

SDifc sdloopifc;

/* must call with c qlocked */
static void
identify(Ctlr *c, SDunit *u)
{
	int n;
	uvlong s, osectors;
	uchar buf[sizeof(Dir) + 100];
	Dir dir;

	if(waserror()){
		iprint("sdloop: identify: %s\n", up->errstr);
		nexterror();
	}
	osectors = c->sectors;
	n = devtab[c->c->type]->stat(c->c, buf, sizeof buf);
	if(convM2D(buf, n, &dir, nil) == 0)
		error("internal error: stat error in seek");
	s = dir.length / c->sectsize;
	poperror();

	memset(u->inquiry, 0, sizeof u->inquiry);
	u->inquiry[2] = 2;
	u->inquiry[3] = 2;
	u->inquiry[4] = sizeof u->inquiry - 4;
	memmove(u->inquiry+8, c->path, 40);

	if(osectors == 0 || osectors != s){
		c->sectors = s;
		c->drivechange = 1;
		c->vers++;
	}
}

static Ctlr*
ctlrlookup(char *path)
{
	Ctlr *c;

	lock(&ctlrlock);
	for(c = head; c; c = c->next)
		if(strcmp(c->path, path) == 0)
			break;
	unlock(&ctlrlock);
	return c;
}

static Ctlr*
newctlr(char *path)
{
	Ctlr *c;

	if(ctlrlookup(path))
		error(Eexist);
	if((c = malloc(sizeof *c)) == nil)
		error(Enomem);
	if(waserror()){
		free(c);
		nexterror();
	}
	c->c = namec(path, Aopen, ORDWR, 0);
	poperror();
	kstrcpy(c->path, path, sizeof c->path);
	lock(&ctlrlock);
	if(head != nil)
		tail->next = c;
	else
		head = c;
	tail = c;
	unlock(&ctlrlock);
	return c;
}

static void
delctlr(Ctlr *c)
{
	Ctlr *x, *prev;

	lock(&ctlrlock);

	for(prev = 0, x = head; x; prev = x, x = c->next)
		if(strcmp(c->path, x->path) == 0)
			break;
	if(x == 0){
		unlock(&ctlrlock);
		error(Enonexist);
	}

	if(prev)
		prev->next = x->next;
	else
		head = x->next;
	if(x->next == nil)
		tail = prev;
	unlock(&ctlrlock);

	if(x->c)
		cclose(x->c);
	free(x);
}

static SDev*
probe(char *path, SDev *s)
{
	char *p;
	uint sectsize;
	Ctlr *c;

	sectsize = 0;
	if(p = strchr(path, '!')){
		*p = 0;
		sectsize = strtoul(p + 1, 0, 0);
	}
	c = newctlr(path);
	c->sectsize = sectsize? sectsize: Devsectsize;
	if(s == nil && (s = malloc(sizeof *s)) == nil)
		return nil;
	s->ctlr = c;
	s->ifc = &sdloopifc;
	s->nunit = 1;
	return s;
}

static char 	*probef[32];
static int 	nprobe;

static int
pnpprobeid(char *s)
{
	int id;

	if(strlen(s) < 2)
		return 0;
	id = 'l';
	if(s[1] == '!')
		id = s[0];
	return id;
}

static SDev*
pnp(void)
{
	int i, id;
	char *p;
	SDev *h, *t, *s;

	if((p = getconf("loopdev")) == 0)
		return 0;
	nprobe = tokenize(p, probef, nelem(probef));
	h = t = 0;
	for(i = 0; i < nprobe; i++){
		id = pnpprobeid(probef[i]);
		if(id == 0)
			continue;
		s = malloc(sizeof *s);
		if(s == nil)
			break;
		s->ctlr = 0;
		s->idno = id;
		s->ifc = &sdloopifc;
		s->nunit = 1;

		if(h)
			t->next = s;
		else
			h = s;
		t = s;
	}
	return h;
}

static Ctlr*
pnpprobe(SDev *s)
{
	char *p;
	static int i;

	if(i > nprobe)
		return 0;
	p = probef[i++];
	if(strlen(p) < 2)
		return 0;
	if(p[1] == '!')
		p += 2;
	s = probe(p, s);
	return s->ctlr;
}


static int
loopverify(SDunit *u)
{
	SDev *s;
	Ctlr *c;

	s = u->dev;
	c = s->ctlr;
	if(c == nil){
		if(waserror())
			return 0;
		s->ctlr = c = pnpprobe(s);
		poperror();
	}
	c->drivechange = 1;
	return 1;
}

static int
connect(SDunit *u, Ctlr *c)
{
	qlock(c);
	if(waserror()){
		qunlock(c);
		return -1;
	}
	identify(u->dev->ctlr, u);
	qunlock(c);
	poperror();
	return 0;
}

static int
looponline(SDunit *u)
{
	Ctlr *c;
	int r;

	c = u->dev->ctlr;
	if(c->drivechange){
		if(connect(u, c) == -1)
			return 0;
		r = 2;
		c->drivechange = 0;
		u->sectors = c->sectors;
		u->secsize = c->sectsize;
	} else
		r = 1;
	return r;
}

static long
loopbio(SDunit *u, int, int write, void *a, long count, uvlong lba)
{
	uchar *data;
	int n;
	long (*rio)(Chan*, void*, long, vlong);
	Ctlr *c;

	c = u->dev->ctlr;
	data = a;
	if(write)
		rio = devtab[c->c->type]->write;
	else
		rio = devtab[c->c->type]->read;

	if(waserror()){
		if(strcmp(up->errstr, Echange) == 0 ||
		    strstr(up->errstr, "device is down") != nil)
			u->sectors = 0;
		nexterror();
	}
	n = rio(c->c, data, c->sectsize * count, c->sectsize * lba);
	poperror();
	return n;
}

static int
looprio(SDreq *r)
{
	int i, count, rw;
	uvlong lba;
	SDunit *u;

	u = r->unit;

	if(r->cmd[0] == 0x35 || r->cmd[0] == 0x91)
		return sdsetsense(r, SDok, 0, 0, 0);

	if((i = sdfakescsi(r)) != SDnostatus)
		return r->status = i;
	if((i = sdfakescsirw(r, &lba, &count, &rw)) != SDnostatus)
		return i;
	r->rlen = loopbio(u, r->lun, rw == SDwrite, r->data, count, lba);
	return r->status = SDok;
}

static int
looprctl(SDunit *u, char *p, int l)
{
	Ctlr *c;
	char *e, *op;

	if((c = u->dev->ctlr) == nil)
		return 0;
	e = p+l;
	op = p;

	p = seprint(p, e, "path\t%s\n", c->path);
	p = seprint(p, e, "geometry %llud %d\n", c->sectors, c->sectsize);
	return p - op;
}

static int
loopwctl(SDunit *, Cmdbuf *cmd)
{
	cmderror(cmd, Ebadarg);
	return 0;
}

static SDev*
loopprobew(DevConf *c)
{
	char *p;

	p = strchr(c->type, '/');
	if(p == nil || strlen(p) > Maxpath - 1)
		error(Ebadarg);
	p++;
	if(ctlrlookup(p))
		error(Einuse);
	return probe(p, 0);
}

static void
loopclear(SDev *s)
{
	delctlr((Ctlr *)s->ctlr);
}

static char*
looprtopctl(SDev *s, char *p, char *e)
{
	Ctlr *c;

	c = s->ctlr;
	return seprint(p, e, "%s loop %s\n", s->name, c? c->path: "");
}

static int
loopwtopctl(SDev *, Cmdbuf *cmd)
{
	switch(cmd->nf){
	default:
		cmderror(cmd, Ebadarg);
	}
	return 0;
}

SDifc sdloopifc = {
	"loop",

	pnp,
	nil,		/* legacy */
	nil,		/* enable */
	nil,		/* disable */

	loopverify,
	looponline,
	looprio,
	looprctl,
	loopwctl,

	loopbio,
	loopprobew,	/* probe */
	loopclear,	/* clear */
	looprtopctl,
	loopwtopctl,
};
