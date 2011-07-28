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
	int ref;
};

static Event *evfirst, *evlast;
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
	evfirst = mallocz(sizeof(*evfirst), 1);
	if(evfirst == nil)
		sysfatal("malloc: %r");
	evlast = evfirst;
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
	if(--e->ref == 0 && e == evfirst){
		evfirst = e->link;
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
	if(ee == nil)
		sysfatal("malloc: %r");
	evlast = ee;
	e->data = data;
	e->len = strlen(data);
	e->link = ee;
	for(r = reqfirst; r != nil; r = rr){
		rr = r->aux;
		r->aux = nil;
		r->fid->aux = ee;
		ee->ref++;
		e->ref--;
		fulfill(r, e);
		respond(r, nil);
	}
	if(e->ref == 0 && e == evfirst){
		evfirst = ee;
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

static void
usbdopen(Req *req)
{
	if(req->fid->qid.path == Qusbevent){
		qlock(&evlock);
		req->fid->aux = evlast;
		evlast->ref++;
		qunlock(&evlock);
	}
	respond(req, nil);
}

static void
usbddestroyfid(Fid *fid)
{
	Event *e, *ee;

	if(fid->qid.path == Qusbevent){
		qlock(&evlock);
		e = fid->aux;
		if(--e->ref == 0 && e == evfirst){
			while(e->ref == 0 && e != evlast){
				ee = e->link;
				free(e->data);
				free(e);
				e = ee;
			}
			evfirst = e;
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
	Usbdev *u;

	if((d = p->dev) == nil || (u = p->dev->usb) == nil){
		fprint(2, "okay what?\n");
		return -1;
	}
	pushevent(smprint("in id %d vid 0x%.4x did 0x%.4x csp 0x%.8x\n",
		d->id, u->vid, u->did, u->csp));
	closedev(p->dev);
	return 0;
}

void
main(int argc, char **argv)
{
	int fd, i, nd;
	Dir *d;
	
	argc--; argv++;
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
}
