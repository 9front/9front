#include "common.h"
#include <libsec.h>
#include <auth.h>
#include "dat.h"

#pragma varargck type "M" uchar*
#pragma varargck argpos pop3cmd 2
#define pdprint(p, ...)	if((p)->debug) fprint(2, __VA_ARGS__); else{}

typedef struct Popm Popm;
struct Popm{
	int	mesgno;
};

typedef struct Pop Pop;
struct Pop {
	char	*freep;		/* free this to free the strings below */
	char	*host;
	char	*user;
	char	*port;

	int	ppop;
	int	refreshtime;
	int	debug;
	int	pipeline;
	int	encrypted;
	int	needtls;
	int	notls;
	int	needssl;

	Biobuf	bin;		/* open network connection */
	Biobuf	bout;
	int	fd;
	char	*lastline;		/* from Brdstr */
};

static int
mesgno(Message *m)
{
	Popm *a;

	a = m->aux;
	return a->mesgno;
}

static char*
geterrstr(void)
{
	static char err[64];

	err[0] = '\0';
	errstr(err, sizeof(err));
	return err;
}

/*
 *  get pop3 response line , without worrying
 *  about multiline responses; the clients
 *  will deal with that.
 */
static int
isokay(char *s)
{
	return s!=nil && strncmp(s, "+OK", 3)==0;
}

static void
pop3cmd(Pop *pop, char *fmt, ...)
{
	char buf[128], *p;
	va_list va;

	va_start(va, fmt);
	vseprint(buf, buf + sizeof buf, fmt, va);
	va_end(va);

	p = buf + strlen(buf);
	if(p > buf + sizeof buf - 3)
		sysfatal("pop3 command too long");
	pdprint(pop, "<- %s\n", buf);
	strcpy(p, "\r\n");
	Bwrite(&pop->bout, buf, strlen(buf));
	Bflush(&pop->bout);
}

static char*
pop3resp(Pop *pop)
{
	char *s;
	char *p;

	if((s = Brdstr(&pop->bin, '\n', 0)) == nil){
		close(pop->fd);
		pop->fd = -1;
		return "unexpected eof";
	}

	p = s + strlen(s) - 1;
	while(p >= s && (*p == '\r' || *p == '\n'))
		*p-- = '\0';

	pdprint(pop, "-> %s\n", s);
	free(pop->lastline);
	pop->lastline = s;
	return s;
}

/*
 *  get capability list, possibly start tls
 */
static char*
pop3capa(Pop *pop)
{
	char *s;
	int hastls;

	pop3cmd(pop, "CAPA");
	if(!isokay(pop3resp(pop)))
		return nil;

	hastls = 0;
	for(;;){
		s = pop3resp(pop);
		if(strcmp(s, ".") == 0 || strcmp(s, "unexpected eof") == 0)
			break;
		if(strcmp(s, "STLS") == 0)
			hastls = 1;
		if(strcmp(s, "PIPELINING") == 0)
			pop->pipeline = 1;
		if(strcmp(s, "EXPIRE 0") == 0)
			return "server does not allow mail to be left on server";
	}

	if(hastls && !pop->notls){
		pop3cmd(pop, "STLS");
		if(!isokay(s = pop3resp(pop)))
			return s;
		Bterm(&pop->bin);
		Bterm(&pop->bout);
		if((pop->fd = wraptls(pop->fd, pop->host)) < 0)
			return geterrstr();
		pop->encrypted = 1;
		Binit(&pop->bin, pop->fd, OREAD);
		Binit(&pop->bout, pop->fd, OWRITE);
	}
	return nil;
}

/*
 *  log in using APOP if possible, password if allowed by user
 */
