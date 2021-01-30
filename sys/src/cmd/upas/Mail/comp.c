#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <regexp.h>

#include "mail.h"

typedef struct Fn	Fn;

struct Fn {
	char *name;
	void (*fn)(Comp *, char **, int);
};

void
execmarshal(void *p)
{
	Comp *c;
	char *av[8];
	int na;

	c = p;
	rfork(RFFDG);
	dup(c->fd[0], 0);
	close(c->fd[0]);
	close(c->fd[1]);

	na = 0;
	av[na++] = "marshal";
	av[na++] = "-8";
	if(savebox != nil){
		av[na++] = "-S";
		av[na++] = savebox;
	}
	if(c->rpath != nil){
		av[na++] = "-R";
		av[na++] = c->rpath;
	}
	av[na] = nil;
	assert(na < nelem(av));
	procexec(c->sync, "/bin/upas/marshal", av);
}

static void
postmesg(Comp *c, char **, int nf)
{
	char *buf, wpath[64], *path;
	int n, fd;
	Mesg *m;

	snprint(wpath, sizeof(wpath), "/mnt/acme/%d/body", c->id);
	if(nf != 0){
		fprint(2, "Post: too many args\n");
		return;
	}
	if((fd = open(wpath, OREAD)) == -1){
		fprint(2, "open body: %r\n");
		return;
	}
	if(pipe(c->fd) == -1)
		sysfatal("pipe: %r\n");

	c->sync = chancreate(sizeof(ulong), 0);
	procrfork(execmarshal, c, Stack, RFNOTEG);
	recvul(c->sync);
	chanfree(c->sync);
	close(c->fd[0]);

	/* needed because mail is by default Latin-1 */
	fprint(c->fd[1], "Content-Type: text/plain; charset=\"UTF-8\"\n");
	fprint(c->fd[1], "Content-Transfer-Encoding: 8bit\n");
	buf = emalloc(Bufsz);
	while((n = read(fd, buf, Bufsz)) > 0)
		if(write(c->fd[1], buf, n) != n)
			break;
	write(c->fd[1], "\n", 1);
	close(c->fd[1]);
	close(fd);
	if(n == -1)
		return;

	if(fprint(c->ctl, "name %s:Sent\n", c->path) == -1)
		sysfatal("write ctl: %r");
	if(c->replyto != nil){
		if((m = mesglookup(c->rname, c->rdigest)) == nil)
			return;
		m->flags |= Fresp;
		path = estrjoin(mbox.path, "/", m->name, "/flags", nil);
		if((fd = open(path, OWRITE)) != -1){
			fprint(fd, "+a");
			close(fd);
		}
		mbredraw(m, 0, 0);
		free(path);
	}
	fprint(c->ctl, "clean\n");
}

static void
compquit(Comp *c, char **, int)
{
	c->quitting = 1;
}

static Fn compfn[] = {
	{"Post", postmesg},
	{"Del", compquit},
	{nil},
};

static void
compmain(void *cp)
{
	char *f[32];
	int nf;
	Event ev;
	Comp *c, **pc;
	Fn *p;

	c = cp;
	c->quitting = 0;
	c->qnext = mbox.opencomp;
	mbox.opencomp = c;
	fprint(c->ctl, "clean\n");
	mbox.nopen++;
	while(!c->quitting){
		if(winevent(c, &ev) != 'M')
			continue;
		if(strcmp(ev.text, "Del") == 0)
			break;
		switch(ev.type){
		case 'l':
		case 'L':
			if(matchmesg(&mbox, ev.text))
				mesgopen(ev.text, nil);
			else
				winreturn(c, &ev);
			break;
		case 'x':
		case 'X':
			if((nf = tokenize(ev.text, f, nelem(f))) == 0)
				continue;
			for(p = compfn; p->fn != nil; p++)
				if(strcmp(p->name, f[0]) == 0){
					p->fn(c, &f[1], nf - 1);
					break;
				}
			if(p->fn == nil)
				winreturn(c, &ev);
			break;
		break;
		}
	}
	for(pc = &mbox.opencomp; *pc != nil; pc = &(*pc)->qnext)
		if(*pc == c){
			*pc = c->qnext;
			break;
		}
	mbox.nopen--;
	c->qnext = nil;
	winclose(c);
	free(c->replyto);
	free(c->rname);
	free(c->rdigest);
	free(c->rpath);
	threadexits(nil);
}

