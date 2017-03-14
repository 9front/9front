#include "common.h"
#include <fcall.h>
#include <libsec.h>
#include <pool.h>
#include "dat.h"

typedef struct Fid Fid;
struct Fid
{
	Qid	qid;
	short	busy;
	short	open;
	int	fid;
	Fid	*next;
	Mailbox	*mb;
	Message	*m;
	Message *mtop;		/* top level message */

	long	foff;		/* offset/DIRLEN of finger */
	Message	*fptr;		/* pointer to message at off */
	int	fvers;		/* mailbox version when finger was saved */
};

Fid		*newfid(int);
void		error(char*);
void		io(void);
void		*erealloc(void*, ulong);
void		*emalloc(ulong);
void		usage(void);
void		reader(void);
int		readheader(Message*, char*, int, int);
void		post(char*, char*, int);

char	*rflush(Fid*), *rauth(Fid*),
	*rattach(Fid*), *rwalk(Fid*),
	*ropen(Fid*), *rcreate(Fid*),
	*rread(Fid*), *rwrite(Fid*), *rclunk(Fid*),
	*rremove(Fid*), *rstat(Fid*), *rwstat(Fid*),
	*rversion(Fid*);

char 	*(*fcalls[])(Fid*) = {
	[Tflush]	rflush,
	[Tversion]	rversion,
	[Tauth]	rauth,
	[Tattach]	rattach,
	[Twalk]		rwalk,
	[Topen]		ropen,
	[Tcreate]	rcreate,
	[Tread]		rread,
	[Twrite]	rwrite,
	[Tclunk]	rclunk,
	[Tremove]	rremove,
	[Tstat]		rstat,
	[Twstat]	rwstat,
};

char	Eperm[] =	"permission denied";
char	Enotdir[] =	"not a directory";
char	Enoauth[] =	"upas/fs: authentication not required";
char	Enotexist[] =	"file does not exist";
char	Einuse[] =	"file in use";
char	Eexist[] =	"file exists";
char	Enotowner[] =	"not owner";
char	Eisopen[] = 	"file already open for I/O";
char	Excl[] = 	"exclusive use file already open";
char	Ename[] = 	"illegal name";
char	Ebadctl[] =	"unknown control message";
char	Ebadargs[] =	"invalid arguments";
char 	Enotme[] =	"path not served by this file server";

char *dirtab[] = {
[Qdir]		".",
[Qbcc]		"bcc",
[Qbody]		"body",
[Qcc]		"cc",
[Qdate]		"date",
[Qdigest]	"digest",
[Qdisposition]	"disposition",
[Qffrom]		"ffrom",
[Qfileid]		"fileid",
[Qfilename]	"filename",
[Qflags]		"flags",
[Qfrom]		"from",
[Qheader]	"header",
[Qinfo]		"info",
[Qinreplyto]	"inreplyto",
[Qlines]		"lines",
[Qmessageid]	"messageid",
[Qmimeheader]	"mimeheader",
[Qraw]		"raw",
[Qrawbody]	"rawbody",
[Qrawheader]	"rawheader",
[Qrawunix]	"rawunix",
[Qreferences]	"references",
[Qreplyto]	"replyto",
[Qsender]	"sender",
[Qsize]		"size",
[Qsubject]	"subject",
[Qto]		"to",
[Qtype]		"type",
[Qunixdate]	"unixdate",
[Qunixheader]	"unixheader",
[Qctl]		"ctl",
[Qmboxctl]	"ctl",
};

enum
{
	Hsize=	1999,
};


char	*mntpt;
char	user[Elemlen];
int	Dflag;
int	Sflag;
int	iflag;
int	lflag;
int	biffing;
int	debug;
int	plumbing = 1;
ulong	cachetarg = Maxcache;
Mailbox	*mbl;
QLock	mbllock;

static	int	messagesize = 8*1024 + IOHDRSZ;
static	int	mfd[2];
static	char	hbuf[32*1024];
static	uchar	mbuf[16*1024 + IOHDRSZ];
static	uchar	mdata[16*1024 + IOHDRSZ];
static	ulong	path;		/* incremented for each new file */
static	Hash	*htab[Hsize];
static	Fcall	rhdr;
static	Fcall	thdr;
static	Fid	*fids;

void
sanemsg(Message *m)
{
	if(debug)
		poolcheck(mainmem);
	assert(m->refs < 100);
	assert(m->next != m);
	if(m->end < m->start)
		abort();
	if(m->ballocd && (m->start <= m->body && m->end >= m->body))
		abort();
	if(m->end - m->start > Maxmsg)
		abort();
	if(m->size > Maxmsg)
		abort();
	if(m->fileid != 0 && m->fileid <= 1000000ull<<8)
		abort();
}

void
sanembmsg(Mailbox *mb, Message *m)
{
	sanemsg(m);
	if(Topmsg(mb, m)){
		if(m->start > end && m->size == 0)
			abort();
		if(m->fileid <= 1000000ull<<8)
			abort();
	}
}

static int
Afmt(Fmt *f)
{
	char buf[SHA1dlen*2 + 1];
	uchar *u, i;

	u = va_arg(f->args, uchar*);
	if(u == 0 && f->flags & FmtSharp)
		return fmtstrcpy(f, "-");
	if(u == 0)
		return fmtstrcpy(f, "<nildigest>");
	for(i = 0; i < SHA1dlen; i++)
		sprint(buf + 2*i, "%2.2ux", u[i]);
	return fmtstrcpy(f, buf);
}

static int
Δfmt(Fmt *f)
{
	char buf[32];
	uvlong v;

	v = va_arg(f->args, uvlong);
	if(f->flags & FmtSharp)
		if((v>>8) == 0)
			return fmtstrcpy(f, "");
	strcpy(buf, ctime(v>>8));
	buf[28] = 0;
	return fmtstrcpy(f, buf);
}

