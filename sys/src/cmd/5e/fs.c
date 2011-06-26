#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include "dat.h"
#include "fns.h"

static char *luser;
extern int pflag;

enum
{
	Qdir,
	Qtrace,
	Qargs,
	Qctl,
	Qfd,
	Qfpregs,
	Qkregs,
	Qmem,
	Qnote,
	Qnoteid,
	Qnotepg,
	Qns,
	Qproc,
	Qregs,
	Qsegment,
	Qstatus,
	Qtext,
	Qwait,
	Qprofile,
	Qsyscall,
	NQid,
};

typedef struct Aux Aux;
typedef struct Dirtab Dirtab;
struct Dirtab {
	char name[28];
	Qid qid;
	vlong length;
	long perm;
};
struct Aux {
	Process *p;
	int fd;
	Dirtab *d;
};

Dirtab procdir[] =
{
	"args",		{Qargs},	0,			0660,
	"ctl",		{Qctl},		0,			0600,
	"fd",		{Qfd},		0,			0444,
	"fpregs",	{Qfpregs},	0,			0400,
	"kregs",	{Qkregs},	18 * 4,			0400,
	"mem",		{Qmem},		0,			0400,
	"note",		{Qnote},	0,			0000,
	"noteid",	{Qnoteid},	0,			0664,
	"notepg",	{Qnotepg},	0,			0000,
	"ns",		{Qns},		0,			0444,
	"proc",		{Qproc},	0,			0400,
	"regs",		{Qregs},	18 * 4,			0400,
	"segment",	{Qsegment},	0,			0444,
	"status",	{Qstatus},	176,			0444,
	"text",		{Qtext},	0,			0400,
	"wait",		{Qwait},	0,			0400,
	"profile",	{Qprofile},	0,			0400,
	"syscall",	{Qsyscall},	0,			0400,	
	"",		{0},		0,			0,
};

static char *
readin(int pid, char *file)
{
	char *name, *buf;
	int fd, rc;
	
	name = smprint("#p/%d/%s", pid, file);
	fd = open(name, OREAD);
	if(fd < 0)
		return nil;
	buf = malloc(1024);
	rc = read(fd, buf, 1023);
	if(rc < 0)
		return nil;
	buf[rc] = 0;
	free(name);
	close(fd);
	return buf;
}

static int
calcmem(Process *p)
{
	int i, r;
	
	r = 0;
	for(i = 0; i < SEGNUM; i++) {
		if(i == SEGSTACK)
			continue;
		if(p->S[i] == nil)
			continue;
		r += p->S[i]->size;
	}
	r = (r + 1023) / 1024;
	return r;
}

static int
copymem(Process *p, char *buf, u32int addr, int len)
{
	int i, n, r;

	r = len;
	while(len > 0) {
		for(i = 0; i < SEGNUM; i++) {
			if(p->S[i] == nil)
				continue;
			if(p->S[i]->start <= addr && p->S[i]->start + p->S[i]->size > addr)
				break;
		}
		if(i == SEGNUM) {
			werrstr("bad arg in syscall");
			return -1;
		}
		n = p->S[i]->start + p->S[i]->size - addr;
		if(n > len)
			n = len;
		memcpy(buf, (char*)p->S[i]->data + addr - p->S[i]->start, n);
		len -= n;
		buf += n;
	}
	return r;
}

static char *
segments(Process *p)
{
	char *r, *s;
	static char *names[] = {
		[SEGTEXT] "Text",
		[SEGSTACK] "Stack",
		[SEGDATA] "Data",
		[SEGBSS] "Bss",
	};
	int i;
	
	r = emalloc(1024);
	s = r;
	for(i = 0; i < SEGNUM; i++) {
		if(p->S[i] == nil)
			continue;
		s += sprint(s, "%-7s%c  %.8ux %.8ux %4ld\n", names[i], i == SEGTEXT ? 'R' : ' ', p->S[i]->start, p->S[i]->start + p->S[i]->size, p->S[i]->dref->ref);
	}
	return r;
}

static void
procattach(Req *req)
{
	req->fid->qid = (Qid) {0, 0, 0x80};
	req->fid->aux = emallocz(sizeof(Aux));
	((Aux *) req->fid->aux)->fd = -1;
	req->ofcall.qid = req->fid->qid;
	respond(req, nil);
}

static char *
procwalk(Fid *fid, char *name, Qid *qid)
{
	int pid;
	char buf[20];
	Dirtab *d;
	Aux *a;
	
	a = fid->aux;
	if(fid->qid.path == 0) {
		pid = atoi(name);
		sprint(buf, "%d", pid);
		if(strcmp(buf, name) != 0 || (a->p = findproc(pid)) == nil)
			return "file does not exist";
		*qid = (Qid) {pid * NQid, 0, 0x80};
		fid->qid = *qid;
		return nil;
	}
	if((fid->qid.path % NQid) == 0) {
		for(d = procdir; d->name[0] != 0; d++)
			if(strcmp(d->name, name) == 0)
				break;
		if(d->name[0] == 0)
			return "file does not exist";
		*qid = d->qid;
		qid->path += fid->qid.path;
		fid->qid = *qid;
		a->d = d;
		return nil;
	}
	return "the front fell off";
}

static char *
procclone(Fid *old, Fid *new)
{
	new->aux = emallocz(sizeof(Aux));
	memcpy(new->aux, old->aux, sizeof(Aux));
	return nil;
}

