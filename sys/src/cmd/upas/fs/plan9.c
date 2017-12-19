#include "common.h"
#include <libsec.h>
#include "dat.h"

typedef struct {
	Biobuf	*in;
	char	*shift;
} Inbuf;

/*
 *  parse a Unix style header
 */
static int
memtotm(char *p, int n, Tm *t)
{
	char buf[128];

	if(n > sizeof buf - 1)
		n = sizeof buf -1;
	memcpy(buf, p, n);
	buf[n] = 0;
	return strtotm(buf, t);
}

static int
chkunix0(char *s, int n)
{
	char *p;
	Tm tm;

	if(n > 256)
		return -1;
	if((p = memchr(s, ' ', n)) == nil)
		return -1;
	if(memtotm(p, n - (p - s), &tm) < 0)
		return -1;
	if(tm2sec(&tm) < 1000000)
		return -1;
	return 0;
}

static int
chkunix(char *s, int n)
{
	int r;

	r = chkunix0(s, n);
	if(r == -1)
		eprint("plan9: warning naked from [%.*s]\n", n, s);
	return r;
}

static char*
parseunix(Message *m)
{
	char *s, *p, *q;
	int l;
	Tm tm;

	l = m->header - m->start;
	m->unixheader = smprint("%.*s", l, m->start);
	s = m->start + 5;
	if((p = strchr(s, ' ')) == nil)
		return s;
	*p = 0;
	m->unixfrom = strdup(s);
	*p++ = ' ';
	if(q = strchr(p, '\n'))
		*q = 0;
	if(strtotm(p, &tm) < 0)
		return p;
	if(q)
		*q = '\n';
	m->fileid = (uvlong)tm2sec(&tm) << 8;
	return 0;
}

static void
addtomessage(Message *m, char *p, int n)
{
	int i, len;

	if(n == 0)
		return;
	/* add to message (+1 in malloc is for a trailing NUL) */
	if(m->lim - m->end < n){
		if(m->start != nil){
			i = m->end - m->start;
			len = (4*(i + n))/3;
			m->start = erealloc(m->start, len + 1);
			m->end = m->start + i;
		} else {
			len = 2*n;
			m->start = emalloc(len + 1);
			m->end = m->start;
		}
		m->lim = m->start + len;
		*m->lim = 0;
	}

	memmove(m->end, p, n);
	m->end += n;
	*m->end = 0;
}

/*
 *   read in a single message
 */
static int
okmsg(Mailbox *mb, Message *m, Inbuf *b)
{
	char e[ERRMAX], buf[128];

	rerrstr(e, sizeof e);
	if(strlen(e)){
		if(fd2path(Bfildes(b->in), buf, sizeof buf) < 0)
			strcpy(buf, "unknown mailbox");
		eprint("plan9: error reading %s: %r\n", buf);
		return -1;
	}
	if(m->end == m->start)
		return -1;
	if(m->end[-1] == '\n')
		m->end--;
	*m->end = 0;
	m->size = m->end - m->start;
	if(m->size > Maxmsg)
		return -1;
	m->bend = m->rbend = m->end;
	if(m->digest == nil)
		digestmessage(mb, m);
	return 0;
}

static char*
inbread(Inbuf *b)
{
	if(b->shift)
		return b->shift;
	return b->shift = Brdline(b->in, '\n');
}

void
inbconsume(Inbuf *b)
{
	b->shift = 0;
}

/*
 * bug: very long line with From at the buffer break.
 */
static int
readmessage(Mailbox *mb, Message *m, Inbuf *b)
{
	char *s, *n;
	long l, state;

	werrstr("");
	state = 0;
	for(;;){
		s = inbread(b);
		if(s == 0)
			break;
		n = s + (l = Blinelen(b->in)) - 1;
		if(l >= 28 + 7 && n[0] == '\n')
		if(strncmp(s, "From ", 5) == 0)
		if(!chkunix(s + 5, l - 5))
		if(++state == 2)
			break;
		if(state == 0)
			return -1;
		addtomessage(m, s, l);
		inbconsume(b);
	}
	return okmsg(mb, m, b);
}

/* throw out deleted messages.  return number of freshly deleted messages */
int
purgedeleted(Mailbox *mb)
{
	Message *m;
	int newdels;

	/* forget about what's no longer in the mailbox */
	newdels = 0;
	for(m = mb->root->part; m != nil; m = m->next){
		if(m->deleted && m->inmbox){
			newdels++;
			m->inmbox = 0;
		}
	}
	return newdels;
}

static void
mergemsg(Message *m, Message *x)
{
	assert(m->start == 0);
	m->mallocd = 1;
	m->inmbox = 1;
	m->lim = x->lim;
	m->start = x->start;
	m->end = x->end;
	m->bend = x->bend;
	m->rbend = x->rbend;
	x->lim = 0;
	x->start = 0;
	x->end = 0;
	x->bend = 0;
	x->rbend = 0;
}

/*
 *   read in the mailbox and parse into messages.
 */
