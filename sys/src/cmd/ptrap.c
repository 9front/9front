#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
#include <plumb.h>
#include <regexp.h>

typedef struct IOProc IOProc;
typedef struct PFilter PFilter;
typedef struct FAttr FAttr;
typedef struct PFid PFid;

struct IOProc {
	int id;
	Channel *ch;
	IOProc *next;
};
QLock freellock;
IOProc *freel;

struct PFid {
	char *name;
	PFilter *filter;
	int fd;
	char *msg;
	int msgn, msgp;
};
Qid rootqid = {.type QTDIR};

struct FAttr {
	char *name;
	Reprog *filt;
	int invert;
	FAttr *next;
};

struct PFilter {
	char *name;
	Reprog *filt;
	int invert;
	FAttr *attr;
	PFilter *next;
};
PFilter *filters;

static char *
fname(char *n)
{
	static char *last;
	
	if(last != nil){
		free(last);
		last = nil;
	}
	if(n == nil)
		return "/mnt/plumb";
	last = smprint("/mnt/plumb/%s", n);
	return last;
}

static void theioproc(void *);
static IOProc *
getioproc(void)
{
	IOProc *p;

	qlock(&freellock);
	p = freel;
	if(p != nil)
		freel = freel->next;
	qunlock(&freellock);
	if(p == nil){
		p = emalloc9p(sizeof(IOProc));
		p->ch = chancreate(sizeof(Req*), 0);
		p->id = proccreate(theioproc, p, 4096);
	}
	return p;
}

static void
putioproc(IOProc *p)
{
	qlock(&freellock);
	p->next = freel;
	freel = p;
	qunlock(&freellock);
}

static void
ptrapattach(Req *r)
{
	PFid *pf;
	
	pf = emalloc9p(sizeof(PFid));
	pf->fd = -1;
	r->fid->aux = pf;
	r->ofcall.qid = rootqid;
	r->fid->qid = rootqid;
	respond(r, nil);
}

static char *
ptrapclone(Fid *old, Fid *new)
{
	PFid *pf;

	pf = emalloc9p(sizeof(PFid));
	memcpy(pf, old->aux, sizeof(PFid));
	new->aux = pf;
	return nil;
}

static void
ptrapdestroyfid(Fid *f)
{
	PFid *pf;
	
	pf = f->aux;
	if(pf == nil) return;
	free(pf->name);
	if(pf->fd >= 0)
		close(pf->fd);
	free(pf);
	f->aux = nil;
}

static char *
ptrapwalk1(Fid *fid, char *name, Qid *qid)
{
	PFid *pf;
	Dir *d;
	static char err[ERRMAX];
	
	pf = fid->aux;
	if(pf->name != nil)
		return "phase error";
	d = dirstat(fname(name));
	if(d == nil){
		rerrstr(err, ERRMAX);
		return err;
	}
	pf->name = strdup(name);
	fid->qid = d->qid;
	*qid = d->qid;
	free(d);
	return nil;	
}

static void
ptrapopen(Req *r)
{
	PFid* pf;
	PFilter *f;
	
	pf = r->fid->aux;
	pf->fd = open(fname(pf->name), r->ifcall.mode);
	if(pf->fd < 0){
		responderror(r);
		return;
	}
	if(pf->name == nil){
		respond(r, nil);
		return;
	}
	for(f = filters; f != nil; f = f->next)
		if(strcmp(f->name, pf->name) == 0)
			break;
	pf->filter = f;
	respond(r, nil);
}

static int
filter(PFilter *f, Plumbmsg *pm)
{
	FAttr *a;
	char *value;

	if(!(regexec(f->filt, pm->data, nil, 0) ^ f->invert))
		return 0;
	for(a = f->attr; a; a = a->next){
		value = plumblookup(pm->attr, a->name);
		if(value == nil)
			return 0;
		if(!(regexec(a->filt, value, nil, 0) ^ f->attr->invert))
			return 0;
	}
	return 1;
}

static int
filterread(Req *r, PFid *pf)
{
	int rc, len, more;
	char *buf;
	Plumbmsg *pm;
	PFilter *f;
	
	f = pf->filter;
	for(;;){
		if(pf->msg != nil){
			rc = r->ifcall.count;
			if(pf->msgp + rc >= pf->msgn)
				rc = pf->msgn - pf->msgp;
			r->ofcall.count = rc;
			memmove(r->ofcall.data, pf->msg + pf->msgp, rc);
			pf->msgp += rc;
			if(pf->msgp >= pf->msgn){
				free(pf->msg);
				pf->msg = nil;
			}
			return 0;
		}
		buf = emalloc9p(4096);
		rc = read(pf->fd, buf, 4096);
		if(rc < 0) goto err;
		len = rc;
		while(pm = plumbunpackpartial(buf, len, &more), pm == nil){
			if(more == 0) goto err;
			buf = erealloc9p(buf, len + more);
			rc = readn(pf->fd, buf + len, more);
			if(rc < 0) goto err;
			len += rc;
		}
		free(buf);
		if(filter(f, pm)){
			pf->msg = plumbpack(pm, &pf->msgn);
			pf->msgp = 0;
		}
		plumbfree(pm);
	}
err:
	free(buf);
	return -1;
}

