#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>

typedef struct Patch Patch;
typedef struct Hunk Hunk;
typedef struct Fbuf Fbuf;
typedef struct Fchg Fchg;

struct Patch {
	char	*name;
	Hunk	*hunk;
	usize	nhunk;
};

struct Hunk {
	int	lnum;

	char	*oldpath;
	int	oldln;
	int	oldcnt;
	int	oldlen;
	int	oldsz;
	char	*old;

	char	*newpath;
	int	newln;
	int	newcnt;
	int	newlen;
	int	newsz;
	char	*new;
};

struct Fbuf {
	int	*lines;
	int	nlines;
	int	lastln;
	int	lastfuzz;
	char	*buf;
	int	len;
	int	mode;
};

struct Fchg {
	char	*tmp;
	char	*old;
	char	*new;
};

int	strip;
int	reverse;
Fchg	*changed;
int	nchanged;
int	dryrun;

char*
readline(Biobuf *f, int *lnum)
{
	char *ln;

	if((ln = Brdstr(f, '\n', 0)) == nil)
		return nil;
	*lnum += 1;
	return ln;
}

void *
emalloc(ulong n)
{
	void *v;
	
	v = mallocz(n, 1);
	if(v == nil)
		sysfatal("malloc: %r");
	setmalloctag(v, getcallerpc(&n));
	return v;
}

void *
erealloc(void *v, ulong n)
{
	if(n == 0)
		n++;
	v = realloc(v, n);
	if(v == nil)
		sysfatal("malloc: %r");
	setmalloctag(v, getcallerpc(&n));
	return v;
}

int
fileheader(char *s, char *pfx, char **name)
{
	int len, n, nnull;
	char *e;

	if((strncmp(s, pfx, strlen(pfx))) != 0)
		return -1;
	for(s += strlen(pfx); *s; s++)
		if(!isspace(*s))
			break;
	for(e = s; *e; e++)
		if(isspace(*e))
			break;
	if(s == e)
		return -1;
	nnull = strlen("/dev/null");
	if((e - s) != nnull || strncmp(s, "/dev/null", nnull) != 0){
		n = strip;
		while(s != e && n > 0){
			while(s != e && *s == '/')
				s++;
			while(s != e && *s != '/')
				s++;
			n--;
		}
		while(*s == '/')
			s++;
		if(*s == '\0')
			sysfatal("too many components stripped");
	}
	len = (e - s) + 1;
	*name = emalloc(len);
	strecpy(*name, *name + len, s);
	return 0;
}

int
hunkheader(Hunk *h, char *s, char *oldpath, char *newpath, int lnum)
{
	char *e;

	memset(h, 0, sizeof(*h));
	h->lnum = lnum;
	h->oldpath = strdup(oldpath);
	h->newpath = strdup(newpath);
	h->oldlen = 0;
	h->oldsz = 32;
	h->old = emalloc(h->oldsz);
	h->newlen = 0;
	h->newsz = 32;
	h->new = emalloc(h->newsz);
	if(strncmp(s, "@@ -", 4) != 0)
		return -1;
	e = s + 4;
	h->oldln = strtol(e, &e, 10);
	h->oldcnt = 1;
	if(*e == ','){
		e++;
		h->oldcnt = strtol(e, &e, 10);
	}
	while(*e == ' ' || *e == '\t')
		e++;
	if(*e != '+')
		return -1;
	e++;
	h->newln = strtol(e, &e, 10);
	h->newcnt = 1;
	if(e == s)
		return -1;
	if(*e == ','){
		e++;
		h->newcnt = strtol(e, &e, 10);
	}
	if(e == s || *e != ' ')
		return -1;
	if(strncmp(e, " @@", 3) != 0)
		return -1;
	/*
	 * empty files have line number 0: keep that,
	 * otherwise adjust down.
	 */
	if(h->oldln > 0)
		h->oldln--;
	if(h->newln > 0)
		h->newln--;
	if(h->oldln < 0 || h->newln < 0 || h->oldcnt < 0 || h->newcnt < 0)
		sysfatal("malformed hunk %s", s);
	return 0;
}

void
addnew(Hunk *h, char *ln)
{
	int n;

	ln++;
	n = strlen(ln);
	while(h->newlen + n >= h->newsz){
		h->newsz *= 2;
		h->new = erealloc(h->new, h->newsz);
	}
	memcpy(h->new + h->newlen, ln, n);
	h->newlen += n;
}

void
addold(Hunk *h, char *ln)
{
	int n;

	ln++;
	n = strlen(ln);
	while(h->oldlen + n >= h->oldsz){
		h->oldsz *= 2;
		h->old = erealloc(h->old, h->oldsz);
	}
	memcpy(h->old + h->oldlen, ln, n);
	h->oldlen += n;
}