static char*
pop3login(Pop *pop)
{
	int n;
	char *s, *p, *q;
	char ubuf[128], user[128];
	char buf[500];
	UserPasswd *up;

	s = pop3resp(pop);
	if(!isokay(s))
		return "error in initial handshake";

	if(pop->user)
		snprint(ubuf, sizeof ubuf, " user=%q", pop->user);
	else
		ubuf[0] = '\0';

	/* look for apop banner */
	if(pop->ppop == 0 && (p = strchr(s, '<')) && (q = strchr(p + 1, '>'))) {
		*++q = '\0';
		if((n=auth_respond(p, q - p, user, sizeof user, buf, sizeof buf, auth_getkey, "proto=apop role=client server=%q%s",
			pop->host, ubuf)) < 0)
			return "factotum failed";
		if(user[0]=='\0')
			return "factotum did not return a user name";

		if(s = pop3capa(pop))
			return s;

		pop3cmd(pop, "APOP %s %.*s", user, utfnlen(buf, n), buf);
		if(!isokay(s = pop3resp(pop)))
			return s;

		return nil;
	} else {
		if(pop->ppop == 0)
			return "no APOP hdr from server";

		if(s = pop3capa(pop))
			return s;

		if(pop->needtls && !pop->encrypted)
			return "could not negotiate TLS";

		up = auth_getuserpasswd(auth_getkey, "proto=pass service=pop dom=%q%s",
			pop->host, ubuf);
		if(up == nil)
			return "no usable keys found";

		pop3cmd(pop, "USER %s", up->user);
		if(!isokay(s = pop3resp(pop))){
			free(up);
			return s;
		}
		pop3cmd(pop, "PASS %s", up->passwd);
		free(up);
		if(!isokay(s = pop3resp(pop)))
			return s;

		return nil;
	}
}

/*
 *  dial and handshake with pop server
 */
static char*
pop3dial(Pop *pop)
{
	char *err;

	if((pop->fd = dial(netmkaddr(pop->host, "net", pop->needssl ? "pop3s" : "pop3"), 0, 0, 0)) < 0)
		return geterrstr();
	if(pop->needssl && (pop->fd = wraptls(pop->fd, pop->host)) < 0)
		return geterrstr();
	pop->encrypted = pop->needssl;
	Binit(&pop->bin, pop->fd, OREAD);
	Binit(&pop->bout, pop->fd, OWRITE);
	if(err = pop3login(pop)) {
		close(pop->fd);
		return err;
	}

	return nil;
}

/*
 *  close connection
 */
static void
pop3hangup(Pop *pop)
{
	pop3cmd(pop, "QUIT");
	pop3resp(pop);
	close(pop->fd);
}

/*
 *  download a single message
 */
static char*
pop3download(Mailbox *mb, Pop *pop, Message *m)
{
	char *s, *f[3], *wp, *ep;
	int l, sz, pos, n;
	Popm *a;

	a = m->aux;
	if(!pop->pipeline)
		pop3cmd(pop, "LIST %d", a->mesgno);
	if(!isokay(s = pop3resp(pop)))
		return s;

	if(tokenize(s, f, 3) != 3)
		return "syntax error in LIST response";

	if(atoi(f[1]) != a->mesgno)
		return "out of sync with pop3 server";

	sz = atoi(f[2]) + 200;	/* 200 because the plan9 pop3 server lies */
	if(sz == 0)
		return "invalid size in LIST response";

	m->start = wp = emalloc(sz + 1);
	ep = wp + sz;

	if(!pop->pipeline)
		pop3cmd(pop, "RETR %d", a->mesgno);
	if(!isokay(s = pop3resp(pop))) {
		m->start = nil;
		free(wp);
		return s;
	}

	s = nil;
	while(wp <= ep) {
		s = pop3resp(pop);
		if(strcmp(s, "unexpected eof") == 0) {
			free(m->start);
			m->start = nil;
			return "unexpected end of conversation";
		}
		if(strcmp(s, ".") == 0)
			break;

		l = strlen(s) + 1;
		if(s[0] == '.') {
			s++;
			l--;
		}
		/*
		 * grow by 10%/200bytes - some servers
		 *  lie about message sizes
		 */
		if(wp + l > ep) {
			pos = wp - m->start;
			n = sz/10;
			if(n < 200)
				n = 200;
			sz += n;
			m->start = erealloc(m->start, sz + 1);
			wp = m->start + pos;
			ep = m->start + sz;
		}
		memmove(wp, s, l - 1);
		wp[l-1] = '\n';
		wp += l;
	}

	if(s == nil || strcmp(s, ".") != 0)
		return "out of sync with pop3 server";

	m->end = wp;

	/*
	 *  make sure there's a trailing null
	 *  (helps in body searches)
	 */
	*m->end = 0;
	m->bend = m->rbend = m->end;
	m->header = m->start;
	m->size = m->end - m->start;
	if(m->digest == nil)
		digestmessage(mb, m);

	return nil;
}

