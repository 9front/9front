#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include "dat.h"

Reqqueue *queue;

static char *addrwalk(Fid *, char *, Qid *);
char *balancestr(DirEntry *, Aux *);
char *txstr(DirEntry *, Aux *);

DirEntry entr[] = {
	[TROOT] = {
		.name = "",
		.qid = {TROOT, 0, QTDIR},
		.par = TROOT,
		.sub = {TADDR},
	},
	[TADDR] = {
		.name = "addr",
		.qid = {TADDR, 0, QTDIR},
		.walk = addrwalk,
		.par = TROOT,
	},
	[TADDRSUB] = {
		.qid = {TADDRSUB, 0, QTDIR},
		.par = TADDR,
		.sub = {TADDRBALANCE, TADDRTX},
	},
	[TADDRBALANCE] = {
		.name = "balance",
		.qid = {TADDRBALANCE, 0, 0},
		.par = TADDRSUB,
		.str = balancestr,
	},
	[TADDRTX] = {
		.name = "tx",
		.qid = {TADDRTX, 0, 0},
		.par = TADDRSUB,
		.str = txstr,
	},
};

void
pfree(void **v)
{
	if(*v != nil)
		free(*v);
	*v = nil;
}

static void
btcattach(Req *req)
{
	req->ofcall.qid = (Qid){0, 0, QTDIR};
	req->fid->qid = req->ofcall.qid;
	req->fid->aux = emalloc9p(sizeof(Aux));
	respond(req, nil);
}

static char *
addrwalk(Fid *fid, char *name, Qid *qid)
{
	Aux *a;

	a = fid->aux;
	pfree(&a->addr);
	a->addr = strdup(name);
	fid->qid = entr[TADDRSUB].qid;
	*qid = fid->qid;
	return nil;
}

static char *
btcwalk1(Fid *fid, char *name, Qid *qid)
{
	DirEntry *d;
	int *s;
	
	d = entr + (int)fid->qid.path;
	if(strcmp(name, "..") == 0){
		fid->qid = entr[d->par].qid;
		*qid = fid->qid;
		return nil;
	}
	if(d->walk)
		return d->walk(fid, name, qid);
	for(s = d->sub; *s; s++)
		if(strcmp(entr[*s].name, name) == 0){
			fid->qid = entr[*s].qid;
			*qid = fid->qid;
			return nil;
		}
	return "directory entry not found";
}

static char *
btcclone(Fid *oldfid, Fid *newfid)
{
	Aux *a, *b;
	
	a = oldfid->aux;
	b = emalloc9p(sizeof(Aux));
	memcpy(b, a, sizeof(Aux));
	if(b->addr)
		b->addr = strdup(b->addr);
	newfid->aux = b;
	return nil;
}

static void
btcopenread(Req *req)
{
	DirEntry *d;
	Aux *a;
	
	d = entr + (int)req->fid->qid.path;
	a = req->fid->aux;
	a->str = d->str(d, a);
	if(a->str == nil)
		responderror(req);
	else
		respond(req, nil);
}

static void
btcopen(Req *req)
{
	DirEntry *d;
	
	d = entr + (int)req->fid->qid.path;
	switch(req->ifcall.mode & 3){
	case OREAD:
		if((req->fid->qid.type & QTDIR) != 0)
			break;
		if(d->str == nil)
			goto noperm;
		reqqueuepush(queue, req, btcopenread);
		return;
	case OWRITE:
		if(d->write == nil)
			goto noperm;
		break;
	case ORDWR:
		if(d->str == nil || d->write == nil)
			goto noperm;
		break;
	case OEXEC:
		goto noperm;
	}
	respond(req, nil);
	return;
noperm:
	respond(req, "permission denied");
}

static void
fill(Dir *de, int t, Aux *a)
{
	DirEntry *d;

	d = entr + t;
	de->qid = d->qid;
	if(d->qid.type & QTDIR)
		de->mode = 0555;
	else
		de->mode = (d->str ? 0444 : 0) | (d->write ? 0222 : 0);
	if(d->name != nil)
		de->name = strdup(d->name);
	else if(a->addr != nil)
		de->name = strdup(a->addr);
	else
		de->name = strdup("");
	de->uid = strdup("satoshi");
	de->gid = strdup("satoshi");
	de->muid = strdup("satoshi");
	de->atime = de->mtime = time(0);
}

static void
btcstat(Req *req)
{
	fill(&req->d, (int)req->fid->qid.path, req->fid->aux);
	respond(req, nil);
}

static int
btcdirgen(int n, Dir *dir, void *aux)
{
	DirEntry *d;
	
	d = entr + (int)((Req*)aux)->fid->qid.path;
	if(n >= nelem(d->sub) || d->sub[n] == 0)
		return -1;
	fill(dir, d->sub[n], ((Req*)aux)->fid->aux);
	return 0;
}

static void
btcread(Req *req)
{
	DirEntry *d;
	Aux *a;
	
	d = entr + (int)req->fid->qid.path;
	a = req->fid->aux;
	if(req->fid->qid.type & QTDIR){
		dirread9p(req, btcdirgen, req);
		respond(req, nil);
	}else if(d->str && a->str){
		readstr(req, a->str);
		respond(req, nil);
	}else
		respond(req, "permission denied");	
}

static void
btcflush(Req *req)
{
	reqqueueflush(queue, req->oldreq);
	respond(req, nil);
}

static void
btcdestroyfid(Fid *fid)
{
	Aux *a;
	
	a = fid->aux;
	if(a != nil){
		pfree(&a->addr);
		pfree(&a->str);
		free(a);
	}
	fid->aux = nil;
}

Srv btcsrv = {
	.attach = btcattach,
	.walk1 = btcwalk1,
	.clone = btcclone,
	.stat = btcstat,
	.open = btcopen,
	.read = btcread,
	.flush = btcflush,
	.destroyfid = btcdestroyfid,
};

void
gofs(void)
{
	queue = reqqueuecreate();
	threadpostmountsrv(&btcsrv, nil, "/mnt/btc", 0);
}
