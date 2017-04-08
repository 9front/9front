#include "common.h"

enum{
	Mbox	= 1,
	Mdir,
};

typedef struct Folder Folder;
struct Folder{
	int	open;
	int	ofd;
	int	type;
	Biobuf	*out;
	Mlock	*l;
	long	t;
};
static Folder ftab[5];

static Folder*
getfolder(Biobuf *out)
{
	int i;
	Folder *f;

	for(i = 0; i < nelem(ftab); i++){
		f = ftab+i;
		if(f->open == 0){
			f->open = 1;
			f->ofd = -1;
			f->type = 0;
			return f;
		}
		if(f->out == out)
			return f;
	}
	sysfatal("folder.c:ftab too small");
	return 0;
}

static int
putfolder(Folder *f)
{
	int r;

	r = 0;
	if(f->l)
		sysunlock(f->l);
	if(f->out){
		r |= Bterm(f->out);
		free(f->out);
	}
	if(f->ofd >= 0)
		close(f->ofd);
	memset(f, 0, sizeof *f);
	return r;
}

static Biobuf*
mboxopen(char *s)
{
	Folder *f;

	f = getfolder(nil);
	f->l = syslock(s);		/* traditional botch: ignore failure */
	if((f->ofd = open(s, OWRITE)) == -1)
	if((f->ofd = create(s, OWRITE|OEXCL, DMAPPEND|0600)) == -1){
		putfolder(f);
		return nil;
	}
	seek(f->ofd, 0, 2);
	f->out = malloc(sizeof *f->out);
	Binit(f->out, f->ofd, OWRITE);
	f->type = Mbox;
	return f->out;
}

/*
 * sync with send/cat_mail.c:/^mdir
 */
static Biobuf*
mdiropen(char *s, long t)
{
	char buf[Pathlen];
	Folder *f;
	int i;

	f = getfolder(nil);
	for(i = 0; i < 100; i++){
		snprint(buf, sizeof buf, "%s/%lud.%.2d.tmp", s, t, i);
		if((f->ofd = create(buf, OWRITE|OEXCL, DMAPPEND|0660)) != -1)
			goto found;
	}
	putfolder(f);
	return nil;
found:
	werrstr("");
	f->out = malloc(sizeof *f->out);
	Binit(f->out, f->ofd, OWRITE);
	f->type = Mdir;
	f->t = t;
	return f->out;
}

Biobuf*
openfolder(char *s, long t)
{
	int isdir;
	Dir *d;

	if(d = dirstat(s)){
		isdir = d->mode&DMDIR;
		free(d);
	}else{
		isdir = create(s, OREAD, DMDIR|0777);
		if(isdir == -1)
			return nil;
		close(isdir);
		isdir = 1;
	}
	if(isdir)
		return mdiropen(s, t);
	else
		return mboxopen(s);
}

int
closefolder(Biobuf *b)
{
	char buf[32];
	Folder *f;
	Dir d;
	int i;

	if(b == nil)
		return 0;
	f = getfolder(b);
	if(f->type != Mdir)
		return putfolder(f);
	if(Bflush(b) == 0){
		for(i = 0; i < 100; i++){
			nulldir(&d);
			snprint(buf, sizeof buf, "%lud.%.2d", f->t, i);
			d.name = buf;
			if(dirfwstat(f->ofd, &d) > 0)
				return putfolder(f);
		}
	}
	putfolder(f);
	return -1;
}

/*
 * escape "From " at the beginning of a line;
 * translate \r\n to \n for imap
 */
static int
mboxesc(Biobuf *in, Biobuf *out, int type)
{
	char *s;
	int n;

	for(; s = Brdstr(in, '\n', 0); free(s)){
		if(!strncmp(s, "From ", 5))
			Bputc(out, ' ');
		n = strlen(s);
		if(n > 1 && s[n-2] == '\r'){
			s[n-2] = '\n';
			n--;
		}
		if(Bwrite(out, s, n) == Beof){
			free(s);
			return -1;
		}
		if(s[n-1] != '\n')
			Bputc(out, '\n');
	}
	if(type == Mbox)
		Bputc(out, '\n');
	if(Bflush(out) == Beof)
		return -1;
	return 0;
}

