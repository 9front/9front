#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
#include "usb.h"
#include "dat.h"
#include "fns.h"

enum {
	Qroot,
	Qusbevent,
	Qmax
};

char *names[] = {
	"",
	"usbevent",
};

static char Enonexist[] = "does not exist";

typedef struct Event Event;

struct Event {
	char *data;
	int len;
	Event *link;
	int ref;  /* number of readers which will read this one
                     the next time they'll read */
	int prev; /* number of events pointing to this one with
                     their link pointers */
};

static Event *evlast;
static Req *reqfirst, *reqlast;
static QLock evlock;

static void
addreader(Req *req)
{
	req->aux = nil;
	if(reqfirst == nil)
		reqfirst = req;
	else
		reqlast->aux = req;
	reqlast = req;
}

static void
fulfill(Req *req, Event *e)
{
	int n;
	
	n = e->len;
	if(n > req->ifcall.count)
		n = req->ifcall.count;
	memmove(req->ofcall.data, e->data, n);
	req->ofcall.count = n;
}

static void
initevent(void)
{
	evlast = emallocz(sizeof(Event), 1);
}

static void
readevent(Req *req)
{
	Event *e;

	qlock(&evlock);
	e = req->fid->aux;
	if(e == evlast){
		addreader(req);
		qunlock(&evlock);
		return;
	}
	fulfill(req, e);
	req->fid->aux = e->link;
	e->link->ref++;
	if(--e->ref == 0 && e->prev == 0){
		e->link->prev--;
		free(e->data);
		free(e);
	}
	qunlock(&evlock);
	respond(req, nil);
}

static void
pushevent(char *data)
{
	Event *e, *ee;
	Req *r, *rr;
	
	qlock(&evlock);
	e = evlast;
	ee = emallocz(sizeof(Event), 1);
	evlast = ee;
	e->data = data;
	e->len = strlen(data);
	e->link = ee;
	ee->prev++;
	for(r = reqfirst; r != nil; r = rr){
		rr = r->aux;
		r->aux = nil;
		r->fid->aux = ee;
		ee->ref++;
		e->ref--;
		fulfill(r, e);
		respond(r, nil);
	}
	if(e->ref == 0 && e->prev == 0){
		ee->prev--;
		free(e->data);
		free(e);
	}
	reqfirst = nil;
	reqlast = nil;
	qunlock(&evlock);
}

static int
dirgen(int n, Dir *d, void *)
{
	if(n >= Qmax - 1)
		return -1;
	d->qid.path = n + 1;
	d->qid.vers = 0;
	if(n >= 0)
		d->qid.type = 0;
	else
		d->qid.type = QTDIR;
	d->uid = strdup(getuser());
	d->gid = strdup(d->uid);
	d->muid = strdup(d->uid);
	d->name = strdup(names[n+1]);
	d->mode = 0555 | (d->qid.type << 24);
	d->atime = d->mtime = time(0);
	d->length = 0;
	return 0;
}

static void
usbdattach(Req *req)
{
	req->fid->qid = (Qid) {Qroot, 0, QTDIR};
	req->ofcall.qid = req->fid->qid;
	respond(req, nil);
}

static char *
usbdwalk(Fid *fid, char *name, Qid *qid)
{
	int i;

	if(strcmp(name, "..") == 0){
		fid->qid = (Qid) {Qroot, 0, QTDIR};
		*qid = fid->qid;
		return nil;
	}
	if(fid->qid.path != Qroot)
		return "not a directory";
	for(i = 0; i < Qmax; i++)
		if(strcmp(name, names[i]) == 0){
			fid->qid = (Qid) {i, 0, 0};
			*qid = fid->qid;
			return nil;
		}
	return "does not exist";
}

static void
usbdread(Req *req)
{
	switch((long)req->fid->qid.path){
	case Qroot:
		dirread9p(req, dirgen, nil);
		respond(req, nil);
		break;
	case Qusbevent:
		if(req->fid->aux == nil){
			respond(req, "the front fell off");
			return;
		}
		readevent(req);
		break;
	default:
		respond(req, Enonexist);
		break;
	}
}

