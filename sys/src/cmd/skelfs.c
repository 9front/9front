#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

typedef struct Skel Skel;
struct Skel {
	char name[64];
	char mode;
};

static uvlong 	sessions;
static char	defmode;

enum{
	Qroot,
	Qdir,
	Qskel,
};

#define qtype(x)	(((ulong)x)&0x1f)
#define qsess(x)	((((ulong)x))>>5)
#define mkqid(i,t)	((((ulong)i)<<5)|(t))

static int
step(Fid *f, int way, Qid *res, Dir *dp)
{
	Skel *s;
	int path;
	char *name;
	ulong perm;

	s = f->aux;
	name = s->name;
	perm = 0550|DMDIR;
	path = qtype(f->qid.path) + way;
	if(!name[0] && way > 0)
		return -1;

	if(path < 0)
		goto Root;
	switch(path){
	Root:
	case Qroot:
		name = "/";
		/* fallthrough */
	case Qdir:
		res->type = QTDIR;
		break;
	case Qskel:
		switch(s->mode){
		case 'd':
			res->type = QTDIR;
			break;
		case 'f':
		default:
			res->type = QTFILE;
			perm = 0;
			break;
		}
		break;
	default:
		return -1;
	}
	res->vers = qsess(f->qid.path);
	res->path = mkqid(res->vers, path);
	if(dp){
		dp->mode = perm;
		dp->name = estrdup9p(name);
		dp->uid = estrdup9p("sys");
		dp->gid = estrdup9p("sys");
		dp->qid = *res;
		dp->length = 0;
	}
	return 1;
}

static int
dirgen(int i, Dir *d, void *a)
{
	Fid *f;
	Qid q;

	if(i > 0)
		return -1;
	f = a;
	return step(f, 1, &q, d);
}

static void
fidclunk(Fid *fid)
{
	free(fid->aux);
}

static char*
fsclone(Fid *old, Fid *new, void*)
{
	Skel *s, *s2;

	s = old->aux;
	s2 = emalloc9p(sizeof *s2);

	s2->mode = s->mode;
	utfecpy(s2->name, &s2->name[sizeof s2->name-1], s->name);
	new->aux = s2;
	return nil;
}

static char Enonexist[] = "file does not exist";

static char*
fswalk1(Fid *old, char *name, void*)
{
	Skel *s;

	if(strcmp("..", name) == 0){
		step(old, -1, &old->qid, nil);
		return nil;
	}

	s = old->aux;
	if(!s->name[0] && qtype(old->qid.path) == Qroot && s->mode != 'e'){
		utfecpy(s->name, &s->name[sizeof s->name-1], name);
		old->qid.vers = sessions++;
		old->qid.path = mkqid(old->qid.vers, qtype(old->qid.path));
	} else if(strcmp(name, s->name) != 0)
		return Enonexist;

	if(step(old, 1, &old->qid, nil) < 0)
		return Enonexist;

	return nil;
}

static void
fswalk(Req *r)
{
	walkandclone(r, fswalk1, fsclone, nil);
}

static void
fsattach(Req *r)
{
	Skel s;
	Fid root;
	char *spec;
	Qid *q;

	spec = r->ifcall.aname;
	if(spec && spec[0] != '\0')
		s.mode = spec[0];
	else
		s.mode = defmode;

	q = &r->fid->qid;
	q->vers = sessions++;
	q->path = mkqid(q->vers, Qroot);
	q->type = QTDIR;
	r->ofcall.qid = *q;

	s.name[0] = '\0';
	root.aux = &s;
	respond(r, fsclone(&root, r->fid, nil));
}

static void
fsstat(Req *r)
{
	Qid q;

	if(step(r->fid, 0, &q, &r->d) < 0)
		respond(r, Enonexist);
	respond(r, nil);
}

static void
fsread(Req *r)
{
	dirread9p(r, dirgen, r->fid);
	respond(r, nil);
}

static void
fsopen(Req *r)
{
	r->ofcall.mode = r->ifcall.mode;
	if(r->ifcall.mode != OREAD)
		respond(r, "permission denied");
	else
		respond(r, nil);
}

Srv fs=
{
.attach=	fsattach,
.open=		fsopen,
.read=		fsread,
.stat=		fsstat,
.walk=		fswalk,
.destroyfid=	fidclunk
};

void
usage(void)
{
	fprint(2, "usage: %s [ -Di ] [ -s service ] [ -t mode ] [ mntpt ]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *s, *mode;
	char *mtpt;
	int stdio;

	s = nil;
	stdio = 0;
	defmode = 'f';
	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 's':
		s = EARGF(usage());
		break;
	case 'i':
		stdio = 1;
		break;
	case 't':
		mode = EARGF(usage());
		defmode = mode[0];
		break;
	default:
		usage();
	}ARGEND

	if(argc > 1)
		usage();

	if(stdio == 0){
		if(s != nil && argc == 0)
			mtpt = nil;
		else if(argc)
			mtpt = argv[0];
		else
			mtpt = "/mnt/skel";
		postmountsrv(&fs, s, mtpt, MREPL);
		exits(nil);
	}
	fs.infd = 0;
	fs.outfd = 1;
	srv(&fs);
	exits(nil);
}