static void
theioproc(void *iopp)
{
	Req *r;
	PFid *pf;
	IOProc *iop;
	char *buf;
	int fd, rc;
	
	rfork(RFNOTEG);
	
	buf = smprint("/proc/%d/ctl", getpid());
	fd = open(buf, OWRITE);
	free(buf);
	
	iop = iopp;
	for(;;){
		if(fd >= 0)
			write(fd, "nointerrupt", 11);
		r = recvp(iop->ch);
		r->aux = iop;
		pf = r->fid->aux;
		switch(r->ifcall.type){
		case Tread:
			if(!pf->filter){
				rc = pread(pf->fd, r->ofcall.data, r->ifcall.count, r->ifcall.offset);
				if(rc < 0){
					responderror(r);
					break;
				}
				r->ofcall.count = rc;
				respond(r, nil);
				break;
			}
			if(filterread(r, pf) < 0)
				responderror(r);
			else
				respond(r, nil);
			break;
		case Twrite:
			rc = pwrite(pf->fd, r->ifcall.data, r->ifcall.count, r->ifcall.offset);
			if(rc < 0)
				responderror(r);
			else{
				r->ofcall.count = rc;
				respond(r, nil);
			}
			break;
		}
		putioproc(iop);
	}
}

static void
ptrapread(Req *r)
{
	IOProc *iop;
	
	iop = getioproc();
	send(iop->ch, &r);
}

static void
ptrapwrite(Req *r)
{
	IOProc *iop;
	
	iop = getioproc();
	send(iop->ch, &r);
}

static void
ptrapstat(Req *r)
{
	PFid *pf;
	Dir *d;
	
	pf = r->fid->aux;
	if(pf->fd >= 0)
		d = dirfstat(pf->fd);
	else
		d = dirstat(fname(pf->name));
	if(d == nil){
		responderror(r);
		return;
	}
	memmove(&r->d, d, sizeof(Dir));
	r->d.name = strdup(d->name);
	r->d.uid = strdup(d->uid);
	r->d.muid = strdup(d->muid);
	r->d.gid = strdup(d->gid);
	free(d);
	respond(r, nil);
}

static void
ptrapwstat(Req *r)
{
	PFid *pf;
	int rc;
	
	pf = r->fid->aux;
	if(pf->fd >= 0)
		rc = dirfwstat(pf->fd, &r->d);
	else
		rc = dirwstat(fname(pf->name), &r->d);
	if(rc < 0)
		responderror(r);
	else
		respond(r, nil);
}

static void
ptrapflush(Req *r)
{
	if(r->oldreq->aux != nil)
		threadint(((IOProc*)r->oldreq->aux)->id);
	respond(r, nil);
}

Srv ptrapsrv = {
	.attach = ptrapattach,
	.clone = ptrapclone,
	.destroyfid = ptrapdestroyfid,
	.walk1 = ptrapwalk1,
	.open = ptrapopen,
	.read = ptrapread,
	.write = ptrapwrite,
	.stat = ptrapstat,
	.wstat = ptrapwstat,
	.flush = ptrapflush,
};

void
usage(void)
{
	fprint(2, "usage: %s port regex [ +attr regex ... ] ...\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	PFilter *f;
	FAttr *fa;
	char *p;
	int i;

	ARGBEGIN{
	default: usage();
	}ARGEND;

	if(argc == 0 || argc % 2) usage();
	for(i = 0; i+1 < argc;){
		p = argv[i];
		f = emalloc9p(sizeof(PFilter));
		f->name = estrdup9p(p);
		p = argv[i+1];
		if(p[0] == '!'){
			p++;
			f->invert = 1;
		}
		if((f->filt = regcomp(p)) == nil)
			sysfatal("regcomp: %r");
		f->next = filters;
		filters = f;
		for(i += 2; p = argv[i], i+1 < argc && p[0] == '+'; i += 2){
			p++;
			fa = emalloc9p(sizeof(FAttr));
			fa->name = estrdup9p(p);
			p = argv[i+1];
			if(p[0] == '!'){
				p++;
				fa->invert = 1;
			}
			if((fa->filt = regcomp(p)) == nil)
				sysfatal("regcomp: %r");
			fa->next = f->attr;
			f->attr = fa;
		}
	}
	threadpostmountsrv(&ptrapsrv, nil, "/mnt/plumb", MREPL | MCREATE);

	threadexits(nil);
}