static int
Dfmt(Fmt *f)
{
	char buf[32];
	int seq;
	uvlong v;

	v = va_arg(f->args, uvlong);
	seq = v & 0xff;
	if(seq > 99)
		seq = 99;
	snprint(buf, sizeof buf, "%llud.%.2d", v>>8, seq);
	return fmtstrcpy(f, buf);
}

Mpair
mpair(Mailbox *mb, Message *m)
{
	Mpair mp;

	mp.mb = mb;
	mp.m = m;
	return mp;
}

static int
Pfmt(Fmt *f)
{
	char buf[128], *p, *e;
	int i, dots;
	Mailbox *mb;
	Message *t[32], *m;
	Mpair mp;

	mp = va_arg(f->args, Mpair);
	mb = mp.mb;
	m = mp.m;
	if(m == nil || mb == nil)
		return fmtstrcpy(f, "<P nil>");
	i = 0;
	for(; !Topmsg(mb, m); m = m->whole){
		t[i++] = m;
		if(i == nelem(t)-1)
			break;
	}
	t[i++] = m;
	dots = 0;
	if(i == nelem(t))
		dots = 1;
	e = buf + sizeof buf;
	p = buf;
	if(dots)
		p = seprint(p, e, ".../");
	while(--i >= 1)
		p = seprint(p, e, "%s/", t[i]->name);
	if(i == 0)
		seprint(p, e, "%s", t[0]->name);
	return fmtstrcpy(f, buf);
}

void
usage(void)
{
	fprint(2, "usage: upas/fs [-DSbdlmnps] [-c cachetarg] [-f mboxfile] [-m mountpoint]\n");
	exits("usage");
}

void
notifyf(void *, char *s)
{
	if(strncmp(s, "interrupt", 9) == 0)
		noted(NCONT);
	if(strncmp(s, "die: yankee pig dog", 19) != 0)
		/* don't want to call syslog from notify handler */
		fprint(2, "upas/fs: user: %s; note: %s\n", getuser(), s);
	noted(NDFLT);
}

void
setname(char **v)
{
	char buf[128], buf2[32], *p, *e;
	int fd, i;

	e = buf + sizeof buf;
	p = seprint(buf, e, "%s", v[0]);
	for(i = 0; v[++i]; )
		p = seprint(p, e, " %s", v[i]);
	snprint(buf2, sizeof buf2, "#p/%d/args", getpid());
	if((fd = open(buf2, OWRITE)) >= 0){
		write(fd, buf, p - buf);
		close(fd);
	}
}

ulong
ntoi(char *s)
{
	ulong n;

	n = strtoul(s, &s, 0);
	for(;;)
	switch(*s++){
	default:
		usage();
	case 'g':
		n *= 1024;
	case 'm':
		n *= 1024;
	case 'k':
		n *= 1024;
		break;
	case 0:
		return n;
	}
}

void
main(int argc, char *argv[])
{
	char maildir[Pathlen], mbox[Pathlen], srvfile[64], **v;
	char *mboxfile, *err;
	int p[2], nodflt, srvpost;

	rfork(RFNOTEG);
	mboxfile = nil;
	nodflt = 0;
	srvpost = 0;
	v = argv;

	ARGBEGIN{
	case 'D':
		Dflag = 1;
		break;
	case 'S':
		Sflag = 1;
		break;
	case 'b':
		biffing = 1;
		break;
	case 'c':
		cachetarg = ntoi(EARGF(usage()));
		break;
	case 'd':
		debug = 1;
		mainmem->flags |= POOL_PARANOIA;
		break;
	case 'f':
		mboxfile = EARGF(usage());
		break;
	case 'i':
		iflag++;
		break;
	case 'l':
		lflag = 1;
		break;
	case 'm':
		mntpt = EARGF(usage());
		break;
	case 'n':
		nodflt = 1;
		break;
	case 'p':
		plumbing = 0;
		break;
	case 's':
		srvpost = 1;
		break;
	default:
		usage();
	}ARGEND

	if(argc)
		usage();
	fmtinstall('A', Afmt);
	fmtinstall('D', Dfmt);
	fmtinstall(L'Δ', Δfmt);
	fmtinstall('F', fcallfmt);
	fmtinstall('H', encodefmt);		/* forces tls stuff */
	fmtinstall('P', Pfmt);
	quotefmtinstall();
	if(pipe(p) < 0)
		error("pipe failed");
	mfd[0] = p[0];
	mfd[1] = p[0];

	notify(notifyf);
	strcpy(user, getuser());
	if(mntpt == nil){
		snprint(maildir, sizeof(maildir), "/mail/fs");
		mntpt = maildir;
	}
	if(mboxfile == nil && !nodflt){
		snprint(mbox, sizeof mbox, "/mail/box/%s/mbox", user);
		mboxfile = mbox;
	}

	if(mboxfile != nil)
		if(err = newmbox(mboxfile, "mbox", 0, nil))
			sysfatal("opening %s: %s", mboxfile, err);

	switch(rfork(RFFDG|RFPROC|RFNAMEG|RFNOTEG|RFREND)){
	case -1:
		error("fork");
	case 0:
		henter(PATH(0, Qtop), dirtab[Qctl],
			(Qid){PATH(0, Qctl), 0, QTFILE}, nil, nil);
		close(p[1]);
		setname(v);
		io();
		syncallmboxes();
		syskillpg(getpid());
		break;
	default:
		close(p[0]);	/* don't deadlock if child fails */
		if(srvpost){
			snprint(srvfile, sizeof srvfile, "/srv/upasfs.%s", user);
			post(srvfile, "upasfs", p[1]);
		}else
			if(mount(p[1], -1, mntpt, MREPL, "") < 0)
				error("mount failed");
	}
	exits("");
}

