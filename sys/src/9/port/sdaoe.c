/*
 * aoe sd driver, copyright © 2007-9 coraid
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
#include "../port/aoe.h"
#include <fis.h>

extern	char	Echange[];
extern	char	Enotup[];

#define uprint(...)	snprint(up->genbuf, sizeof up->genbuf, __VA_ARGS__);

enum {
	Maxpath		= 128,

	Probeintvl	= 100,		/* ms. between probes */
	Probemax	= 10*1000,	/* max ms. to wait */
};

typedef struct Ctlr Ctlr;
struct Ctlr {
	QLock;

	Ctlr	*next;
	SDunit	*unit;

	char	path[Maxpath];
	Chan	*c;

	ulong	vers;
	uchar	drivechange;
	Sfis;

	uvlong	sectors;
	char	serial[20+1];
	char	firmware[8+1];
	char	model[40+1];
	char	ident[0x100];
};

static	Lock	ctlrlock;
static	Ctlr	*head;
static	Ctlr	*tail;

SDifc sdaoeifc;

static int
identify(Ctlr *c, ushort *id)
{
	uchar oserial[21];
	vlong osectors, s;

	osectors = c->sectors;
	memmove(oserial, c->serial, sizeof c->serial);
	s = idfeat(c, id);
	if(s == -1){
		uprint("%s: identify fails", c->unit->name);
		print("%s\n", up->genbuf);
		error(up->genbuf);
	}
	idmove(c->serial, id+10, 20);
	idmove(c->firmware, id+23, 8);
	idmove(c->model, id+27, 40);

	if((osectors == 0 || osectors != s) &&
	    memcmp(oserial, c->serial, sizeof oserial) != 0){
		c->sectors = s;
		c->drivechange = 1;
		c->vers++;
	}
	return 0;
}

static void
aoectl(Ctlr *d, char *s)
{
	Chan *c;

	uprint("%s/ctl", d->path);
	c = namec(up->genbuf, Aopen, OWRITE, 0);
	if(waserror()){
		print("sdaoectl: %s\n", up->errstr);
		cclose(c);
		nexterror();
	}
	devtab[c->type]->write(c, s, strlen(s), 0);
	cclose(c);
	poperror();
}

/* must call with d qlocked */
static int
aoeidentify(Ctlr *d, SDunit *u)
{
	Chan *c;

	uprint("%s/ident", d->path);
	c = namec(up->genbuf, Aopen, OREAD, 0);
	if(waserror()){
		iprint("aoeidentify: %s\n", up->errstr);
		cclose(c);
		nexterror();
	}
	devtab[c->type]->read(c, d->ident, sizeof d->ident, 0);
	cclose(c);
	poperror();

	d->feat = 0;
	identify(d, (ushort*)d->ident);

	memset(u->inquiry, 0, sizeof u->inquiry);
	u->inquiry[2] = 2;
	u->inquiry[3] = 2;
	u->inquiry[4] = sizeof u->inquiry - 4;
	memmove(u->inquiry+8, d->model, 40);

	return 0;
}

static Ctlr*
ctlrlookup(char *path)
{
	Ctlr *c;

	lock(&ctlrlock);
	for(c = head; c != nil; c = c->next)
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
		return 0;
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

	for(prev = 0, x = head; x != nil; prev = x, x = c->next)
		if(strcmp(c->path, x->path) == 0)
			break;
	if(x == nil){
		unlock(&ctlrlock);
		error(Enonexist);
	}

	if(prev != nil)
		prev->next = x->next;
	else
		head = x->next;
	if(x->next == nil)
		tail = prev;
	unlock(&ctlrlock);

	if(x->c != nil)
		cclose(x->c);
	free(x);
}