static void
usbdstat(Req *req)
{
	if(dirgen(req->fid->qid.path - 1, &req->d, nil) < 0)
		respond(req, "the front fell off");
	else
		respond(req, nil);
}

static char *
formatdev(Dev *d)
{
	Usbdev *u;
	
	u = d->usb;
	return smprint("dev %d %.4x %.4x %.8lx\n", d->id, u->vid, u->did, u->csp);
}

static void
enumerate(Event **l)
{
	Event *e;
	Hub *h;
	Port *p;
	extern Hub *hubs;
	
	for(h = hubs; h != nil; h = h->next){
		for(p = h->port; p < h->port + h->nport; p++){
			if(p->dev == nil || p->dev->usb == nil || p->hub != nil)
				continue;
			e = emallocz(sizeof(Event), 1);
			e->data = formatdev(p->dev);
			e->len = strlen(e->data);
			e->prev = 1;
			*l = e;
			l = &e->link;
		}
	}
	*l = evlast;
	evlast->prev++;
}

static void
usbdopen(Req *req)
{
	extern QLock hublock;

	if(req->fid->qid.path == Qusbevent){
		qlock(&hublock);
		qlock(&evlock);
		enumerate(&req->fid->aux);
		((Event *)req->fid->aux)->ref++;
		((Event *)req->fid->aux)->prev--;
		qunlock(&evlock);
		qunlock(&hublock);
	}
	respond(req, nil);
}

static void
usbddestroyfid(Fid *fid)
{
	Event *e, *ee;

	if(fid->qid.path == Qusbevent && fid->aux != nil){
		qlock(&evlock);
		e = fid->aux;
		if(--e->ref == 0 && e->prev == 0){
			while(e->ref == 0 && e->prev == 0 && e != evlast){
				ee = e->link;
				ee->prev--;
				free(e->data);
				free(e);
				e = ee;
			}
		}
		qunlock(&evlock);
	}
}

static void
usbdflush(Req *req)
{
	Req **l, *r;
	qlock(&evlock);
	l = &reqfirst;
	while(r = *l){
		if(r == req->oldreq){
			*l = r->aux;
			break;
		}
		l = &r->aux;
	}
	qunlock(&evlock);
	respond(req->oldreq, "interrupted");
	respond(req, nil);
}

Srv usbdsrv = {
	.attach = usbdattach,
	.walk1 = usbdwalk,
	.read = usbdread,
	.stat = usbdstat,
	.open = usbdopen,
	.flush = usbdflush,
	.destroyfid = usbddestroyfid,
};

int
startdev(Port *p)
{
	Dev *d;

	if((d = p->dev) == nil || p->dev->usb == nil){
		fprint(2, "okay what?\n");
		return -1;
	}
	if(d->usb->class == Clhub){
		/*
		 * Hubs are handled directly by this process avoiding
		 * concurrent operation so that at most one device
		 * has the config address in use.
		 * We cancel kernel debug for these eps. too chatty.
		 */
		if((p->hub = newhub(d->dir, d)) == nil)
			return -1;
		return 0;
	}
	close(d->dfd);
	d->dfd = -1;
	pushevent(formatdev(d));
	return 0;
}

void
main(int argc, char **argv)
{
	int fd, i, nd;
	Dir *d;

	ARGBEGIN {
	} ARGEND;

	initevent();
	rfork(RFNOTEG);
	switch(rfork(RFPROC|RFMEM)){
	case -1: sysfatal("rfork: %r");
	case 0: work(); exits(nil);
	}
	if(argc == 0){
		fd = open("/dev/usb", OREAD);
		if(fd < 0)
			sysfatal("/dev/usb: %r");
		nd = dirreadall(fd, &d);
		close(fd);
		if(nd < 2)
			sysfatal("/dev/usb: no hubs");
		for(i = 0; i < nd; i++)
			if(strcmp(d[i].name, "ctl") != 0)
				rendezvous(work, smprint("/dev/usb/%s", d[i].name));
		free(d);
	}else
		for(i = 0; i < argc; i++)
			rendezvous(work, strdup(argv[i]));
	rendezvous(work, nil);
	postsharesrv(&usbdsrv, nil, "usb", "usbd", "b");
	exits(nil);
}
