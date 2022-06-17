#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	<dtracy.h>

enum{
	Dwalk,
	Dstat,
	Dopen,
	Dcreate,
	Dclose,
	Dread,
	Dbread,
	Dwrite,
	Dbwrite,
	Dremove,
	Dwstat,

	Dend
};

static char *optab[] = {
	[Dwalk] "walk",
	[Dstat] "stat",
	[Dopen] "open",
	[Dcreate] "create",
	[Dclose] "close",
	[Dread] "read",
	[Dbread] "bread",
	[Dwrite] "write",
	[Dbwrite] "bwrite",
	[Dremove] "remove",
	[Dwstat] "wstat",
};

struct {
	DTProbe *in[Dend];
	DTProbe *out[Dend];
	Dev clean;
} ledger[256];

static Walkqid*
wrapwalk(Chan *c, Chan *nc, char **names, int nname)
{
	DTTrigInfo info;
	Walkqid *wq;


	memset(&info, 0, sizeof info);
	info.arg[0] = (uvlong)c;
	info.arg[1] = (uvlong)nc;
	info.arg[2] = (uvlong)names;
	info.arg[3] = (uvlong)nname;
	dtptrigger(ledger[c->type].in[Dwalk], &info);

	wq = ledger[c->type].clean.walk(c, nc, names, nname);

	memset(&info, 0, sizeof info);
	info.arg[9] = (uvlong)wq;
	dtptrigger(ledger[c->type].out[Dwalk], &info);
	return wq;
}

static int
wrapstat(Chan *c, uchar *b, int n)
{
	DTTrigInfo info;

	memset(&info, 0, sizeof info);
	info.arg[0] = (uvlong)c;
	info.arg[1] = (uvlong)b;
	info.arg[2] = (uvlong)n;
	dtptrigger(ledger[c->type].in[Dstat], &info);

	n = ledger[c->type].clean.stat(c, b, n);

	memset(&info, 0, sizeof info);
	info.arg[9] = (uvlong)n;
	dtptrigger(ledger[c->type].out[Dstat], &info);
	return n;
}

static Chan*
wrapopen(Chan *c, int mode)
{
	DTTrigInfo info;

	memset(&info, 0, sizeof info);
	info.arg[0] = (uvlong)c;
	info.arg[1] = (uvlong)mode;
	dtptrigger(ledger[c->type].in[Dopen], &info);

	c = ledger[c->type].clean.open(c, mode);

	memset(&info, 0, sizeof info);
	info.arg[9] = (uvlong)c;
	dtptrigger(ledger[c->type].out[Dopen], &info);
	return c;
}

static Chan*
wrapcreate(Chan *c, char *name, int mode, ulong perm)
{
	DTTrigInfo info;

	memset(&info, 0, sizeof info);
	info.arg[0] = (uvlong)c;
	info.arg[1] = (uvlong)name;
	info.arg[2] = (uvlong)mode;
	info.arg[3] = (uvlong)perm;
	dtptrigger(ledger[c->type].in[Dcreate], &info);

	c = ledger[c->type].clean.create(c, name, mode, perm);

	memset(&info, 0, sizeof info);
	info.arg[9] = (uvlong)c;
	dtptrigger(ledger[c->type].out[Dcreate], &info);
	return c;
}

static void
wrapclose(Chan *c)
{
	DTTrigInfo info;

	memset(&info, 0, sizeof info);
	info.arg[0] = (uvlong)c;
	dtptrigger(ledger[c->type].in[Dclose], &info);

	ledger[c->type].clean.close(c);

	memset(&info, 0, sizeof info);
	dtptrigger(ledger[c->type].out[Dclose], &info);
}

static long
wrapread(Chan *c, void *b, long n, vlong off)
{
	DTTrigInfo info;

	memset(&info, 0, sizeof info);
	info.arg[0] = (uvlong)c;
	info.arg[1] = (uvlong)b;
	info.arg[2] = (uvlong)n;
	info.arg[3] = (uvlong)off;
	dtptrigger(ledger[c->type].in[Dread], &info);

	n = ledger[c->type].clean.read(c, b, n, off);

	memset(&info, 0, sizeof info);
	info.arg[9] = (uvlong)n;
	dtptrigger(ledger[c->type].out[Dread], &info);
	return n;
}

static Block*
wrapbread(Chan *c, long n, ulong off)
{
	Block *b;
	DTTrigInfo info;

	memset(&info, 0, sizeof info);
	info.arg[0] = (uvlong)c;
	info.arg[1] = (uvlong)n;
	info.arg[2] = (uvlong)off;
	dtptrigger(ledger[c->type].in[Dbread], &info);

	b = ledger[c->type].clean.bread(c, n, off);

	memset(&info, 0, sizeof info);
	info.arg[9] = (uvlong)b;
	dtptrigger(ledger[c->type].out[Dbread], &info);
	return b;
}