/*
 *  check for new messages on pop server
 *  UIDL is not required by RFC 1939, but
 *  netscape requires it, so almost every server supports it.
 *  we'll use it to make our lives easier.
 */
static char*
pop3read(Pop *pop, Mailbox *mb)
{
	char *s, *p, *uidl, *f[2];
	int mno, ignore;
	Message *m, *next, **l;
	Popm *a;

	/* Some POP servers disallow UIDL if the maildrop is empty. */
	pop3cmd(pop, "STAT");
	if(!isokay(s = pop3resp(pop)))
		return s;

	/* fetch message listing; note messages to grab */
	l = &mb->root->part;
	if(strncmp(s, "+OK 0 ", 6) != 0) {
		pop3cmd(pop, "UIDL");
		if(!isokay(s = pop3resp(pop)))
			return s;

		for(;;){
			p = pop3resp(pop);
			if(strcmp(p, ".") == 0 || strcmp(p, "unexpected eof") == 0)
				break;

			if(tokenize(p, f, 2) != 2)
				continue;

			mno = atoi(f[0]);
			uidl = f[1];
			if(strlen(uidl) > 75)	/* RFC 1939 says 70 characters max */
				continue;

			ignore = 0;
			while(*l != nil) {
				a = (*l)->aux;
				if(strcmp((*l)->idxaux, uidl) == 0){
					if(a == 0){
						m = *l;
						m->mallocd = 1;
						m->inmbox = 1;
						m->aux = a = emalloc(sizeof *a);
					}
					/* matches mail we already have, note mesgno for deletion */
					a->mesgno = mno;
					ignore = 1;
					l = &(*l)->next;
					break;
				}else{
					/* old mail no longer in box mark deleted */
					(*l)->inmbox = 0;
					(*l)->deleted = Deleted;
					l = &(*l)->next;
				}
			}
			if(ignore)
				continue;

			m = newmessage(mb->root);
			m->mallocd = 1;
			m->inmbox = 1;
			m->idxaux = strdup(uidl);
			m->aux = a = emalloc(sizeof *a);
			a->mesgno = mno;

			/* chain in; will fill in message later */
			*l = m;
			l = &m->next;
		}
	}

	/* whatever is left has been removed from the mbox, mark as deleted */
	while(*l != nil) {
		(*l)->inmbox = 0;
		(*l)->deleted = Disappear;
		l = &(*l)->next;
	}

	/* download new messages */
	if(pop->pipeline){
		switch(rfork(RFPROC|RFMEM)){
		case -1:
			eprint("pop3: rfork: %r\n");
			pop->pipeline = 0;

		default:
			break;

		case 0:
			for(m = mb->root->part; m != nil; m = m->next){
				if(m->start != nil || m->deleted)
					continue;
				Bprint(&pop->bout, "LIST %d\r\nRETR %d\r\n", mesgno(m), mesgno(m));
			}
			Bflush(&pop->bout);
			_exits("");
		}
	}

	for(m = mb->root->part; m != nil; m = next) {
		next = m->next;

		if(m->start != nil || m->deleted)
			continue;
		if(s = pop3download(mb, pop, m)) {
			eprint("pop3: download %d: %s\n", mesgno(m), s);
			unnewmessage(mb, mb->root, m);
			continue;
		}
		parse(mb, m, 1, 0);
	}
	if(pop->pipeline)
		waitpid();
	return nil;	
}

/*
 *  delete marked messages
 */
