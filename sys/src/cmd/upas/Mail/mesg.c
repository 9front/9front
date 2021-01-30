#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <regexp.h>

#include "mail.h"

#define Datefmt		"?WWW, ?MMM ?DD hh:mm:ss ?Z YYYY"

typedef struct Fn	Fn;

struct Fn {
	char *name;
	void (*fn)(Mesg *, char **, int);
};

void
mesgclear(Mesg *m)
{
	int i;

	for(i = 0; i < m->nparts; i++)
		mesgclear(m->parts[i]);
	free(m->name);
	free(m->from);
	free(m->to);
	free(m->cc);
	free(m->replyto);
	free(m->date);
	free(m->subject);
	free(m->type);
	free(m->disposition);
	free(m->messageid);
	free(m->filename);
	free(m->digest);
	free(m->mflags);
	free(m->fromcolon);
}

void
mesgfree(Mesg *m)
{
	if(m == nil)
		return;
	mesgclear(m);
	free(m);
}

static char*
line(char *data, char **pp, int z)
{
	char *p, *q;

	for(p=data; *p!='\0' && *p!='\n'; p++)
		;
	if(*p == '\n')
		*pp = p+1;
	else
		*pp = p;
	if(z && p == data)
		return nil;
	q = emalloc(p-data + 1);
	memmove(q, data, p-data);
	return q;
}

static char*
fc(Mesg *m, char *s)
{
	char *r;

	if(s != nil && strlen(m->from) != 0){
		r = smprint("%s <%s>", s, m->from);
		free(s);
		return r;
	}
	if(m->from != nil)
		return estrdup(m->from);
	if(s != nil)
		return s;
	return estrdup("??");
}

Mesg*
mesgload(char *name)
{
	char *info, *p;
	int ninfo;
	Mesg *m;
	Tm tm;

	m = emalloc(sizeof(Mesg));
	m->name = estrjoin(name, "/", nil);
	if((info = rslurp(m, "info", &ninfo)) == nil){
		free(m->name);
		free(m);
		return nil;
	}

	p = info;
	m->from = line(p, &p, 0);
	m->to = line(p, &p, 0);
	m->cc = line(p, &p, 0);
	m->replyto = line(p, &p, 1);
	m->date = line(p, &p, 0);
	m->subject = line(p, &p, 0);
	m->type = line(p, &p, 1);
	m->disposition = line(p, &p, 1);
	m->filename = line(p, &p, 1);
	m->digest = line(p, &p, 1);
	/* m->bcc = */ free(line(p, &p, 1));
	m->inreplyto = line(p, &p, 1);
	/* m->date = */ free(line(p, &p, 1));
	/* m->sender = */ free(line(p, &p, 1));
	m->messageid = line(p, &p, 0);
	/* m->lines = */ free(line(p, &p, 1));
	/* m->size = */ free(line(p, &p, 1));
	m->mflags = line(p, &p, 0);
	/* m->fileid = */ free(line(p, &p, 1));
	m->fromcolon = fc(m, line(p, &p, 1));
	free(info);

	m->flags = 0;
	if(strchr(m->mflags, 'd')) m->flags |= Fdel;
	if(strchr(m->mflags, 's')) m->flags |= Fseen;
	if(strchr(m->mflags, 'a')) m->flags |= Fresp;

	m->time = time(nil);
	if(tmparse(&tm, Datefmt, m->date, nil, nil) != nil)
		m->time = tmnorm(&tm);
	m->hash = 0;
	if(m->messageid != nil)
		m->hash = strhash(m->messageid);
	return m;
}

static Mesg*
readparts(Mesg *m)
{
	char *dpath, *apath;
	int n, i, dfd;
	Mesg *a, *sub;
	Dir *d;

	if(m->body != nil)
		return m->body;

	dpath = estrjoin(mbox.path, m->name, nil);
	dfd = open(dpath, OREAD);
	free(dpath);
	if(dfd == -1)
		return m;

	n = dirreadall(dfd, &d);
	close(dfd);
	if(n == -1)
		sysfatal("%s read: %r", mbox.path);

	m->body = nil;
	for(i = 0; i < n; i++){
		if(d[i].qid.type != QTDIR)
			continue;

		apath = estrjoin(m->name, d[i].name, nil);
		a = mesgload(apath);
		free(apath);
		if(a == nil)
			continue;
		if(strncmp(a->type, "multipart/", strlen("multipart/")) == 0){
			sub = readparts(a);
			if(sub != a)
				m->body = sub;
			continue;
		} 
		if(m->nparts >= m->xparts)
			m->parts = erealloc(m->parts, (2 + m->nparts*2)*sizeof(Mesg*));
		m->parts[m->nparts++] = a;
		if(m->body == nil && strcmp(a->type, "text/plain") == 0)
			m->body = a;
		else if(m->body == nil && strcmp(a->type, "text/html") == 0)
			m->body = a;
	}
	free(d);
	if(m->body == nil)
		m->body = m;
	return m->body;
}