static long
wrapwrite(Chan *c, void *b, long n, vlong off)
{
	DTTrigInfo info;

	memset(&info, 0, sizeof info);
	info.arg[0] = (uvlong)c;
	info.arg[1] = (uvlong)b;
	info.arg[2] = (uvlong)n;
	info.arg[3] = (uvlong)off;
	dtptrigger(ledger[c->type].in[Dwrite], &info);

	n = ledger[c->type].clean.write(c, b, n, off);

	memset(&info, 0, sizeof info);
	info.arg[9] = (uvlong)n;
	dtptrigger(ledger[c->type].out[Dwrite], &info);
	return n;
}

static long
wrapbwrite(Chan *c, Block *b, ulong off)
{
	long n;
	DTTrigInfo info;

	memset(&info, 0, sizeof info);
	info.arg[0] = (uvlong)c;
	info.arg[1] = (uvlong)b;
	info.arg[2] = (uvlong)off;
	dtptrigger(ledger[c->type].in[Dbwrite], &info);

	n = ledger[c->type].clean.bwrite(c, b, off);

	memset(&info, 0, sizeof info);
	info.arg[9] = (uvlong)n;
	dtptrigger(ledger[c->type].out[Dbwrite], &info);
	return n;
}

void
wrapremove(Chan *c)
{
	DTTrigInfo info;

	memset(&info, 0, sizeof info);
	info.arg[0] = (uvlong)c;
	dtptrigger(ledger[c->type].in[Dremove], &info);

	ledger[c->type].clean.remove(c);

	memset(&info, 0, sizeof info);
	dtptrigger(ledger[c->type].out[Dremove], &info);
}

int
wrapwstat(Chan *c, uchar *b, int n)
{
	DTTrigInfo info;

	memset(&info, 0, sizeof info);
	info.arg[0] = (uvlong)c;
	info.arg[1] = (uvlong)b;
	info.arg[2] = (uvlong)n;
	dtptrigger(ledger[c->type].in[Dwstat], &info);

	n = ledger[c->type].clean.wstat(c, b, n);

	memset(&info, 0, sizeof info);
	info.arg[9] = (uvlong)n;
	dtptrigger(ledger[c->type].out[Dwstat], &info);
	return n;
}

static void
devprovide(DTProvider *prov)
{
	uint i, j;
	uint path;
	char pname[32];
	char buf[32];

	for(i = 0; devtab[i] != nil; i++){
		memmove(&ledger[i].clean, devtab[i], sizeof(Dev));
		for(j = 0; j < Dend; j++){
			path = (i<<16) | j;
			snprint(buf, sizeof buf, "dev:%s:%s", devtab[i]->name, optab[j]);
			snprint(pname, sizeof pname, "%s:entry", buf);
			ledger[i].in[j] = dtpnew(pname, prov, (void *) path);
			snprint(pname, sizeof pname, "%s:return", buf);
			ledger[i].out[j] = dtpnew(pname, prov, (void *) path);
		}
	}
}

static int
devenable(DTProbe *p)
{
	uint i, j;
	uint path;
	Dev *d;
	
	path = (uint)(uintptr)p->aux;
	i = path>>16;
	j = path & ((1<<16)-1);
	assert(i < 256);
	assert(j < Dend);

	d = devtab[i];
	switch(j){
	case Dwalk:
		d->walk = wrapwalk;
		break;
	case Dstat:
		d->stat = wrapstat;
		break;
	case Dopen:
		d->open = wrapopen;
		break;
	case Dcreate:
		d->create = wrapcreate;
		break;
	case Dclose:
		d->close = wrapclose;
		break;
	case Dread:
		d->read = wrapread;
		break;
	case Dbread:
		d->bread = wrapbread;
		break;
	case Dwrite:
		d->write = wrapwrite;
		break;
	case Dbwrite:
		d->bwrite = wrapbwrite;
		break;
	case Dremove:
		d->remove = wrapremove;
		break;
	case Dwstat:
		d->wstat = wrapwstat;
		break;
	}
	return 0;
}

static void
devdisable(DTProbe *p)
{
	uint i, j;
	uint path;
	Dev *d, *clean;
	
	path = (uint)(uintptr)p->aux;
	i = path>>16;
	j = path & ((1<<16)-1);
	assert(i < 256);
	assert(j < Dend);

	d = devtab[i];
	clean = &ledger[i].clean;
	switch(j){
	case Dwalk:
		d->walk = clean->walk;
		break;
	case Dstat:
		d->stat = clean->stat;
		break;
	case Dopen:
		d->open = clean->open;
		break;
	case Dcreate:
		d->create = clean->create;
		break;
	case Dclose:
		d->close = clean->close;
		break;
	case Dread:
		d->read = clean->read;
		break;
	case Dbread:
		d->bread = clean->bread;
		break;
	case Dwrite:
		d->write = clean->write;
		break;
	case Dbwrite:
		d->bwrite = clean->bwrite;
		break;
	case Dremove:
		d->remove = clean->remove;
		break;
	case Dwstat:
		d->wstat = clean->wstat;
		break;
	}
}

DTProvider dtracydevprov = {
	.name = "dev",
	.provide = devprovide,
	.enable = devenable,
	.disable = devdisable,
};
