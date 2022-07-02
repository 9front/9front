#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>

typedef struct Patch Patch;
typedef struct Hunk Hunk;
typedef struct Fbuf Fbuf;

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
	char	*buf;
	int	len;
};

int	strip;
int	reverse;
void	(*addnew)(Hunk*, char*);
void	(*addold)(Hunk*, char*);

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
	if(e == s)
		return -1;
	h->newcnt = 1;
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
addnewfn(Hunk *h, char *ln)
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
addoldfn(Hunk *h, char *ln)
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

Patch*
parse(Biobuf *f, char *name)
{
	char *ln, *old, *new, **oldp, **newp;
	int oldcnt, newcnt, lnum;
	Patch *p;
	Hunk h;

	ln = nil;
	lnum = 0;
	p = emalloc(sizeof(Patch));
	if(!reverse){
		oldp = &old;
		newp = &new;
	}else{
		oldp = &new;
		newp = &old;
	}
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
	if(fileheader(ln, "--- ", oldp) == -1)
		goto comment;
	free(ln);

	if((ln = readline(f, &lnum)) == nil)
		goto out;
	if(fileheader(ln, "+++ ", newp) == -1)
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
			if(oldcnt != h.oldcnt || newcnt != h.newcnt)
				sysfatal("%s:%d: malformed hunk: mismatched counts", name, lnum);
			addhunk(p, &h);
			break;
		}
		switch(ln[0]){
		default:
			sysfatal("%s:%d: malformed hunk: leading junk", name, lnum);
			goto out;
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
		if(strncmp(ln, "--- ", 4) == 0)
			goto patch;
		if(strncmp(ln, "@@ ", 3) == 0)
			goto hunk;
		goto comment;
	}

out:
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
blat(char *old, char *new, char *o, usize len)
{
	char *tmp;
	int fd;

	if(strcmp(new, "/dev/null") == 0){
		if(len != 0)
			sysfatal("diff modifies removed file");
		if(remove(old) == -1)
			sysfatal("removeold %s: %r", old);
		return;
	}
	if(mkpath(new) == -1)
		sysfatal("mkpath %s: %r", new);
	if((tmp = smprint("%s.tmp%d", new, getpid())) == nil)
		sysfatal("smprint: %r");
	if((fd = create(tmp, OWRITE, 0666)) == -1)
		sysfatal("open %s: %r", tmp);
	if(write(fd, o, len) != len)
		sysfatal("write %s: %r", tmp);
	if(strcmp(old, new) == 0 && remove(old) == -1)
		sysfatal("remove %s: %r", old);
	if(rename(fd, new) == -1)
		sysfatal("create %s: %r", new);
	if(close(fd) == -1)
		sysfatal("close %s: %r", tmp);
	free(tmp);
}

int
slurp(Fbuf *f, char *path)
{
	int n, i, fd, sz, len, nlines, linesz;
	char *buf;
	int *lines;

	if((fd = open(path, OREAD)) == -1)
		sysfatal("open %s: %r", path);
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
		if(nlines+1 == linesz){
			linesz *= 2;
			lines = erealloc(lines, linesz*sizeof(int));
		}
		lines[nlines++] = i+1;
	}
	f->len = len;
	f->buf = buf;
	f->lines = lines;
	f->nlines = nlines;
	f->lastln = -1;
	return 0;
}

char*
search(Fbuf *f, Hunk *h, char *fname)
{
	int ln, len, off, fuzz, nfuzz, scanning;

	scanning = 1;
	len = h->oldlen;
	nfuzz = (f->nlines < 250) ? f->nlines : 250;
	for(fuzz = 0; scanning && fuzz <= nfuzz; fuzz++){
		scanning = 0;
		ln = h->oldln - fuzz;
		if(ln > f->lastln){
			off = f->lines[ln];
			if(off + len > f->len)
				continue;
			scanning = 1;
			if(memcmp(f->buf + off, h->old, h->oldlen) == 0){
				f->lastln = ln;
				return f->buf + off;
			}
		}
		ln = h->oldln + fuzz - 1;
		if(ln <= f->nlines){
			off = f->lines[ln];
			if(off + len >= f->len)
				continue;
			scanning = 1;
			if(memcmp(f->buf + off, h->old, h->oldlen) == 0){
				f->lastln = ln;
				return f->buf + off;
			}
		}
	}
	sysfatal("%s:%d: unable to find hunk offset in %s", fname, h->lnum, h->oldpath);
	return nil;
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
	char *o, *s, *e, *curfile;
	int i, osz;
	Hunk *h;
	Fbuf f;

	e = nil;
	o = nil;
	osz = 0;
	curfile = nil;
	for(i = 0; i < p->nhunk; i++){
		h = &p->hunk[i];
		if(curfile == nil || strcmp(curfile, h->newpath) != 0){
			if(slurp(&f, h->oldpath) == -1)
				sysfatal("slurp %s: %r", h->oldpath);
			curfile = h->newpath;
			e = f.buf;
		}
		s = e;
		e = search(&f, h, fname);
		o = append(o, &osz, s, e);
		o = append(o, &osz, h->new, h->new + h->newlen);
		e += h->oldlen;
		if(i+1 == p->nhunk || strcmp(curfile, p->hunk[i+1].newpath) != 0){
			o = append(o, &osz, e, f.buf + f.len);
			blat(h->oldpath, h->newpath, o, osz);
			if(strcmp(h->newpath, "/dev/null") == 0)
				print("%s\n", h->oldpath);
			else
				print("%s\n", h->newpath);
			osz = 0;
		}
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
	fprint(2, "usage: %s [-R] [-p nstrip] [patch...]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	Biobuf *f;
	Patch *p;
	int i;

	ARGBEGIN{
	case 'p':
		strip = atoi(EARGF(usage()));
		break;
	case 'R':
		reverse++;
		break;
	default:
		usage();
		break;
	}ARGEND;

	if(reverse){
		addnew = addoldfn;
		addold = addnewfn;
	}else{
		addnew = addnewfn;
		addold = addoldfn;
	}
	if(argc == 0){
		if((f = Bfdopen(0, OREAD)) == nil)
			sysfatal("open stdin: %r");
		if((p = parse(f, "stdin")) == nil)
			sysfatal("parse patch: %r");
		if(apply(p, "stdin") == -1)
			sysfatal("apply stdin: %r");
		freepatch(p);
		Bterm(f);
	}else{
		for(i = 0; i < argc; i++){
			if((f = Bopen(argv[i], OREAD)) == nil)
				sysfatal("open %s: %r", argv[i]);
			if((p = parse(f, argv[i])) == nil)
				sysfatal("parse patch: %r");
			if(apply(p, argv[i]) == -1)
				sysfatal("apply %s: %r", argv[i]);
			freepatch(p);
			Bterm(f);
		}
	}
	exits(nil);
}