char*
sputc(char *p, char *e, int c)
{
	if(p < e - 1)
		*p++ = c;
	return p;
}

char*
seappend(char *s, char *e, char *a, int n)
{
	int l;

	l = e - s - 1;
	if(l < n)
		n = l;
	memcpy(s, a, n);
	s += n;
	*s = 0;
	return s;
}

static int
fileinfo(Mailbox *mb, Message *m, int t, char **pp)
{
	char *s, *e, *p;
	int len, i;
	static char buf[64 + 512];

	cacheidx(mb, m);
	sanembmsg(mb, m);
	p = nil;
	len = -1;
	switch(t){
	case Qbody:
		cachebody(mb, m);
		p = m->body;
		len = m->bend - p;
		break;
	case Qbcc:
		p = m->bcc;
		break;
	case Qcc:
		p = m->cc;
		break;
	case Qdisposition:
		switch(m->disposition){
		case Dinline:
			p = "inline";
			break;
		case Dfile:
			p = "file";
			break;
		}
		break;
	case Qdate:
		p = m->date822;
		if(!p){
			p = buf;
			len = snprint(buf, sizeof buf, "%#Δ", m->fileid);
		}
		break;
	case Qfilename:
		p = m->filename;
		break;
	case Qflags:
		p = flagbuf(buf, m->flags);
		break;
	case Qinreplyto:
		p = m->inreplyto;
		break;
	case Qmessageid:
		p = m->messageid;
		break;
	case Qfrom:
		if(m->from)
			p = m->from;
		else
			p = m->unixfrom;
		break;
	case Qffrom:
		if(m->ffrom)
			p = m->ffrom;
		break;
	case Qlines:
		len = snprint(buf, sizeof buf, "%lud", m->lines);
		p = buf;
		break;
	case Qraw:
		cachebody(mb, m);
		p = m->start;
		if(strncmp(m->start, "From ", 5) == 0)
		if(e = strchr(p, '\n'))
			p = e + 1;
		len = m->rbend - p;
		break;
	case Qrawunix:
		cachebody(mb, m);
		p = m->start;
		len = m->end - p;
		break;
	case Qrawbody:
		cachebody(mb, m);
		p = m->rbody;
		len = m->rbend - p;
		break;
	case Qrawheader:
		cacheheaders(mb, m);
		p = m->header;
		len = m->hend - p;
		break;
	case Qmimeheader:
		cacheheaders(mb, m);
		p = m->mheader;
		len = m->mhend - p;
		break;
	case Qreferences:
		cacheheaders(mb, m);
		e = buf + sizeof buf;
		s = buf;
		for(i = 0; i < nelem(m->references); i++){
			if(m->references[i] == 0)
				break;
			s = seprint(s, e, "%s\n", m->references[i]);
		}
		*s = 0;
		p = buf;
		len = s - buf;
		break;
	case Qreplyto:
		if(m->replyto != nil)
			p = m->replyto;
		else if(m->from != nil)
			p = m->from;
		else if(m->sender != nil)
			p = m->sender;
		else if(m->unixfrom != nil)
			p = m->unixfrom;
		break;
	case Qsender:
		p = m->sender;
		break;
	case Qsubject:
		p = m->subject;
		break;
	case Qsize:
		len = snprint(buf, sizeof buf, "%lud", m->size);
		p = buf;
		break;
	case Qto:
		p = m->to;
		break;
	case Qtype:
		p = rtab[m->type].s;
		len = rtab[m->type].l;
		break;
	case Qunixdate:
		p = buf;
		len = snprint(buf, sizeof buf, "%#Δ", m->fileid);
		break;
	case Qfileid:
		p = buf;
		len = snprint(buf, sizeof buf, "%D", m->fileid);
		break;
	case Qunixheader:
		cacheheaders(mb, m);
		p = m->unixheader;
		break;
	case Qdigest:
		p = buf;
		len = snprint(buf, sizeof buf, "%A", m->digest);
		break;
	}
	if(p == nil)
		p = "";
	if(len == -1)
		len = strlen(p);
	*pp = p;
	putcache(mb, m);
	return len;
}

int infofields[] = {
	Qfrom,
	Qto,
	Qcc,
	Qreplyto,
	Qunixdate,
	Qsubject,
	Qtype,
	Qdisposition,
	Qfilename,
	Qdigest,
	Qbcc,
	Qinreplyto,
	Qdate,
	Qsender,
	Qmessageid,
	Qlines,
	Qsize,
	Qflags,
	Qfileid,
	Qffrom,
};

int
readinfo(Mailbox *mb, Message *m, char *buf, long off, int count)
{
	char *s, *p, *e;
	int i, n;
	long off0;

	if(m->infolen > 0 && off >= m->infolen)
		return 0;
	off0 = off;
	s = buf;
	e = s + count;
	for(i = 0; s < e; i++){
		if(i == nelem(infofields)){
			m->infolen = s - buf + off0;
			break;
		}
		n = fileinfo(mb, m, infofields[i], &p);
		if(off > n){
			off -= n + 1;
			continue;
		}
		if(off){
			n -= off;
			p += off;
			off = 0;
		}
		if(s + n > e)
			n = e - s;
		memcpy(s, p, n);
		s += n;
		if(s < e)
			*s++ = '\n';
	}
	return s - buf;
}

