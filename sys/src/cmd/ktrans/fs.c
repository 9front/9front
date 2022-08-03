#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include "hash.h"
#include "ktrans.h"

static Channel 	*globalkbd;
static char 	*user;

char*
parsekbd(Channel *out, char *buf, int n)
{
	char *p, *e;
	Msg msg;

	for(p = buf; p < buf+n;){
		msg.code = p[0];
		p++;
		switch(msg.code){
		case 'c': case 'k': case 'K':
			break;
		default:
			return "malformed kbd message";
		}
		e = utfecpy(msg.buf, msg.buf + sizeof msg.buf, p);
		if(e == msg.buf)
			return "short command";
		p += e - msg.buf;
		p++;
		if(send(out, &msg) == -1)
			return nil;
	}
	return nil;
}

void
kbdproc(void *a)
{
	char *s;
	int fd, n;
	char buf[128];

	s = a;
	fd = open(s, OREAD);
	if(fd < 0){
		fprint(2, "could not open file %s: %r", s);
		chanclose(globalkbd);
		return;
	}
	for(;;){
		n = read(fd, buf, sizeof buf);
		if(n < 3){
			continue;
		}
		parsekbd(globalkbd, buf, n);
	}
}

Trans*
spawntrans(int global)
{
	Trans *t;

	t = mallocz(sizeof *t, 1);
	if(global)
		t->input = globalkbd;
	else
		t->input = chancreate(sizeof(Msg), 0);
	t->output = chancreate(sizeof(Msg), 0);
	t->dict = chancreate(sizeof(Msg), 0);
	t->done = chancreate(1, 0);
	t->lang = chancreate(sizeof(char*), 0);
	proccreate(keyproc, t, mainstacksize);
	return t;
}

void
closetrans(Trans *t)
{
	chanclose(t->input);
	chanclose(t->output);
	chanclose(t->dict);

	/* wait for threads to exit */
	recv(t->done, nil);
	recv(t->done, nil);

	chanfree(t->done);
	chanfree(t->input);
	chanfree(t->output);
	chanfree(t->dict);
	free(t);
}

enum{
	Qroot,
	Qkbd,
	Qkbdin,
	Qlang,
};

Dir dirtab[] = {
	{.qid={Qroot, 0, QTDIR}, .mode=0555, .name="/"},
	{.qid={Qkbd, 0, QTFILE}, .mode=0600, .name="kbd"},
	{.qid={Qkbdin, 0, QTFILE}, .mode=0200, .name="kbdin"},
	{.qid={Qlang, 0, QTFILE}, .mode=0600, .name="lang"},
};

static int
dirgen(int n, Dir *dir, void*)
{
	n++;
	if(n >= nelem(dirtab))
		return -1;

	*dir = dirtab[n];
	dir->name = estrdup9p(dir->name);
	dir->uid = estrdup9p(user);
	dir->gid = estrdup9p(user);
	dir->muid = estrdup9p(user);
	return 0;
}

typedef struct Aux Aux;
struct Aux {
	Ref;
	Reqqueue *q;
	Trans	 *t;
};

static void
fsattach(Req *r)
{
	Aux *aux;
	Trans *t;
	char *aname;

	/*
	 * Each attach allocates a new "keyboard".
	 * The global attach argument denotes to
	 * use /dev/kbd as the source of keyboard input.
	 *
	 * Sessions include one translation
	 * process, and one read queue. Since
	 * it is common for clients to constantly be
	 * blocked on the kbd file, we need to assign it to
	 * it's own process so we can service other requests
	 * in the meantime.
	 */

	aname = r->ifcall.aname;
	if(aname != nil && strcmp(aname, "global") == 0)
		t = spawntrans(1);
	else
		t = spawntrans(0);

	aux = mallocz(sizeof *aux, 1);
	aux->t = t;
	aux->q = reqqueuecreate();
	incref(aux);

	r->fid->aux = aux;
	r->ofcall.qid = dirtab[0].qid;
	r->fid->qid = dirtab[0].qid;
	respond(r, nil);
}

static void
fsopen(Req *r)
{
	respond(r, nil);
}

static void
fskbd(Req *r)
{
	Aux *aux;
	Msg m;
	char *p;
	char buf[1+128], *bp;
	Rune rn;

	aux = r->fid->aux;
	if(recv(aux->t->output, &m) == -1){
		respond(r, "closing");
		return;
	}
	if(m.code != 'c'){
		bp = seprint(buf, buf + sizeof buf, "%c%s", m.code, m.buf);
		goto Send;
	}
	p = m.buf;
	bp = buf;
	for(;bp < buf + sizeof buf;){
		p += chartorune(&rn, p);
		if(rn == Runeerror || rn == '\0')
			break;
		bp = seprint(bp, buf + sizeof buf, "c%C", rn);
		bp++;
	}
	if(bp >= buf + sizeof buf){
		while(*bp-- != '\0')
			;
		bp++;
	}

Send:
	r->ifcall.offset = 0;
	readbuf(r, buf, (bp-buf)+1);
	respond(r, nil);
}