int
addmiss(Hunk *h, char *ln, int *nold, int *nnew)
{
	if(ln == nil)
		return 1;
	else if(ln[0] != '-' && ln[0] != '+')
		return 0;
	if(ln[0] == '-'){
		addold(h, ln);
		*nold += 1;
	}else{
		addnew(h, ln);
		*nnew += 1;
	}
	return 1;
}

void
addhunk(Patch *p, Hunk *h)
{
	p->hunk = erealloc(p->hunk, ++p->nhunk*sizeof(Hunk));
	p->hunk[p->nhunk-1] = *h;
}

int
hunkcmp(void *a, void *b)
{
	int c;

	c = strcmp(((Hunk*)a)->oldpath, ((Hunk*)b)->oldpath);
	if(c != 0)
		return c;
	return ((Hunk*)a)->oldln - ((Hunk*)b)->oldln;
}

void
swapint(int *a, int *b)
{
	int t;

	t = *a;
	*a = *b;
	*b = t;
}

void
swapstr(char **a, char **b)
{
	char *t;

	t = *a;
	*a = *b;
	*b = t;
}

void
trimhunk(char c, Hunk *h)
{
	if((c == ' ' || c == '-') && h->oldlen > 0 && h->old[h->oldlen-1] == '\n'){
		h->oldcnt--;
		h->oldlen--;
	}
	if((c == ' ' || c == '+') && h->newlen > 0 && h->new[h->newlen-1] == '\n'){
		h->newcnt--;
		h->newlen--;
	}
}

Patch*
parse(Biobuf *f, char *name)
{
	char *ln, *old, *new, c;
	int i, oldcnt, newcnt, lnum;
	Patch *p;
	Hunk h, *ph;

	ln = nil;
	lnum = 0;
	p = emalloc(sizeof(Patch));
comment:
	free(ln);
	while((ln = readline(f, &lnum)) != nil){
		if(strncmp(ln, "--- ", 4) == 0)
			goto patch;
		free(ln);
	}
	if(p->nhunk == 0)
		sysfatal("%s: could not find start of patch", name);
	goto out;

patch:
	if(fileheader(ln, "--- ", &old) == -1)
		goto comment;
	free(ln);

	if((ln = readline(f, &lnum)) == nil)
		goto out;
	if(fileheader(ln, "+++ ", &new) == -1)
		goto comment;
	free(ln);

	if((ln = readline(f, &lnum)) == nil)
		goto out;
hunk:
	oldcnt = 0;
	newcnt = 0;
	if(hunkheader(&h, ln, old, new, lnum) == -1)
		goto comment;
	free(ln);

	while(1){
		if((ln = readline(f, &lnum)) == nil){
			if(oldcnt != h.oldcnt)
				sysfatal("%s:%d: malformed hunk: mismatched -hunk size %d != %d", name, lnum, oldcnt, h.oldcnt);
			if(newcnt != h.newcnt)
				sysfatal("%s:%d: malformed hunk: mismatched +hunk size %d != %d", name, lnum, newcnt, h.newcnt);
			addhunk(p, &h);
			break;
		}
		c = ln[0];
		switch(ln[0]){
		default:
			sysfatal("%s:%d: malformed hunk: leading junk", name, lnum);
		case '\\':
			if(strncmp(ln, "\\ No newline", nelem("\\ No newline")-1) == 0)
				trimhunk(c, &h);
			/* ignore unknown directives */
			break;
		case '-':
			addold(&h, ln);
			oldcnt++;
			break;
		case '+':
			addnew(&h, ln);
			newcnt++;
			break;
		case '\n':
			addold(&h, " \n");
			addnew(&h, " \n");
			oldcnt++;
			newcnt++;
			break;
		case ' ':
			addold(&h, ln);
			addnew(&h, ln);
			oldcnt++;
			newcnt++;
			break;
		}
		free(ln);
		if(oldcnt > h.oldcnt || newcnt > h.newcnt)
			sysfatal("%s:%d: malformed hunk: oversized hunk", name, lnum);
		if(oldcnt < h.oldcnt || newcnt < h.newcnt)
			continue;

		addhunk(p, &h);
		if((ln = readline(f, &lnum)) == nil)
			goto out;
		if(strncmp(ln, "\\ No newline", nelem("\\ No newline")-1) == 0)
			trimhunk(c, &p->hunk[p->nhunk-1]);
		if(strncmp(ln, "--- ", 4) == 0)
			goto patch;
		if(strncmp(ln, "@@ ", 3) == 0)
			goto hunk;
		goto comment;
	}

out:
	if(reverse){
		for(i = 0; i < p->nhunk; i++){
			ph = &p->hunk[i];
			swapint(&ph->oldln, &ph->newln);
			swapint(&ph->oldcnt, &ph->newcnt);
			swapint(&ph->oldlen, &ph->newlen);
			swapint(&ph->oldsz, &ph->newsz);
			swapstr(&ph->oldpath, &ph->newpath);
			swapstr(&ph->old, &ph->new);
		}
	}
	qsort(p->hunk, p->nhunk, sizeof(Hunk), hunkcmp);
	free(old);
	free(new);
	free(ln);
	return p;
}