static void
mkstat(Dir *d, Mailbox *mb, Message *m, int t)
{
	char *p, *e;

	d->uid = user;
	d->gid = user;
	d->muid = user;
	d->mode = 0444;
	d->qid.vers = 0;
	d->qid.type = QTFILE;
	d->type = 0;
	d->dev = 0;
	if(m && m->fileid > 1000000ull)
		d->atime = m->fileid >> 8;
	else if(mb && mb->d)
		d->atime = mb->d->mtime;
	else
		d->atime = time(0);
	d->mtime = d->atime;

	switch(t){
	case Qtop:
		d->name = ".";
		d->mode = DMDIR|0555;
		d->atime = d->mtime = time(0);
		d->length = 0;
		d->qid.path = PATH(0, Qtop);
		d->qid.type = QTDIR;
		break;
	case Qmbox:
		d->name = mb->name;
		d->mode = DMDIR|0555;
		d->length = 0;
		d->qid.path = PATH(mb->id, Qmbox);
		d->qid.type = QTDIR;
		d->qid.vers = mb->vers;
		break;
	case Qdir:
		d->name = m->name;
		d->mode = DMDIR|0555;
		d->length = 0;
		d->qid.path = PATH(m->id, Qdir);
		d->qid.type = QTDIR;
		break;
	case Qctl:
		d->name = dirtab[t];
		d->mode = 0666;
		d->atime = d->mtime = time(0);
		d->length = 0;
		d->qid.path = PATH(0, Qctl);
		break;
	case Qheader:
		d->name = dirtab[t];
		cacheheaders(mb, m);
		d->length = readheader(m, hbuf, 0, sizeof hbuf);
		putcache(mb, m);
		break;
	case Qmboxctl:
		d->name = dirtab[t];
		d->mode = 0222;
		d->atime = d->mtime = time(0);
		d->length = 0;
		d->qid.path = PATH(mb->id, Qmboxctl);
		break;
	case Qinfo:
		d->name = dirtab[t];
		d->length = readinfo(mb, m, hbuf, 0, sizeof hbuf);
		d->qid.path = PATH(m->id, t);
		break;
	case Qraw:
		cacheheaders(mb, m);
		p = m->start;
		if(strncmp(m->start, "From ", 5) == 0)
		if(e = strchr(p, '\n'))
			p = e + 1;
		d->name = dirtab[t];
		d->length = m->size - (p - m->start);
		putcache(mb, m);
		break;
	case Qrawbody:
		d->name = dirtab[t];
		d->length = m->rawbsize;
		break;
	case Qrawunix:
		d->name = dirtab[t];
		d->length = m->size;
		if(mb->addfrom && Topmsg(mb, m)){
			cacheheaders(mb, m);
			d->length += strlen(m->unixheader);
			putcache(mb, m);
		}
		break;
	case Qflags:
		d->mode = 0666;
	default:
		d->name = dirtab[t];
		d->length = fileinfo(mb, m, t, &p);
		d->qid.path = PATH(m->id, t);
		break;
	}
}

char*
rversion(Fid*)
{
	Fid *f;

	if(thdr.msize < 256)
		return "max messagesize too small";
	if(thdr.msize < messagesize)
		messagesize = thdr.msize;
	rhdr.msize = messagesize;
	if(strncmp(thdr.version, "9P2000", 6) != 0)
		return "unknown 9P version";
	else
		rhdr.version = "9P2000";
	for(f = fids; f; f = f->next)
		if(f->busy)
			rclunk(f);
	return nil;
}

char*
rauth(Fid*)
{
	return Enoauth;
}

char*
rflush(Fid*)
{
	return 0;
}

char*
rattach(Fid *f)
{
	f->busy = 1;
	f->m = nil;
	f->mb = nil;
	f->qid.path = PATH(0, Qtop);
	f->qid.type = QTDIR;
	f->qid.vers = 0;
	rhdr.qid = f->qid;
	if(strcmp(thdr.uname, user) != 0)
		return Eperm;
	return 0;
}

static Fid*
doclone(Fid *f, int nfid)
{
	Fid *nf;

	nf = newfid(nfid);
	if(nf->busy)
		return nil;
	nf->busy = 1;
	nf->open = 0;
	nf->mb = f->mb;
	if(nf->mb){
		qlock(nf->mb);
		mboxincref(nf->mb);
	}
	if(nf->m = f->m)
		msgincref(gettopmsg(nf->mb, nf->m));
	if(nf->mtop = f->mtop)
		msgincref(nf->mtop);
	nf->qid = f->qid;
	if(nf->mb){
		qunlock(nf->mb);
	}
	return nf;
}

/* slow?  binary search? */
static int
dindex(char *name)
{
	int i;

	for(i = 0; i < Qmax; i++)
		if(dirtab[i] != nil)
		if(strcmp(dirtab[i], name) == 0)
			return i;
	return -1;
}