static void
execfmt(void *pm)
{
	Mesg *m;

	m = pm;
	rfork(RFFDG);
	dup(m->fd[1], 1);
	close(m->fd[0]);
	close(m->fd[1]);
	procexecl(m->sync, "/bin/htmlfmt", "htmlfmt", "-a", "-cutf-8", m->path, nil);
}

static int
htmlfmt(Mesg *m, char *path)
{
	if(pipe(m->fd) == -1)
		sysfatal("pipe: %r");
	m->sync = chancreate(sizeof(ulong), 0);
	m->path = path;
	procrfork(execfmt, m, Stack, RFNOTEG);
	recvul(m->sync);
	chanfree(m->sync);
	close(m->fd[1]);
	return m->fd[0];
}

static void
copy(Biobuf *wfd, Biobuf *rfd)
{
	char *buf;
	int n;

	buf = emalloc(Bufsz);
	while(1){
		n = Bread(rfd, buf, Bufsz);
		if(n <= 0)
			break;
		if(Bwrite(wfd, buf, n) != n)
			break;
	}
	free(buf);
}

static int
mesgshow(Mesg *m)
{
	char *path, *home, *name, *suff;
	Biobuf *rfd, *wfd;
	Mesg *a;
	int i;

	if((wfd = bwinopen(m, "body", OWRITE)) == nil)
		return -1;
	if(m->parent != nil || m->nchild != 0) {
		Bprint(wfd, "Thread:");
		if(m->parent && !(m->parent->state & Sdummy))
			Bprint(wfd, " ↑ %s", m->parent->name);
		for(i = 0; i < m->nchild; i++)
			Bprint(wfd, " ↓ %s", m->child[i]->name);
		Bprint(wfd, "\n");
	}
	Bprint(wfd, "From: %s\n", m->fromcolon);
	Bprint(wfd, "To:   %s\n", m->to);
	Bprint(wfd, "Date: %s\n", m->date);
	Bprint(wfd, "Subject: %s\n\n", m->subject);

	rfd = mesgopenbody(m);
	if(rfd != nil){
		copy(wfd, rfd);
		Bterm(rfd);
	}

	home = getenv("home");
	if(m->nparts != 0)
		Bprint(wfd, "\n");
	for(i = 0; i < m->nparts; i++){
		a = m->parts[i];
		name = a->name;
		if(strncmp(a->name, m->name, strlen(m->name)) == 0)
			name += strlen(m->name);
		if(a->disposition != nil
		&& strcmp(a->disposition, "inline") == 0
		&& strcmp(a->type, "text/plain") == 0){
			if(a == m || a == m->body)
				continue;
			Bprint(wfd, "\n===> %s (%s)\n", name, a->type);
			path = estrjoin(mbox.path, a->name, "body", nil);
			if((rfd = Bopen(path, OREAD)) != nil){
				copy(wfd, rfd);
				Bterm(rfd);
			}
			free(path);
			continue;
		}
		Bprint(wfd, "\n===> %s (%s)\n", name, a->type);
		name = a->filename;
		if(name == nil)
			name = "body";
		if((suff = strchr(name, '.')) == nil)
			suff = "";
		Bprint(wfd, "\tcp %s%sbody%s %s/%s\n", mbox.path, a->name, suff, home, name);
		continue;
	}
	Bterm(wfd);
	free(home);
	fprint(m->ctl, "clean\n");
	return 0;
}

static void
reply(Mesg *m, char **f, int nf)
{
	if(nf >= 1 &&  strcmp(f[0], "all") != 0)
		compose(m->replyto, m, 1);
	else
		compose(m->replyto, m, 0);
}

static void
delmesg(Mesg *m, char **, int nf)
{
	if(nf != 0){
		fprint(2, "Delmesg: too many args\n");
		return;
	}
	m->flags |= Ftodel;
	m->quitting = 1;
	mbredraw(m, 0, 0);
}

static void
markone(Mesg *m, char **f, int nf)
{
	int add, flg, fd;
	char *path;

	if(nf != 1){
		fprint(2, "Mark: invalid arguments");
		return;
	}

	if((flg = mesgflagparse(f[0], &add)) == -1){
		fprint(2, "Mark: invalid flags %s\n", f[0]);
		return;
	}
	if(add)
		m->flags |= flg;
	else
		m->flags &= ~flg;
	if(strlen(f[0]) != 0){
		path = estrjoin(mbox.path, "/", m->name, "/flags", nil);
		if((fd = open(path, OWRITE)) != -1){
			fprint(fd, f[0]);
			close(fd);
		}
		free(path);
	}
	mbredraw(m, 0, 0);
}