int
rename(int fd, char *name)
{
	Dir st;
	char *p;

	nulldir(&st);
	if((p = strrchr(name, '/')) == nil)
		st.name = name;
	else
		st.name = p + 1;
	return dirfwstat(fd, &st);
}

int
mkpath(char *path)
{
	char *p, buf[ERRMAX];
	int f;
	
	if(*path == '\0')
		return 0;
	for(p = strchr(path+1, '/'); p != nil; p = strchr(p+1, '/')){
		*p = '\0';
		if(access(path, AEXIST) != 0){
			if((f = create(path, OREAD, DMDIR | 0777)) == -1){
				rerrstr(buf, sizeof(buf));
				if(strstr(buf, "exist") == nil)
					return -1;
			}
			close(f);
		}
		*p = '/';
	}
	return 0;
}

void
blat(char *old, char *new, char *o, usize len, int mode)
{
	char *tmp;
	int fd;

	tmp = nil;
	if(strcmp(new, "/dev/null") == 0){
		if(len != 0)
			sysfatal("diff modifies removed file");
	}else if(!dryrun){
		if(mkpath(new) == -1)
			sysfatal("mkpath %s: %r", new);
		if((tmp = smprint("%s.tmp%d", new, getpid())) == nil)
			sysfatal("smprint: %r");
		if((fd = create(tmp, OWRITE, mode|0200)) == -1)
			sysfatal("open %s: %r", tmp);
		if(write(fd, o, len) != len)
			sysfatal("write %s: %r", tmp);
		close(fd);
	}
	if((changed = realloc(changed, (nchanged+1)*sizeof(Fchg))) == nil)
		sysfatal("realloc: %r");
	if((changed[nchanged].new = strdup(new)) == nil)
		sysfatal("strdup: %r");
	if((changed[nchanged].old = strdup(old)) == nil)
		sysfatal("strdup: %r");
	changed[nchanged].tmp = tmp;
	nchanged++;
}

void
finish(int ok)
{
	Fchg *c;
	int i, fd;

	for(i = 0; i < nchanged; i++){
		c = &changed[i];
		if(!ok){
			if(c->tmp != nil && remove(c->tmp) == -1)
				fprint(2, "remove %s: %r\n", c->tmp);
			goto Free;
		}
		if(!dryrun){
			if(strcmp(c->new, "/dev/null") == 0){
				if(remove(c->old) == -1)
					sysfatal("remove %s: %r", c->old);
				goto Print;
			}
			if((fd = open(c->tmp, ORDWR)) == -1)
				sysfatal("open %s: %r", c->tmp);
			if(strcmp(c->old, c->new) == 0 && remove(c->old) == -1)
				sysfatal("remove %s: %r", c->old);
			if(rename(fd, c->new) == -1)
				sysfatal("create %s: %r", c->new);
			if(close(fd) == -1)
				sysfatal("close %s: %r", c->tmp);
		}
Print:
		if(strcmp(c->new, "/dev/null") == 0)
			print("%s\n", c->old);
		else
			print("%s\n", c->new);
Free:
		free(c->tmp);
		free(c->old);
		free(c->new);
	}
	free(changed);
}

void
slurp(Fbuf *f, char *path)
{
	int n, i, fd, sz, len, nlines, linesz;
	char *buf;
	int *lines;
	Dir *d;

	if((fd = open(path, OREAD)) == -1)
		sysfatal("open %s: %r", path);
	if((d = dirfstat(fd)) == nil)
		sysfatal("stat %s: %r", path);
	sz = 8192;
	len = 0;
	buf = emalloc(sz);
	while(1){
		if(len == sz){
			sz *= 2;
			buf = erealloc(buf, sz);
		}
		n = read(fd, buf + len, sz - len);
		if(n == 0)
			break;
		if(n == -1)
			sysfatal("read %s: %r", path);
		len += n;
	}

	nlines = 0;
	linesz = 32;
	lines = emalloc(linesz*sizeof(int));
	lines[nlines++] = 0;
	for(i = 0; i < len; i++){
		if(buf[i] != '\n')
			continue;
		if(nlines+2 == linesz){
			linesz *= 2;
			lines = erealloc(lines, linesz*sizeof(int));
		}
		lines[nlines++] = i+1;
	}
	lines[nlines] = len;
	f->len = len;
	f->buf = buf;
	f->lines = lines;
	f->nlines = nlines;
	f->lastln = 0;
	f->lastfuzz = 0;
	f->mode = d->mode;
	free(d);
	close(fd);
}