char*
dowalk(Fid *f, char *name)
{
	char *rv, *p;
	int t, t1;
	Mailbox *mb;
	Hash *h;

	t = FILE(f->qid.path);
	rv = Enotexist;

	qlock(&mbllock);
	if(f->mb)
		qlock(f->mb);

	/* this must catch everything except . and .. */
retry:
	t1 = FILE(f->qid.path);
	if((t1 == Qmbox || t1 == Qdir) && *name >= 'a' && *name <= 'z'){
		h = hlook(f->qid.path, "xxx");		/* sleezy speedup */
		t1 = dindex(name);
		if(t1 == -1)
			h = nil;
	}else
		h = hlook(f->qid.path, name);
	if(h != nil){
		if(f->m)
			msgdecref(f->mb, gettopmsg(f->mb, f->m));
		if(f->mb && f->mb != h->mb){
			qunlock(f->mb);
			mboxdecref(f->mb);
		}
		if(h->mb && h->mb != f->mb)
			qlock(h->mb);
		f->mb = h->mb;
		f->m = h->m;
		if(f->m)
			msgincref(gettopmsg(f->mb, f->m));
		switch(t){
		case Qtop:
			if(f->mb)
				mboxincref(f->mb);
			break;
		case Qmbox:
			if(f->m){
				msgincref(f->m);
				f->mtop = f->m;
			}
			break;
		}
		f->qid = h->qid;
		if(t1 < Qmax)
			f->qid.path = PATH(f->m->id, t1);	/* sleezy speedup */
		rv = nil;
	}else if((p = strchr(name, '.')) != nil && *name != '.'){
		*p = 0;
		goto retry;
	}

	if(f->mb)
		qunlock(f->mb);
	qunlock(&mbllock);

	if(rv == nil)
		return rv;

	if(strcmp(name, ".") == 0)
		return nil;

	if(f->qid.type != QTDIR)
		return Enotdir;

	if(strcmp(name, "..") == 0){
		switch(t){
		case Qtop:
			f->qid.path = PATH(0, Qtop);
			f->qid.type = QTDIR;
			f->qid.vers = 0;
			break;
		case Qmbox:
			f->qid.path = PATH(0, Qtop);
			f->qid.type = QTDIR;
			f->qid.vers = 0;
			mb = f->mb;
			f->mb = nil;
			qlock(&mbllock);
			mboxdecref(mb);
			qunlock(&mbllock);
			break;
		case Qdir:
			qlock(f->mb);
			if(Topmsg(f->mb, f->m)){
				f->qid.path = PATH(f->mb->id, Qmbox);
				f->qid.type = QTDIR;
				f->qid.vers = f->mb->d->qid.vers;
				msgdecref(f->mb, f->mtop);
				msgdecref(f->mb, f->m);
				f->m = f->mtop = nil;
			} else {
				/* refs don't change; still the same message */
				f->m = f->m->whole;
				f->qid.path = PATH(f->m->id, Qdir);
				f->qid.type = QTDIR;
			}
			qunlock(f->mb);
			break;
		}
		rv = nil;
	}
	return rv;
}

char*
rwalk(Fid *f)
{
	Fid *nf;
	char *rv;
	int i;

	if(f->open)
		return Eisopen;

	rhdr.nwqid = 0;
	nf = nil;

	/* clone if requested */
	if(thdr.newfid != thdr.fid){
		nf = doclone(f, thdr.newfid);
		if(nf == nil)
			return "new fid in use";
		f = nf;
	}

	/* if it's just a clone, return */
	if(thdr.nwname == 0 && nf != nil)
		return nil;

	/* walk each element */
	rv = nil;
	for(i = 0; i < thdr.nwname; i++){
		rv = dowalk(f, thdr.wname[i]);
		if(rv != nil){
			if(nf != nil)	
				rclunk(nf);
			break;
		}
		rhdr.wqid[i] = f->qid;
	}
	rhdr.nwqid = i;

	/* we only error out if no walk */
	if(i > 0)
		rv = nil;
	return rv;
}

char*
ropen(Fid *f)
{
	int file;

	if(f->open)
		return Eisopen;
	file = FILE(f->qid.path);
	if(thdr.mode != OREAD)
		if(file != Qctl && file != Qmboxctl && file != Qflags)
			return Eperm;

	/* make sure we've decoded */
	if(file == Qbody){
		qlock(f->mb);
		cachebody(f->mb, f->m);
		decode(f->m);
		convert(f->m);
		putcache(f->mb, f->m);
		qunlock(f->mb);
	}

	rhdr.iounit = 0;
	rhdr.qid = f->qid;
	f->open = 1;
	return 0;
}

char*
rcreate(Fid*)
{
	return Eperm;
}

int
readtopdir(Fid*, uchar *buf, long off, int cnt, int blen)
{
	Dir d;
	int m, n;
	long pos;
	Mailbox *mb;

	qlock(&mbllock);

	n = 0;
	pos = 0;
	mkstat(&d, nil, nil, Qctl);
	m = convD2M(&d, &buf[n], blen);
	if(off <= pos){
		if(m <= BIT16SZ || m > cnt)
			goto out;
		n += m;
		cnt -= m;
	}
	pos += m;
		
	for(mb = mbl; mb != nil; mb = mb->next){
		qlock(mb);
		mkstat(&d, mb, nil, Qmbox);
		qunlock(mb);
		m = convD2M(&d, &buf[n], blen - n);
		if(off <= pos){
			if(m <= BIT16SZ || m > cnt)
				break;
			n += m;
			cnt -= m;
		}
		pos += m;
	}
out:
	qlock(&mbllock);
	return n;
}

int
readmboxdir(Fid *f, uchar *buf, long off, int cnt, int blen)
{
	Dir d;
	int n, m;
	long pos;
	Message *msg;

	qlock(f->mb);
	if(off == 0)
		syncmbox(f->mb, 1);

	n = 0;
	if(f->mb->ctl){
		mkstat(&d, f->mb, nil, Qmboxctl);
		m = convD2M(&d, &buf[n], blen);
		if(off == 0){
			if(m <= BIT16SZ || m > cnt){
				f->fptr = nil;
				goto out;
			}
			n += m;
			cnt -= m;
		} else
			off -= m;
	}

	/* to avoid n**2 reads of the directory, use a saved finger pointer */
	if(f->mb->vers == f->fvers && off >= f->foff && f->fptr != nil){
		msg = f->fptr;
		pos = f->foff;
	} else {
		msg = f->mb->root->part;
		pos = 0;
	}

	for(; cnt > 0 && msg != nil; msg = msg->next){
		/* act like deleted files aren't there */
		if(msg->deleted)
			continue;

		mkstat(&d, f->mb, msg, Qdir);
		m = convD2M(&d, &buf[n], blen - n);
		if(off <= pos){
			if(m <= BIT16SZ || m > cnt)
				break;
			n += m;
			cnt -= m;
		}
		pos += m;
	}

	/* save a finger pointer for next read of the mbox directory */
	f->foff = pos;
	f->fptr = msg;
	f->fvers = f->mb->vers;
out:
	qunlock(f->mb);
	return n;
}