int
appendfolder(Biobuf *b, char *addr, int fd)
{
	char *s;
	int r;
	Biobuf bin;
	Folder *f;
	Tm tm;

	f = getfolder(b);
	Bseek(f->out, 0, 2);
	Binit(&bin, fd, OREAD);
	s = Brdstr(&bin, '\n', 0);
	if(!s || strncmp(s, "From ", 5))
		Bprint(f->out, "From %s %.28s\n", addr, ctime(f->t));
	else if(fromtotm(s, &tm) >= 0)
		f->t = tm2sec(&tm);
	if(s)
		Bwrite(f->out, s, strlen(s));
	free(s);
	r = mboxesc(&bin, f->out, f->type);
	return r | Bterm(&bin);
}

int
fappendfolder(char *addr, long t, char *s, int fd)
{
	Biobuf *b;
	int r;

	b = openfolder(s, t);
	if(b == nil)
		return -1;
	r = appendfolder(b, addr, fd);
	r |= closefolder(b);
	return r;
}

/*
 * BOTCH sync with ../imap4d/mbox.c:/^okmbox
 */

static char *specialfile[] =
{
	"L.mbox",
	"forward",
	"headers",
	"imap.subscribed",
	"names",
	"pipefrom",
	"pipeto",
};

static int
special(char *s)
{
	char *p;
	int i;

	p = strrchr(s, '/');
	if(p == nil)
		p = s;
	else
		p++;
	for(i = 0; i < nelem(specialfile); i++)
		if(strcmp(p, specialfile[i]) == 0)
			return 1;
	return 0;
}

static char*
mkmbpath(char *s, int n, char *user, char *mb, char *path)
{
	char *p, *e, *r, buf[Pathlen];

	if(!mb)
		return mboxpathbuf(s, n, user, path);
	e = buf+sizeof buf;
	p = seprint(buf, e, "%s", mb);
	if(r = strrchr(buf, '/'))
		p = r;
	seprint(p, e, "/%s", path);
	return mboxpathbuf(s, n, user, buf);
}


/*
 * fancy processing for ned:
 * we default to storing in $mail/f then just in $mail.
 */
char*
ffoldername(char *mb, char *user, char *rcvr)
{
	char *p;
	int c, n;
	Dir *d;
	static char buf[Pathlen];

	d = dirstat(mkmbpath(buf, sizeof buf, user, mb, "f/"));
	n = strlen(buf);
	if(!d ||  d->qid.type != QTDIR)
		buf[n -= 2] = 0;
	free(d);

	if(p = strrchr(rcvr, '!'))
		rcvr = p+1;
	while(n < sizeof buf-1 && (c = *rcvr++)){
		if(c== '@')
			break;
		if(c == '/')
			c = '_';
		buf[n++] = c;
	}
	buf[n] = 0;

	if(special(buf)){
		fprint(2, "!won't overwrite %s\n", buf);
		return nil;
	}
	return buf;
}

char*
foldername(char *mb, char *user, char *path)
{
	static char buf[Pathlen];

	mkmbpath(buf, sizeof buf, user, mb, path);
	if(special(buf)){
		fprint(2, "!won't overwrite %s\n", buf);
		return nil;
	}
	return buf;
}

static int
append(Biobuf *in, Biobuf *out)
{
	char *buf;
	int n, m;

	buf = malloc(8192);
	for(;;){
		m = 0;
		n = Bread(in, buf, 8192);
		if(n <= 0)
			break;
		m = Bwrite(out, buf, n);
		if(m != n)
			break;
	}
	if(m != n)
		n = -1;
	else
		n = 1;
	free(buf);
	return n;
}

/* symmetry for nedmail; misnamed */
int
fappendfile(char*, char *target, int in)
{
	int fd, r;
	Biobuf bin, out;

	if((fd = create(target, ORDWR|OEXCL, 0666)) == -1)
		return -1;
	Binit(&out, fd, OWRITE);
	Binit(&bin, in, OREAD);
	r = append(&bin, &out);
	Bterm(&bin);
	Bterm(&out);
	close(fd);
	return r;
}