static SDev*
aoeprobe(char *path, SDev *s)
{
	int n, i;
	char *p;
	Chan *c;
	Ctlr *ctlr;

	if((p = strrchr(path, '/')) == nil)
		error(Ebadarg);
	*p = 0;
	uprint("%s/ctl", path);
	*p = '/';
	c = namec(up->genbuf, Aopen, OWRITE, 0);
	if(waserror()) {
		cclose(c);
		nexterror();
	}
	n = uprint("discover %s", p+1);
	devtab[c->type]->write(c, up->genbuf, n, 0);
	cclose(c);
	poperror();

	for(i = 0;; i += Probeintvl){
		if(i > Probemax || waserror())
			error(Etimedout);
		tsleep(&up->sleep, return0, 0, Probeintvl);
		poperror();

		uprint("%s/ident", path);
		if(waserror())
			continue;
		c = namec(up->genbuf, Aopen, OREAD, 0);
		poperror();
		cclose(c);

		ctlr = newctlr(path);
		break;
	}

	if(s == nil && (s = malloc(sizeof *s)) == nil)
		return nil;
	s->ctlr = ctlr;
	s->ifc = &sdaoeifc;
	s->nunit = 1;
	return s;
}

static char 	*probef[32];
static char	*probebuf;
static int 	nprobe;

