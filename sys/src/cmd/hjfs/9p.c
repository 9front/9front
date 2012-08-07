#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
#include "dat.h"
#include "fns.h"

extern Fs *fsmain;

static void
tauth(Req *req)
{
	if((fsmain->flags & FSNOAUTH) != 0)
		respond(req, "no authentication required");
	else
		auth9p(req);
}

static void
tattach(Req *req)
{
	Chan *ch;
	int flags;
	short uid;

	if((fsmain->flags & FSNOAUTH) == 0 && authattach(req) < 0)
		return;
	if(name2uid(fsmain, req->ifcall.uname, &uid) <= 0){
		respond(req, "no such user");
		return;
	}
	if(req->ifcall.aname == nil || *req->ifcall.aname == 0)
		flags = 0;
	else if(strcmp(req->ifcall.aname, "dump") == 0)
		flags = CHFDUMP|CHFRO;
	else{
		respond(req, Einval);
		return;
	}
	ch = chanattach(fsmain, flags);
	ch->uid = uid;
	req->fid->aux = ch;
	req->fid->qid = ch->loc->Qid;
	req->ofcall.qid = ch->loc->Qid;
	respond(req, nil);
}

static void
tqueue(Req *req)
{
	Chan *ch;

	if((req->fid->qid.type & QTAUTH) != 0){
		switch(req->ifcall.type){
		case Tread:
			authread(req);
			return;
		case Twrite:
			authwrite(req);
			return;
		default:
			respond(req, Einval);
			return;
		}
	}
	ch = req->fid->aux;
	if(ch == nil){
		respond(req, "operation on closed fid");
		return;
	}
	qlock(&chanqu);
	req->aux = nil;
	if(ch->freq == nil)
		ch->freq = req;
	if(ch->lreq != nil)
		((Req *) ch->lreq)->aux = req;
	ch->lreq = req;
	if(ch->qnext == nil){
		ch->qnext = &readych;
		ch->qprev = ch->qnext->qprev;
		ch->qnext->qprev = ch;
		ch->qprev->qnext = ch;
	}
	if(req->ifcall.type == Tremove)
		req->fid->aux = nil;
	rwakeup(&chanre);
	qunlock(&chanqu);
}

static void
tdestroyfid(Fid *fid)
{
	Chan *ch;

	if((fid->qid.type & QTAUTH) != 0){
		authdestroy(fid);
		return;
	}
	qlock(&chanqu);
	ch = fid->aux;
	fid->aux = nil;
	qunlock(&chanqu);
	if(ch != nil)
		chanclunk(ch);
}

static void
tend(Srv *)
{
	shutdown();
}

static Srv mysrv = {
	.auth = tauth,
	.attach = tattach,
	.walk = tqueue,
	.open = tqueue,
	.create = tqueue,
	.read = tqueue,
	.write = tqueue,
	.stat = tqueue,
	.wstat = tqueue,
	.remove = tqueue,
	.destroyfid = tdestroyfid,
	.end = tend,
};

void
start9p(char *service, int stdio)
{
	if(stdio){
		mysrv.infd = 1;
		mysrv.outfd = 1;
		srv(&mysrv);
	}else
		threadpostmountsrv(&mysrv, service, nil, 0);
}

static char *
tclone(Fid *old, Fid *new, void *)
{
	Chan *ch;
	
	ch = old->aux;
	if(ch->open != 0)
		return "trying to clone an open fid";
	new->aux = chanclone(ch);
	return nil;
}

static char *
twalk(Fid *fid, char *name, void *)
{
	Chan *ch;
	char buf[ERRMAX];
	
	ch = fid->aux;
	if(chanwalk(ch, name) < 0){
		rerrstr(buf, ERRMAX);
		return strdup(buf);
	}
	fid->qid = ch->loc->Qid;
	return nil;
}

static void
workerproc(void *)
{
	Chan *ch;
	Req *req;
	int rc;
	Fcall *i, *o;

	qlock(&chanqu);
	for(;;){
		while(readych.qnext == &readych)
			rsleep(&chanre);
		ch = readych.qnext;
		ch->qnext->qprev = ch->qprev;
		ch->qprev->qnext = ch->qnext;
		ch->qprev = nil;
		ch->qnext = nil;
		while(ch != nil && ch->freq != nil){
			req = ch->freq;
			ch->freq = req->aux;
			if(ch->lreq == req)
				ch->lreq = nil;
			req->aux = nil;
			qunlock(&chanqu);
			i = &req->ifcall;
			o = &req->ofcall;
			switch(req->ifcall.type){
			case Twalk:
				walkandclone(req, twalk, tclone, nil);
				goto noret;
			case Topen:
				rc = chanopen(ch, i->mode);
				break;
			case Tcreate:
				rc = chancreat(ch, i->name, i->perm, i->mode);
				if(rc >= 0){
					o->qid = ch->loc->Qid;
					req->fid->qid = o->qid;
				}
				break;
			case Tread:
				rc = o->count = chanread(ch, o->data, i->count, i->offset);
				break;
			case Twrite:
				rc = o->count = chanwrite(ch, i->data, i->count, i->offset);
				break;
			case Tremove:
				rc = chanremove(ch);
				req->fid->aux = ch = nil;
				break;
			case Tstat:
				rc = chanstat(ch, &req->d);
				break;
			case Twstat:
				rc = chanwstat(ch, &req->d);
				break;
			default:
				werrstr(Einval);
				rc = -1;
			}
			if(rc < 0)
				responderror(req);
			else
				respond(req, nil);
		noret:
			qlock(&chanqu);
		}
	}
}

QLock chanqu;
Chan readych;
Rendez chanre;

void
workerinit(void)
{
	int i;
	
	readych.qnext = readych.qprev = &readych;
	chanre.l = &chanqu;
	for(i = 0; i < NWORKERS; i++)
		threadcreate(workerproc, nil, mainstacksize);
}