static void
pop3purge(Pop *pop, Mailbox *mb)
{
	Message *m;

	if(pop->pipeline){
		switch(rfork(RFPROC|RFMEM)){
		case -1:
			eprint("pop3: rfork: %r\n");
			pop->pipeline = 0;

		default:
			break;

		case 0:
			for(m = mb->root->part; m != nil; m = m->next){
				if(m->deleted && m->inmbox)
					Bprint(&pop->bout, "DELE %d\r\n", mesgno(m));
			}
			Bflush(&pop->bout);
			_exits("");
		}
	}
	for(m = mb->root->part; m != nil; m = m->next) {
		if(m->deleted && m->inmbox) {
			if(!pop->pipeline)
				pop3cmd(pop, "DELE %d", mesgno(m));
			if(!isokay(pop3resp(pop)))
				continue;
			m->inmbox = 0;
		}
	}
}


/* connect to pop3 server, sync mailbox */
static char*
pop3sync(Mailbox *mb)
{
	char *err;
	Pop *pop;

	pop = mb->aux;
	if(err = pop3dial(pop))
		goto out;
	if((err = pop3read(pop, mb)) == nil)
		pop3purge(pop, mb);
	pop3hangup(pop);
out:
	mb->waketime = (ulong)time(0) + pop->refreshtime;
	return err;
}

static char Epop3ctl[] = "bad pop3 control message";

static char*
pop3ctl(Mailbox *mb, int argc, char **argv)
{
	int n;
	Pop *pop;

	pop = mb->aux;
	if(argc < 1)
		return Epop3ctl;

	if(argc==1 && strcmp(argv[0], "debug")==0){
		pop->debug = 1;
		return nil;
	}

	if(argc==1 && strcmp(argv[0], "nodebug")==0){
		pop->debug = 0;
		return nil;
	}

	if(strcmp(argv[0], "refresh")==0){
		if(argc==1){
			pop->refreshtime = 60;
			return nil;
		}
		if(argc==2){
			n = atoi(argv[1]);
			if(n < 15)
				return Epop3ctl;
			pop->refreshtime = n;
			return nil;
		}
	}

	return Epop3ctl;
}

/* free extra memory associated with mb */
static void
pop3close(Mailbox *mb)
{
	Pop *pop;

	pop = mb->aux;
	free(pop->freep);
	free(pop);
}

static char*
mkmbox(Pop *pop, char *p, char *e)
{
	p = seprint(p, e, "%s/box/%s/pop.%s", MAILROOT, getlog(), pop->host);
	if(pop->user && strcmp(pop->user, getlog()))
		p = seprint(p, e, ".%s", pop->user);
	return p;
}

/*
 *  open mailboxes of the form /pop/host/user or /apop/host/user
 */
char*
pop3mbox(Mailbox *mb, char *path)
{
	char *f[10];
	int nf, apop, ppop, popssl, apopssl, apoptls, popnotls, apopnotls, poptls;
	Pop *pop;

	popssl = strncmp(path, "/pops/", 6) == 0;
	apopssl = strncmp(path, "/apops/", 7) == 0;
	poptls = strncmp(path, "/poptls/", 8) == 0;
	popnotls = strncmp(path, "/popnotls/", 10) == 0;
	ppop = popssl || poptls || popnotls || strncmp(path, "/pop/", 5) == 0;
	apoptls = strncmp(path, "/apoptls/", 9) == 0;
	apopnotls = strncmp(path, "/apopnotls/", 11) == 0;
	apop = apopssl || apoptls || apopnotls || strncmp(path, "/apop/", 6) == 0;

	if(!ppop && !apop)
		return Enotme;

	path = strdup(path);
	if(path == nil)
		return "out of memory";

	nf = getfields(path, f, nelem(f), 0, "/");
	if(nf != 3 && nf != 4) {
		free(path);
		return "bad pop3 path syntax /[a]pop[tls|ssl]/system[/user]";
	}

	pop = emalloc(sizeof *pop);
	pop->freep = path;
	pop->host = f[2];
	if(nf < 4)
		pop->user = nil;
	else
		pop->user = f[3];
	pop->ppop = ppop;
	pop->needssl = popssl || apopssl;
	pop->needtls = poptls || apoptls;
	pop->refreshtime = 60;
	pop->notls = popnotls || apopnotls;
	mkmbox(pop, mb->path, mb->path + sizeof mb->path);
	mb->aux = pop;
	mb->sync = pop3sync;
	mb->close = pop3close;
	mb->ctl = pop3ctl;
	mb->move = nil;
	mb->addfrom = 1;
	return nil;
}