static SDev*
aoepnp(void)
{
	int i, id;
	char *p;
	SDev *h, *t, *s;

	if((p = getconf("aoedev")) == nil)
		return 0;
	kstrdup(&probebuf, p);
	nprobe = tokenize(probebuf, probef, nelem(probef));
	h = t = 0;
	for(i = 0; i < nprobe; i++){
		p = probef[i];
		if(strlen(p) < 2)
			continue;
		id = 'e';
		if(p[1] == '!'){
			id = p[0];
			p += 2;
		}
		/*
		 * shorthand for: id!lun -> id!#æ/aoe/lun
		 * because we cannot type æ in the bootloader console.
		 */
		if(strchr(p, '/') == nil){
			char tmp[64];

			snprint(tmp, sizeof(tmp), "%c!#æ/aoe/%s", (char)id, p);

			probef[i] = nil;
			kstrdup(&probef[i], tmp);
		}
		s = malloc(sizeof *s);
		if(s == nil)
			break;
		s->ctlr = 0;
		s->idno = id;
		s->ifc = &sdaoeifc;
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
pnpprobe(SDev *sd)
{
	int j;
	char *p;
	static int i;

	if(i > nprobe)
		return 0;
	p = probef[i++];
	if(strlen(p) < 2)
		return 0;
	if(p[1] == '!')
		p += 2;

	for(j = 0;; j += Probeintvl){
		if(j > Probemax){
			print("#æ: pnpprobe: %s: %s\n", probef[i-1], up->errstr);
			return 0;
		}
		if(waserror()){
			tsleep(&up->sleep, return0, 0, Probeintvl);
			continue;
		}
		sd = aoeprobe(p, sd);
		poperror();
		break;
	}
	print("#æ: pnpprobe establishes %s in %dms\n", probef[i-1], j);
	aoectl(sd->ctlr, "nofail on");
	return sd->ctlr;
}


static int
aoeverify(SDunit *u)
{
	SDev *s;
	Ctlr *c;

	s = u->dev;
	c = s->ctlr;
	if(c == nil && (s->ctlr = c = pnpprobe(s)) == nil)
		return 0;
	c->drivechange = 1;
	return 1;
}

static int
aoeconnect(SDunit *u, Ctlr *c)
{
	qlock(c);
	if(waserror()){
		qunlock(c);
		return -1;
	}

	aoeidentify(u->dev->ctlr, u);
	if(c->c)
		cclose(c->c);
	c->c = 0;
	uprint("%s/data", c->path);
	c->c = namec(up->genbuf, Aopen, ORDWR, 0);
	qunlock(c);
	poperror();

	return 0;
}

static int
aoeonline(SDunit *u)
{
	Ctlr *c;
	int r;

	c = u->dev->ctlr;
	r = 0;

	if((c->feat&Datapi) && c->drivechange){
		if(aoeconnect(u, c) == 0 && (r = scsionline(u)) > 0)
			c->drivechange = 0;
		return r;
	}

	if(c->drivechange){
		if(aoeconnect(u, c) == -1)
			return 0;
		r = 2;
		c->drivechange = 0;
		u->sectors = c->sectors;
		u->secsize = Aoesectsz;
	} else
		r = 1;

	return r;
}

static long
aoebio(SDunit *u, int, int write, void *a, long count, uvlong lba)
{
	uchar *data;
	int n;
	long (*rio)(Chan*, void*, long, vlong);
	Ctlr *c;

	c = u->dev->ctlr;
//	if(c->feat & Datapi)
//		return scsibio(u, lun, write, a, count, lba);
	data = a;
	if(write)
		rio = devtab[c->c->type]->write;
	else
		rio = devtab[c->c->type]->read;

	if(waserror()){
		if(strcmp(up->errstr, Echange) == 0 ||
		    strcmp(up->errstr, Enotup) == 0)
			u->sectors = 0;
		nexterror();
	}
	n = rio(c->c, data, Aoesectsz * count, Aoesectsz * lba);
	poperror();
	return n;
}

static int
flushcache(Ctlr *)
{
	return -1;
}

static int
aoerio(SDreq *r)
{
	int i, count, rw;
	uvlong lba;
	Ctlr *c;
	SDunit *u;

	u = r->unit;
	c = u->dev->ctlr;
//	if(c->feat & Datapi)
//		return aoeriopkt(r, d);

	if(r->cmd[0] == 0x35 || r->cmd[0] == 0x91){
		qlock(c);
		i = flushcache(c);
		qunlock(c);
		if(i == 0)
			return sdsetsense(r, SDok, 0, 0, 0);
		return sdsetsense(r, SDcheck, 3, 0xc, 2);
	}

	if((i = sdfakescsi(r)) != SDnostatus){
		r->status = i;
		return i;
	}
	if((i = sdfakescsirw(r, &lba, &count, &rw)) != SDnostatus)
		return i;
	r->rlen = aoebio(u, r->lun, rw == SDwrite, r->data, count, lba);
	return r->status = SDok;
}

static int
aoerctl(SDunit *u, char *p, int l)
{
	Ctlr *c;
	char *e, *op;

	if((c = u->dev->ctlr) == nil)
		return 0;
	e = p+l;
	op = p;

	p = seprint(p, e, "model\t%s\n", c->model);
	p = seprint(p, e, "serial\t%s\n", c->serial);
	p = seprint(p, e, "firm	%s\n", c->firmware);
	p = seprint(p, e, "flag	");
	p = pflag(p, e, c);
	p = seprint(p, e, "geometry %llud %d\n", c->sectors, Aoesectsz);
	return p-op;
}

static int
aoewctl(SDunit *, Cmdbuf *cmd)
{
	cmderror(cmd, Ebadarg);
	return 0;
}

static SDev*
aoeprobew(DevConf *c)
{
	char *p;

	p = strchr(c->type, '/');
	if(p == nil || strlen(p) > Maxpath - 11)
		error(Ebadarg);
	if(p[1] == '#')
		p++;			/* hack */
	if(ctlrlookup(p))
		error(Einuse);
	return aoeprobe(p, 0);
}

static void
aoeclear(SDev *s)
{
	delctlr((Ctlr *)s->ctlr);
}

static char*
aoertopctl(SDev *s, char *p, char *e)
{
	Ctlr *c;

	c = s->ctlr;
	return seprint(p, e, "%s aoe %s\n", s->name, c? c->path: "");
}

static int
aoewtopctl(SDev *, Cmdbuf *cmd)
{
	switch(cmd->nf){
	default:
		cmderror(cmd, Ebadarg);
	}
	return 0;
}

SDifc sdaoeifc = {
	"aoe",

	aoepnp,
	nil,		/* legacy */
	nil,		/* enable */
	nil,		/* disable */

	aoeverify,
	aoeonline,
	aoerio,
	aoerctl,
	aoewctl,

	aoebio,
	aoeprobew,	/* probe */
	aoeclear,	/* clear */
	aoertopctl,
	aoewtopctl,
};