static void
mesgquit(Mesg *m, char **, int)
{
	if(fprint(m->ctl, "del\n") == -1)
		return;
	m->quitting = 1;
	m->open = 0;
}

static Fn mesgfn[] = {
	{"Reply",	reply},
	{"Delmesg",	delmesg},
	{"Del", 	mesgquit},
	{"Mark",	markone},
#ifdef NOTYET
	{"Save",	nil},
#endif
	{nil}
};

static void
mesgmain(void *mp)
{
	char *path, *f[32];
	Event ev;
	Mesg *m, **pm;
	Fn *p;
	int nf;

	m = mp;
	m->quitting = 0;
	m->qnext = mbox.openmesg;
	mbox.openmesg = m;

	path = estrjoin(mbox.path, m->name, nil);
	wininit(m, path);
	free(path);

	wintagwrite(m, "Reply all Delmesg Save  ");
	mesgshow(m);
	fprint(m->ctl, "clean\n");
	mbox.nopen++;
	while(!m->quitting){
		if(winevent(m, &ev) != 'M')
			continue;
		if(strcmp(ev.text, "Del") == 0)
			break;
		switch(ev.type){
		case 'l':
		case 'L':
			if(matchmesg(m, ev.text))
				mesgopen(ev.text, nil);
			else
				winreturn(m, &ev);
			break;
		case 'x':
		case 'X':
			if((nf = tokenize(ev.text, f, nelem(f))) == 0)
				continue;
			for(p = mesgfn; p->fn != nil; p++){
				if(strcmp(p->name, f[0]) == 0 && p->fn != nil){
					p->fn(m, &f[1], nf - 1);
					break;
				}
			}
			if(p->fn == nil)
				winreturn(m, &ev);
			break;
		}
	}
	for(pm = &mbox.openmesg; *pm != nil; pm = &(*pm)->qnext)
		if(*pm == m){
			*pm = m->qnext;
			break;
		}
	mbox.nopen--;
	m->qnext = nil;
	m->state &= ~Sopen;
	winclose(m);
	threadexits(nil);
}

int
mesgflagparse(char *fstr, int *add)
{
	int flg;

	flg = 0;
	*add = (*fstr == '+');
	if(*fstr == '-' || *fstr == '+')
		fstr++;
	for(; *fstr; fstr++){
		switch(*fstr){
		case 'a':
			flg |= Fresp;
			break;
		case 's':
			flg |= Fseen;
			break;
		case 'D':
			flg |= Ftodel;
			memcpy(fstr, fstr +1, strlen(fstr));
			break;
		default:
			fprint(2, "unknown flag %c", *fstr);
			return -1;
		}
	}
	return flg;
}

void
mesgpath2name(char *buf, int nbuf, char *name)
{
	char *p, *e;
	int n;

	n = strlen(mbox.path);
	if(strncmp(name, mbox.path, n) == 0)
		e = strecpy(buf, buf+nbuf-2, name + n);
	else
		e = strecpy(buf, buf+nbuf-2, name);
	if((p = strchr(buf, '/')) == nil)
		p = e;
	p[0] = '/';
	p[1] = 0;
}

int
mesgmatch(Mesg *m, char *name, char *digest)
{
	if(!(m->state & Sdummy) && strcmp(m->name, name) == 0)
		return digest == nil || strcmp(m->digest, digest) == 0;
	return 0;
}

Mesg*
mesglookup(char *name, char *digest)
{
	char buf[32];
	int i;

	mesgpath2name(buf, sizeof(buf), name);
	for(i = 0; i < mbox.nmesg; i++)
		if(mesgmatch(mbox.mesg[i], buf, digest))
			return mbox.mesg[i];
	return nil;
}

Mesg*
mesgopen(char *name, char *digest)
{
	Mesg *m;
	char *path;
	int fd;

	m = mesglookup(name, digest);
	if(m == nil || (m->state & Sopen))
		return nil;

	assert(!(m->state & Sdummy));
	m->state |= Sopen;
	if(!(m->flags & Fseen)){
		m->flags |= Fseen;
		path = estrjoin(mbox.path, "/", m->name, "/flags", nil);
		if((fd = open(path, OWRITE)) != -1){
			fprint(fd, "+s");
			close(fd);
		}
		mbredraw(m, 0, 0);
		free(path);
	}
	threadcreate(mesgmain, m, Stack);
	return m;
}

Biobuf*
mesgopenbody(Mesg *m)
{
	char *path;
	int rfd;
	Mesg *b;

	b = readparts(m);
	path = estrjoin(mbox.path, b->name, "body", nil);
	if(strcmp(b->type, "text/html") == 0)
		rfd = htmlfmt(m, path);
	else
		rfd = open(path, OREAD);
	free(path);
	if(rfd == -1)
		return nil;
	return Bfdopen(rfd, OREAD);
}