static void
fsread(Req *r)
{
	Aux *aux;
	Msg m;
	char *p;

	aux = r->fid->aux;
	switch(r->fid->qid.path){
	case Qroot:
		dirread9p(r, dirgen, nil);
		respond(r, nil);
		break;
	case Qkbd:
		
		reqqueuepush(aux->q, r, fskbd);
		break;
	case Qlang:
		m.code = 'q';
		m.buf[0] = '\0';
		if(send(aux->t->input, &m) == -1){
			respond(r, "closing");
			break;
		}
		if(recv(aux->t->lang, &p) == -1){
			respond(r, "closing");
			break;
		}
		snprint(m.buf, sizeof m.buf, "%s\n", p);
		readstr(r, m.buf);
		respond(r, nil);
		break;
	default:
		respond(r, "bad op");
		break;
	}
}

static void
fswrite(Req *r)
{
	Aux *aux;
	int n, lang;
	char *err, *p;
	Msg m;

	aux = r->fid->aux;
	n = r->ifcall.count;
	switch(r->fid->qid.path){
	case Qkbdin:
		if(n < 3){
			respond(r, "short write");
			return;
		}
		err = parsekbd(aux->t->input, r->ifcall.data, n);
		if(err != nil){
			respond(r, err);
			return;
		}
		break;
	case Qlang:
		if(n >= sizeof m.buf){
			respond(r, "large write");
			return;
		}
		memmove(m.buf, r->ifcall.data, n);
		m.buf[n] = '\0';
		p = strchr(m.buf, '\n');
		if(p != nil)
			*p = '\0';
		lang = parselang(m.buf);
		if(lang < 0){
			respond(r, "unkonwn lang");
			return;
		}
		m.buf[0] = lang;
		m.buf[1] = '\0';
		m.code = 'c';
		send(aux->t->input, &m);
	}
	r->ofcall.count = n;
	respond(r, nil);
}

static void
fsstat(Req *r)
{
	if(dirgen(r->fid->qid.path - 1, &r->d, nil) == -1)
		respond(r, "invalid fid");
	else
		respond(r, nil);
	
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	int i;

	if(fid->qid.path != Qroot)
		return "walk from non root";

	for(i = 0; i  < nelem(dirtab); i++)
		if(strcmp(name, dirtab[i].name) == 0){
			*qid = dirtab[i].qid;
			break;
		}

	if(i == nelem(dirtab))
		return "file does not exist";

	fid->qid = *qid;
	return nil;
}

static char*
fsclone(Fid *oldfid, Fid *newfid)
{
	Aux *aux;

	aux = oldfid->aux;
	incref(aux);
	newfid->aux = aux;

	return nil;
}

static void
fidclunk(Fid *fid)
{
	Aux *aux;

	aux = fid->aux;
	if(decref(aux) != 0)
		return;

	closetrans(aux->t);
	reqqueuefree(aux->q);
}


static Srv fs = {
	.attach=fsattach,
	.open=fsopen,
	.read=fsread,
	.write=fswrite,
	.stat=fsstat,

	.walk1=fswalk1,
	.clone=fsclone,
	.destroyfid=fidclunk,
};

void
launchfs(char *srv, char *mnt, char *kbd)
{
	int fd;
	char buf[128];

	user = getenv("user");
	if(user == nil)
		user = "glenda";
	if(kbd != nil){
		globalkbd = chancreate(sizeof(Msg), 0);
		proccreate(kbdproc, kbd, mainstacksize);
	}

	fd = threadpostsrv(&fs, srv);
	if(fd < 0)
		sysfatal("postsrv %r");

	if(kbd != nil){
		if(mount(fd, -1, mnt, MREPL, "global") < 0)
			sysfatal("mount %r");

		snprint(buf, sizeof buf, "%s/kbd", mnt);
		if(bind(buf, "/dev/kbd", MREPL) < 0)
			sysfatal("bind %r");

		snprint(buf, sizeof buf, "%s/kbdin", mnt);
		if(bind(buf, "/dev/kbdin", MREPL) < 0)
			sysfatal("bind %r");
	} else
		if(mount(fd, -1, mnt, MREPL, "") < 0)
			sysfatal("mount %r");
}