static char*
readmbox(Mailbox *mb, Mlock *lk)
{
	char *p, *x, buf[Pathlen];
	Biobuf *in;
	Dir *d;
	Inbuf b;
	Message *m, **l;
	static char err[ERRMAX];

	l = &mb->root->part;

	/*
	 *  open the mailbox.  If it doesn't exist, try the temporary one.
	 */
retry:
	in = Bopen(mb->path, OREAD);
	if(in == nil){
		errstr(err, sizeof(err));
		if(strstr(err, "exist") != 0){
			snprint(buf, sizeof buf, "%s.tmp", mb->path);
			if(sysrename(buf, mb->path) == 0)
				goto retry;
		}
		return err;
	}

	/*
	 *  a new qid.path means reread the mailbox, while
	 *  a new qid.vers means read any new messages
	 */
	d = dirfstat(Bfildes(in));
	if(d == nil){
		Bterm(in);
		errstr(err, sizeof err);
		return err;
	}
	if(mb->d != nil){
		if(d->qid.path == mb->d->qid.path && d->qid.vers == mb->d->qid.vers){
			Bterm(in);
			free(d);
			return nil;
		}
		if(d->qid.path == mb->d->qid.path){
			while(*l != nil)
				l = &(*l)->next;
			Bseek(in, mb->d->length, 0);
		}
		free(mb->d);
	}
	mb->d = d;

	memset(&b, 0, sizeof b);
	b.in = in;
	b.shift = 0;

	/*  read new messages */
	logmsg(nil, "reading %s", mb->path);
	for(;;){
		if(lk != nil)
			syslockrefresh(lk);
		m = newmessage(mb->root);
		m->mallocd = 1;
		m->inmbox = 1;
		if(readmessage(mb, m, &b) < 0){
			unnewmessage(mb, mb->root, m);
			break;
		}
		/* merge mailbox versions */
		while(*l != nil){
			if(memcmp((*l)->digest, m->digest, SHA1dlen) == 0){
				if((*l)->start == nil){
					logmsg(*l, "read indexed");
					mergemsg(*l, m);
					unnewmessage(mb, mb->root, m);
					m = *l;
				}else{
					logmsg(*l, "duplicate");
					m->inmbox = 1;		/* remove it */
					unnewmessage(mb, mb->root, m);
					m = nil;
					l = &(*l)->next;
				}
				break;
			} else {
				/* old mail no longer in box, mark deleted */
				logmsg(*l, "disappeared");
				(*l)->inmbox = 0;
				(*l)->deleted = Disappear;
				l = &(*l)->next;
			}
		}
		if(m == nil)
			continue;
		m->header = m->end;
		if(x = strchr(m->start, '\n'))
			m->header = x + 1;
		if(p = parseunix(m))
			sysfatal("%s:%lld naked From in body? [%s]", mb->path, seek(Bfildes(in), 0, 1), p);
		m->mheader = m->mhend = m->header;
		parse(mb, m, 0, 0);
		if(m != *l && m->deleted != Dup){
			logmsg(m, "new");
		}
		/* chain in */
		*l = m;
		l = &m->next;
	}
	logmsg(nil, "mbox read");

	/* whatever is left has been removed from the mbox, mark deleted */
	while(*l != nil){
		(*l)->inmbox = 0;
		(*l)->deleted = Deleted;
		l = &(*l)->next;
	}

	Bterm(in);
	return nil;
}

static void
writembox(Mailbox *mb, Mlock *lk)
{
	char buf[Pathlen];
	int mode, errs;
	Biobuf *b;
	Dir *d;
	Message *m;

	snprint(buf, sizeof buf, "%s.tmp", mb->path);

	/*
	 * preserve old files permissions, if possible
	 */
	mode = Mboxmode;
	if(d = dirstat(mb->path)){
		mode = d->mode & 0777;
		free(d);
	}

	remove(buf);
	b = sysopen(buf, "alc", mode);
	if(b == 0){
		eprint("plan9: can't write temporary mailbox %s: %r\n", buf);
		return;
	}

	logmsg(nil, "writing new mbox");
	errs = 0;
	for(m = mb->root->part; m != nil; m = m->next){
		if(lk != nil)
			syslockrefresh(lk);
		if(m->deleted)
			continue;
		logmsg(m, "writing");
		if(Bwrite(b, m->start, m->end - m->start) < 0)
			errs = 1;
		if(Bwrite(b, "\n", 1) < 0)
			errs = 1;
	}
	logmsg(nil, "wrote new mbox");

	if(sysclose(b) < 0)
		errs = 1;

	if(errs){
		eprint("plan9: error writing temporary mail file\n");
		return;
	}

	remove(mb->path);
	if(sysrename(buf, mb->path) < 0)
		eprint("plan9: can't rename %s to %s: %r\n",
			buf, mb->path);
	if(mb->d != nil)
		free(mb->d);
	mb->d = dirstat(mb->path);
}

char*
plan9syncmbox(Mailbox *mb)
{
	char *rv;
	Mlock *lk;

	lk = nil;
	if(mb->dolock){
		lk = syslock(mb->path);
		if(lk == nil)
			return "can't lock mailbox";
	}

	rv = readmbox(mb, lk);		/* interpolate */
	if(purgedeleted(mb) > 0)
		writembox(mb, lk);

	if(lk != nil)
		sysunlock(lk);

	return rv;
}

void
plan9decache(Mailbox*, Message *m)
{
	m->lim = 0;
}

/*
 *   look to see if we can open this mail box
 */
char*
plan9mbox(Mailbox *mb, char *path)
{
	char buf[Pathlen];
	static char err[Pathlen];

	if(access(path, AEXIST) < 0){
		errstr(err, sizeof err);
		snprint(buf, sizeof buf, "%s.tmp", path);
		if(access(buf, AEXIST) < 0)
			return err;
	}
	mb->sync = plan9syncmbox;
	mb->remove = localremove;
	mb->rename = localrename;
	mb->decache = plan9decache;
	return nil;
}