int
readmsgdir(Fid *f, uchar *buf, long off, int cnt, int blen)
{
	Dir d;
	int i, n, m;
	long pos;
	Message *msg;

	qlock(f->mb);
	n = 0;
	pos = 0;
	for(i = 0; i < Qmax; i++){
		mkstat(&d, f->mb, f->m, i);
		m = convD2M(&d, &buf[n], blen - n);
		if(off <= pos){
			if(m <= BIT16SZ || m > cnt)
				goto out;
			n += m;
			cnt -= m;
		}
		pos += m;
	}
	for(msg = f->m->part; msg != nil; msg = msg->next){
		mkstat(&d, f->mb, msg, Qdir);
		m = convD2M(&d, &buf[n], blen - n);
		if(off <= pos){
			if(m <= BIT16SZ || m > cnt)
				break;
			n += m;
			cnt -= m;
		}
		pos += m;
	}
out:
	qunlock(f->mb);
	return n;
}

static int
mboxctlread(Mailbox *mb, char **p)
{
	static char buf[128];

	*p = buf;
	return snprint(*p, sizeof buf, "%s\n%ld\n", mb->path, mb->vers);
}

char*
rread(Fid *f)
{
	char *p;
	int t, i, n, cnt;
	long off;

	rhdr.count = 0;
	off = thdr.offset;
	cnt = thdr.count;
	if(cnt > messagesize - IOHDRSZ)
		cnt = messagesize - IOHDRSZ;
	rhdr.data = (char*)mbuf;

	t = FILE(f->qid.path);
	if(f->qid.type & QTDIR){
		if(t == Qtop)
			n = readtopdir(f, mbuf, off, cnt, messagesize - IOHDRSZ);
		else if(t == Qmbox)
			n = readmboxdir(f, mbuf, off, cnt, messagesize - IOHDRSZ);
		else if(t == Qmboxctl)
			n = 0;
		else
			n = readmsgdir(f, mbuf, off, cnt, messagesize - IOHDRSZ);
		rhdr.count = n;
		return nil;
	}

	qlock(f->mb);
	switch(t){
	case Qctl:
		rhdr.count = 0;
		break;
	case Qmboxctl:
		i = mboxctlread(f->mb, &p);
		goto output;
	case Qheader:
		cacheheaders(f->mb, f->m);
		rhdr.count = readheader(f->m, (char*)mbuf, off, cnt);
		putcache(f->mb, f->m);
		break;
	case Qinfo:
		if(cnt > sizeof mbuf)
			cnt = sizeof mbuf;
		rhdr.count = readinfo(f->mb, f->m, (char*)mbuf, off, cnt);
		break;
	case Qrawunix:
		if(f->mb->addfrom && Topmsg(f->mb, f->m)){
			cacheheaders(f->mb, f->m);
			p = f->m->unixheader;
			if(off < strlen(p)){
				rhdr.count = strlen(p + off);
				memmove(mbuf, p + off, rhdr.count);
				break;
			}
			off -= strlen(p);
			putcache(f->mb, f->m);
		}
	default:
		i = fileinfo(f->mb, f->m, t, &p);
	output:
		if(off < i){
			if(off + cnt > i)
				cnt = i - off;
			if(cnt > sizeof mbuf)
				cnt = sizeof mbuf;
			memmove(mbuf, p + off, cnt);
			rhdr.count = cnt;
		}
		break;
	}
	qunlock(f->mb);
	return nil;
}

char*
modflags(Mailbox *mb, Message *m, char *p)
{
	char *err;
	uchar f;

	f = m->flags;
	if(err = txflags(p, &f))
		return err;
	if(f != m->flags){
		if(mb->modflags != nil)
			mb->modflags(mb, m, f);
		m->flags = f;
		m->cstate |= Cidxstale;
	}
	return nil;
}