static void
procopen(Req *req)
{
	Aux *a;
	
	a = req->fid->aux;
	switch((int)(req->fid->qid.path % NQid)) {
	case Qtext:
		a->fd = open((char*)(a->p->path + 1), OREAD);
		break;
	default:
		respond(req, nil);
		return;
	}
	if(a->fd < 0)
		responderror(req);
	else
		respond(req, nil);
}

static void
procdestroyfid(Fid *fid)
{
	Aux *a;
	
	a = fid->aux;
	free(a);
}

static int
procgen(int n, Dir *d, void *)
{
	int i;
	Process *p;
	
	p = &plist;
	for(i = 0;; i++) {
		p = p->next;
		if(p == &plist)
			return -1;
		if(i == n)
			break;
	}
	d->uid = estrdup9p(luser);
	d->gid = estrdup9p(luser);
	d->muid = estrdup9p(luser);
	d->name = smprint("%d", p->pid);
	d->mode = DMDIR | 0555;
	d->qid = (Qid) {p->pid * NQid, 0, 0x80};
	return 0;
}

static int
procsubgen(int n, Dir *d, void *)
{
	Dirtab *di;
	
	if(n >= nelem(procdir) - 1)
		return -1;
	
	di = procdir + n;
	d->uid = estrdup9p(luser);
	d->gid = estrdup9p(luser);
	d->muid = estrdup9p(luser);
	d->name = estrdup9p(di->name);
	d->mode = di->perm;
	d->length = di->length;
	d->qid = di->qid;
	return 0;
}

static void
procread(Req *req)
{
	Aux *a;
	Process *p;
	char *buf;
	int rc;

	a = req->fid->aux;
	if(a == nil) {
		respond(req, "the front fell off");
		return;
	}
	if(req->fid->qid.path == 0) {
		dirread9p(req, procgen, nil);
		respond(req, nil);
		return;
	}
	p = a->p;
	switch((int)(req->fid->qid.path % NQid)) {
	case Qdir:
		dirread9p(req, procsubgen, nil);
		respond(req, nil);
		break;
	case Qstatus:
		buf = readin(p->pid, "status");
		if(buf == nil)
			responderror(req);
		else {
			memset(buf, ' ', 27);
			memcpy(buf, p->name, strlen(p->name));
			sprint(buf + 149, "%d", calcmem(p));
			buf[strlen(buf)] = ' ';
			readstr(req, buf);
			free(buf);
			respond(req, nil);
		}
		break;
	case Qsegment:
		buf = segments(p);
		readstr(req, buf);
		free(buf);
		respond(req, nil);
		break;
	case Qtext:
		rc = pread(a->fd, req->ofcall.data, req->ifcall.count, req->ifcall.offset);
		if(rc >= 0) {
			req->ofcall.count = rc;
			respond(req, nil);
		} else
			responderror(req);
		break;
	case Qmem:
		rc = copymem(p, req->ofcall.data, req->ifcall.offset, req->ifcall.count);
		if(rc >= 0) {
			req->ofcall.count = rc;
			respond(req, nil);
		} else
			responderror(req);
		break;
	case Qregs:
		buf = emallocz(18 * 4);
		memcpy(buf, p->R, 15 * 4);
		memcpy(buf + 16 * 4, &p->CPSR, 4);
		memcpy(buf + 17 * 4, p->R + 15, 4);
		readbuf(req, buf, 18 * 4);
		free(buf);
		respond(req, nil);
		break;
	default:
		respond(req, "the front fell off");
	}
}

static void
writeto(Req *req, char *fmt, ...)
{
	int fd, rc;
	va_list va;
	char *file;
	
	va_start(va, fmt);
	file = vsmprint(fmt, va);
	va_end(va);
	fd = open(file, OWRITE);
	free(file);
	if(fd < 0) {
		responderror(req);
		return;
	}
	rc = write(fd, req->ifcall.data, req->ifcall.count);
	req->ofcall.count = rc;
	if(rc < req->ifcall.count)
		responderror(req);
	else
		respond(req, nil);
	close(fd);
}

static void
procwrite(Req *req)
{
	switch((int)(req->fid->qid.path % NQid)) {
	case Qnote:
		writeto(req, "#p/%lld/note", req->fid->qid.path / NQid);
		break;
	default:
		respond(req, "the front fell off");
	}
}

static void
procstat(Req *req)
{
	Aux *a;
	Dir *d;
	
	d = &req->d;
	a = req->fid->aux;
	if(a == nil) {
		respond(req, "the front fell off");
		return;
	}
	d->qid = req->fid->qid;
	if(a->d != nil) {
		d->mode = a->d->perm;
		d->length = a->d->length;
		d->name = strdup(a->d->name);
	} else {
		d->mode = 0555 | DMDIR;
		if(d->qid.path != 0)
			d->name = smprint("%lld", d->qid.path / NQid);
	}
	d->uid = strdup(luser);
	d->gid = strdup(luser);
	d->muid = strdup(luser);
	respond(req, nil);
}

static Srv procsrv = {
	.attach = procattach,
	.walk1 = procwalk,
	.clone = procclone,
	.destroyfid = procdestroyfid,
	.open = procopen,
	.read = procread,
	.stat = procstat,
};

void
initfs(char *name, char *mtpt)
{
	luser = getuser();
	remove("/srv/armproc");
	postmountsrv(&procsrv, name, mtpt, MREPL);
}