static Biobuf*
openbody(Mesg *r)
{
	Biobuf *f;
	int q0, q1;
	char *s;

	assert(r->state & Sopen);

	wingetsel(r, &q0, &q1);
	if(q1 - q0 != 0){
		s = smprint("/mnt/acme/%d/xdata", r->id);
		f = Bopen(s, OREAD);
		free(s);
	}else
		f = mesgopenbody(r);
	return f;
}

int
strpcmp(void *a, void *b)
{
	return strcmp(*(char**)a, *(char**)b);
}

void
show(Biobuf *fd, char *type, char **addrs, int naddrs)
{
	char *sep;
	int i, w;

	w = 0;
	sep = "";
	if(naddrs == 0)
		return;
	qsort(addrs, naddrs, sizeof(char*), strpcmp);
	Bprint(fd, "%s: ", type);
	for(i = 0; i < naddrs; i++){
		if(i > 0 && strcmp(addrs[i-1], addrs[i]) == 0)
			continue;
		w += Bprint(fd, "%s%s", sep, addrs[i]);
		sep = ", ";
		if(w > 50){
			w = 0;
			sep = "";
			Bprint(fd, "\n%s: ", type);
		}
	}
	Bprint(fd, "\n");
}

void
respondto(Biobuf *fd, char *to, Mesg *r, int all)
{
	char *rpto, **addrs;
	int n;

	rpto = to;
	if(r != nil)
		rpto = (strlen(r->replyto) > 0) ? r->replyto : r->from;
	if(r == nil || !all){
		Bprint(fd, "To: %s\n", rpto);
		return;
	}

	n = 0;
	addrs = emalloc(64*sizeof(char*));
	n += tokenize(to, addrs+n, 64-n);
	n += tokenize(rpto, addrs+n, 64-n);
	n += tokenize(r->to, addrs+n, 64-n);
	show(fd, "To", addrs, n);
	n = tokenize(r->cc, addrs+n, 64-n);
	show(fd, "CC", addrs, n);
	free(addrs);
}

void
compose(char *to, Mesg *r, int all)
{
	static int ncompose;
	Biobuf *rfd, *wfd;
	Comp *c;
	char *ln;

	c = emalloc(sizeof(Comp));
	if(r != nil)
		c->path = esmprint("%s%s%s.%d", mbox.path, r->name, "Reply", ncompose++);
	else
		c->path = esmprint("%sCompose.%d", mbox.path, ncompose++);
	wininit(c, c->path);

	wintagwrite(c, "Post |fmt ");
	wfd = bwinopen(c, "body", OWRITE);
	respondto(wfd, to, r, all);
	if(r == nil)
		Bprint(wfd, "Subject: ");
	else{
		if(r->messageid != nil)
			c->replyto = estrdup(r->messageid);
		c->rpath = estrjoin(mbox.path, r->name, nil);
		c->rname = estrdup(r->name);
		c->rdigest = estrdup(r->digest);
		Bprint(wfd, "Subject: ");
		if(r->subject != nil && cistrncmp(r->subject, "Re", 2) != 0)
			Bprint(wfd, "Re: ");
		Bprint(wfd, "%s\n\n", r->subject);
		Bprint(wfd, "Quoth %s:\n", r->fromcolon);
		rfd = openbody(r);
		if(rfd != nil){
			while((ln = Brdstr(rfd, '\n', 0)) != nil)
				if(Bprint(wfd, "> %s", ln) == -1)
					break;
			Bterm(rfd);
		}
		Bterm(wfd);
	}
	Bterm(wfd);
	fprint(c->addr, "$");
	fprint(c->ctl, "dot=addr");
	threadcreate(compmain, c, Stack);
}