char*
rwrite(Fid *f)
{
	char *argvbuf[1024], **argv, file[Pathlen], *err, *v0;
	int i, t, argc, flags;
	Message *m;

	t = FILE(f->qid.path);
	rhdr.count = thdr.count;
	if(thdr.count == 0)
		return Ebadctl;
	if(thdr.data[thdr.count - 1] == '\n')
		thdr.data[thdr.count - 1] = 0;
	else
		thdr.data[thdr.count] = 0;
	argv = argvbuf;
	switch(t){
	case Qctl:
		memset(argvbuf, 0, sizeof argvbuf);
		argc = tokenize(thdr.data, argv, nelem(argvbuf) - 1);
		if(argc == 0)
			return Ebadctl;
		if(strcmp(argv[0], "open") == 0 || strcmp(argv[0], "create") == 0){
			if(argc == 1 || argc > 3)
				return Ebadargs;
			mboxpathbuf(file, sizeof file, getlog(), argv[1]);
			if(argc == 3){
				if(strchr(argv[2], '/') != nil)
					return "/ not allowed in mailbox name";
			}else
				argv[2] = nil;
			flags = 0;
			if(strcmp(argv[0], "create") == 0)
				flags |= DMcreate;
			return newmbox(file, argv[2], flags, nil);
		}
		if(strcmp(argv[0], "close") == 0){
			if(argc < 2)
				return nil;
			for(i = 1; i < argc; i++)
				freembox(argv[i]);
			return nil;
		}
		if(strcmp(argv[0], "delete") == 0){
			if(argc < 3)
				return nil;
			delmessages(argc - 1, argv + 1);
			return nil;
		}
		if(strcmp(argv[0], "flag") == 0){
			if(argc < 3)
				return nil;
			return flagmessages(argc - 1, argv + 1);
		}
		if(strcmp(argv[0], "remove") == 0){
			v0 = argv0;
			flags = 0;
			ARGBEGIN{
			default:
				argv0 = v0;
				return Ebadargs;
			case 'r':
				flags |= Rrecur;
				break;
			case 't':
				flags |= Rtrunc;
				break;
			}ARGEND
			argv0 = v0;
			if(argc == 0)
				return Ebadargs;
			for(; *argv; argv++){
				mboxpathbuf(file, sizeof file, getlog(), *argv);
				if(err = newmbox(file, nil, 0, nil))
					return err;
				if(err = removembox(file, flags))
					return err;
			}
			return 0;
		}
		if(strcmp(argv[0], "rename") == 0){
			v0 = argv0;
			flags = 0;
			ARGBEGIN{
			case 't':
				flags |= Rtrunc;
				break;
			}ARGEND
			argv0 = v0;
			if(argc != 2)
				return Ebadargs;
			return mboxrename(argv[0], argv[1], flags);
		}
		return Ebadctl;
	case Qmboxctl:
		if(f->mb && f->mb->ctl){
			argc = tokenize(thdr.data, argv, nelem(argvbuf));
			if(argc == 0)
				return Ebadctl;
			qlock(f->mb);
			err = f->mb->ctl(f->mb, argc, argv);
			qunlock(f->mb);
			return err;
		}
		break;
	case Qflags:
		/*
		 * modifying flags on subparts is a little strange.
		 */
		if(!f->mb || !f->m)
			break;
		qlock(f->mb);
		m = gettopmsg(f->mb, f->m);
		err = modflags(f->mb, m, thdr.data);
		qunlock(f->mb);
		return err;
	}
	return Eperm;
}

char*
rclunk(Fid *f)
{
	Mailbox *mb;

	f->busy = 1;
	/* coherence(); */
	f->fid = -1;
	f->open = 0;
	mb = f->mb;
	if(mb){
		qlock(mb);
	}
	if(f->mtop)
		msgdecref(mb, f->mtop);
	if(f->m)
		msgdecref(mb, gettopmsg(mb, f->m));
	f->m = f->mtop = nil;
	if(mb){
		f->mb = nil;
		qunlock(mb);
		qlock(&mbllock);
		mboxdecref(mb);
		qunlock(&mbllock);
	}
	f->busy = 0;
	return 0;
}

char *
rremove(Fid *f)
{
	if(f->m != nil){
		qlock(f->mb);
		if(!f->m->deleted)
			mailplumb(f->mb, f->m, 1);
		f->m->deleted = Deleted;
		qunlock(f->mb);
	}
	return rclunk(f);
}

char *
rstat(Fid *f)
{
	Dir d;

	if(f->mb)
		qlock(f->mb);
	if(FILE(f->qid.path) == Qmbox)
		syncmbox(f->mb, 1);
	mkstat(&d, f->mb, f->m, FILE(f->qid.path));
	rhdr.nstat = convD2M(&d, mbuf, messagesize - IOHDRSZ);
	rhdr.stat = mbuf;
	if(f->mb)
		qunlock(f->mb);
	return 0;
}

char*
rwstat(Fid*)
{
	return Eperm;
}

Fid*
newfid(int fid)
{
	Fid *f, *ff;

	ff = 0;
	for(f = fids; f; f = f->next)
		if(f->fid == fid)
			return f;
		else if(!ff && !f->busy)
			ff = f;
	if(ff){
		ff->fid = fid;
		ff->fptr = nil;
		return ff;
	}
	f = emalloc(sizeof *f);
	f->fid = fid;
	f->fptr = nil;
	f->next = fids;
	fids = f;
	return f;
}

void
io(void)
{
	char *err;
	int n;

	/* start a process to watch the mailboxes*/
	if(plumbing || biffing)
		switch(rfork(RFPROC|RFMEM)){
		case -1:
			/* oh well */
			break;
		case 0:
			reader();
			exits("");
		default:
			break;
		}

	for(;;){
		n = read9pmsg(mfd[0], mdata, messagesize);
		if(n <= 0)
			return;
		if(convM2S(mdata, n, &thdr) == 0)
			continue;

		if(Dflag)
			fprint(2, "%s:<-%F\n", argv0, &thdr);

		rhdr.data = (char*)mdata + messagesize;
		if(!fcalls[thdr.type])
			err = "bad fcall type";
		else
			err = fcalls[thdr.type](newfid(thdr.fid));
		if(err){
			rhdr.type = Rerror;
			rhdr.ename = err;
		}else{
			rhdr.type = thdr.type + 1;
			rhdr.fid = thdr.fid;
		}
		rhdr.tag = thdr.tag;
		if(Dflag)
			fprint(2, "%s:->%F\n", argv0, &rhdr);
		n = convS2M(&rhdr, mdata, messagesize);
		if(write(mfd[1], mdata, n) != n)
			error("mount write");
	}
}

static char *readerargv[] = {"upas/fs", "plumbing", 0};