char*
searchln(Fbuf *f, Hunk *h, int ln)
{
	int off;

	off = f->lines[ln];
	if(off + h->oldlen > f->len)
		return nil;
	if(memcmp(f->buf + off, h->old, h->oldlen) != 0)
		return nil;
	f->lastln = ln + h->oldcnt;
	f->lastfuzz = ln - h->oldln;
	return f->buf + off;
}

char*
search(Fbuf *f, Hunk *h, char *fname)
{
	int ln, oldln, fuzz, scanning;
	char *p;

	oldln = h->oldln + f->lastfuzz;
	if(oldln + h->oldcnt > f->nlines)
		oldln = f->nlines - h->oldcnt;
	scanning = oldln >= f->lastln;
	for(fuzz = 0; scanning && fuzz < 250; fuzz++){
		scanning = 0;
		ln = oldln - fuzz;
		if(ln >= f->lastln){
			scanning = 1;
			if((p = searchln(f, h, ln)) != nil)
				return p;
		}
		ln = oldln + fuzz + 1;
		if(ln + h->oldcnt <= f->nlines){
			scanning = 1;
			if((p = searchln(f, h, ln)) != nil)
				return p;
		}
	}
	sysfatal("%s:%d: unable to find hunk offset in %s", fname, h->lnum, h->oldpath);
}

char*
append(char *o, int *sz, char *s, char *e)
{
	int n;

	n = (e - s);
	o = erealloc(o, *sz + n);
	memcpy(o + *sz, s, n);
	*sz += n;
	return o;
}

int
apply(Patch *p, char *fname)
{
	char *o, *s, *e, *curfile, *nextfile;
	int i, osz;
	Hunk *h, *prevh;
	Fbuf f;

	e = nil;
	o = nil;
	osz = 0;
	curfile = nil;
	h = nil;
	prevh = nil;
	for(i = 0; i < p->nhunk; i++){
		h = &p->hunk[i];
		if(strcmp(h->newpath, "/dev/null") == 0)
			nextfile = h->oldpath;
		else
			nextfile = h->newpath;
		if(curfile == nil || strcmp(curfile, nextfile) != 0){
			if(curfile != nil){
				if(!dryrun)
					o = append(o, &osz, e, f.buf + f.len);
				blat(prevh->oldpath, prevh->newpath, o, osz, f.mode);
				osz = 0;
			}
			if(!dryrun){
				slurp(&f, h->oldpath);
				e = f.buf;
			}
			curfile = nextfile;
		}
		if(!dryrun){
			s = e;
			e = search(&f, h, fname);
			o = append(o, &osz, s, e);
			o = append(o, &osz, h->new, h->new + h->newlen);
			e += h->oldlen;
		}
		prevh = h;
	}
	if(curfile != nil){
		if(!dryrun)
			o = append(o, &osz, e, f.buf + f.len);
		blat(h->oldpath, h->newpath, o, osz, f.mode);
	}
	free(o);
	return 0;
}

void
freepatch(Patch *p)
{
	Hunk *h;
	int i;

	for(i = 0; i < p->nhunk; i++){
		h = &p->hunk[i];
		free(h->oldpath);
		free(h->newpath);
		free(h->old);
		free(h->new);
	}
	free(p->hunk);
	free(p->name);
	free(p);
}

void
usage(void)
{
	fprint(2, "usage: %s [-nR] [-p nstrip] [patch...]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	Biobuf *f;
	Patch *p;
	int i, ok;

	ARGBEGIN{
	case 'p':
		strip = atoi(EARGF(usage()));
		break;
	case 'n':
		dryrun++;
		break;
	case 'R':
		reverse++;
		break;
	default:
		usage();
		break;
	}ARGEND;

	ok = 1;
	if(argc == 0){
		if((f = Bfdopen(0, OREAD)) == nil)
			sysfatal("open stdin: %r");
		if((p = parse(f, "stdin")) == nil)
			sysfatal("parse patch: %r");
		if(apply(p, "stdin") == -1){
			fprint(2, "apply stdin: %r\n");
			ok = 0;
		}
		freepatch(p);
		Bterm(f);
	}else{
		for(i = 0; i < argc; i++){
			if((f = Bopen(argv[i], OREAD)) == nil)
				sysfatal("open %s: %r", argv[i]);
			if((p = parse(f, argv[i])) == nil)
				sysfatal("parse patch: %r");
			if(apply(p, argv[i]) == -1){
				fprint(2, "apply %s: %r\n", argv[i]);
				ok = 0;
			}
			freepatch(p);
			Bterm(f);
		}
	}
	finish(ok);
	exits(nil);
}