void
reader(void)
{
	ulong t;
	Dir *d;
	Mailbox *mb;

	setname(readerargv);
	sleep(15*1000);
	for(;;){
		t = time(0);
		qlock(&mbllock);
		for(mb = mbl; mb != nil; mb = mb->next){
			if(!canqlock(mb))
				continue;
			if(mb->waketime != 0 && t >= mb->waketime){
				mb->waketime = 0;
				break;
			}
			if(mb->d != nil && mb->d->name != nil){
				d = dirstat(mb->path);
				if(d != nil){
					if(d->qid.path != mb->d->qid.path
					|| d->qid.vers != mb->d->qid.vers){
						free(d);
						break;
					}
					free(d);
				}
			}
			qunlock(mb);
		}
		qunlock(&mbllock);
		if(mb != nil){
			syncmbox(mb, 1);
			qunlock(mb);
		} else
			sleep(15*1000);
	}
}

int
newid(void)
{
	int rv;
	static int id;
	static Lock idlock;

	lock(&idlock);
	rv = ++id;
	unlock(&idlock);

	return rv;
}

void
error(char *s)
{
	syskillpg(getpid());
	eprint("upas/fs: fatal error: %s: %r\n", s);
	exits(s);
}


typedef struct Ignorance Ignorance;
struct Ignorance
{
	Ignorance *next;
	char	*str;
	int	len;
};
Ignorance *ignorance;

/*
 *  read the file of headers to ignore
 */
void
readignore(void)
{
	char *p;
	Ignorance *i;
	Biobuf *b;

	if(ignorance != nil)
		return;

	b = Bopen("/mail/lib/ignore", OREAD);
	if(b == 0)
		return;
	while(p = Brdline(b, '\n')){
		p[Blinelen(b) - 1] = 0;
		while(*p && (*p == ' ' || *p == '\t'))
			p++;
		if(*p == '#')
			continue;
		i = emalloc(sizeof *i);
		i->len = strlen(p);
		i->str = strdup(p);
		if(i->str == 0){
			free(i);
			break;
		}
		i->next = ignorance;
		ignorance = i;
	}
	Bterm(b);
}

int
ignore(char *p)
{
	Ignorance *i;

	readignore();
	for(i = ignorance; i != nil; i = i->next)
		if(cistrncmp(i->str, p, i->len) == 0)
			return 1;
	return 0;
}

int
readheader(Message *m, char *buf, int off, int cnt)
{
	char *s, *end, *se, *p, *e, *to;
	int n, ns, salloc;

	to = buf;
	p = m->header;
	e = m->hend;
	s = emalloc(salloc = 2048);
	end = s + salloc;

	/* copy in good headers */
	while(cnt > 0 && p < e){
		n = hdrlen(p, e);
		assert(n > 0);
		if(ignore(p)){
			p += n;
			continue;
		}
		if(n + 1 > salloc){
			s = erealloc(s, salloc = n + 1);
			end = s + salloc;
		}
		se = rfc2047(s, end, p, n, 0);
		ns = se - s;
		if(off > 0){
			if(ns <= off){
				off -= ns;
				p += n;
				continue;
			}
			ns -= off;
		}
		if(ns > cnt)
			ns = cnt;
		memmove(to, s + off, ns);
		to += ns;
		p += n;
		cnt -= ns;
		off = 0;
	}
	free(s);
	return to - buf;
}

QLock hashlock;

uint
hash(ulong ppath, char *name)
{
	uchar *p;
	uint h;

	h = 0;
	for(p = (uchar*)name; *p; p++)
		h = h*7 + *p;
	h += ppath;

	return h % Hsize;
}

Hash*
hlook(ulong ppath, char *name)
{
	int h;
	Hash *hp;

	qlock(&hashlock);
	h = hash(ppath, name);
	for(hp = htab[h]; hp != nil; hp = hp->next)
		if(ppath == hp->ppath && strcmp(name, hp->name) == 0){
			qunlock(&hashlock);
			return hp;
		}
	qunlock(&hashlock);
	return nil;
}

void
henter(ulong ppath, char *name, Qid qid, Message *m, Mailbox *mb)
{
	int h;
	Hash *hp, **l;

//if(m)sanemsg(m);
	qlock(&hashlock);
	h = hash(ppath, name);
	for(l = &htab[h]; *l != nil; l = &(*l)->next){
		hp = *l;
		if(ppath == hp->ppath && strcmp(name, hp->name) == 0){
			hp->m = m;
			hp->mb = mb;
			hp->qid = qid;
			qunlock(&hashlock);
			return;
		}
	}

	*l = hp = emalloc(sizeof(*hp));
	hp->m = m;
	hp->mb = mb;
	hp->qid = qid;
	hp->name = name;
	hp->ppath = ppath;
	qunlock(&hashlock);
}

void
hfree(ulong ppath, char *name)
{
	int h;
	Hash *hp, **l;

	qlock(&hashlock);
	h = hash(ppath, name);
	for(l = &htab[h]; *l != nil; l = &(*l)->next){
		hp = *l;
		if(ppath == hp->ppath && strcmp(name, hp->name) == 0){
			hp->mb = nil;
			*l = hp->next;
			free(hp);
			break;
		}
	}
	qunlock(&hashlock);
}

int
hashmboxrefs(Mailbox *mb)
{
	int h;
	Hash *hp;
	int refs = 0;

	qlock(&hashlock);
	for(h = 0; h < Hsize; h++)
		for(hp = htab[h]; hp != nil; hp = hp->next)
			if(hp->mb == mb)
				refs++;
	qunlock(&hashlock);
	return refs;
}

void
post(char *name, char *envname, int srvfd)
{
	char buf[32];
	int fd;

	fd = create(name, OWRITE, 0600);
	if(fd < 0)
		error("post failed");
	snprint(buf, sizeof buf, "%d", srvfd);
	if(write(fd, buf, strlen(buf)) != strlen(buf))
		error("srv write");
	close(fd);
	putenv(envname, name);
}
