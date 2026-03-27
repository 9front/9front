#include <u.h>
#include <libc.h>
#include "git.h"

typedef struct Idxed	Idxed;
typedef struct Idxent	Idxent;

#define NCACHE 4096

enum {
	Rflg	= 1 << 0,
	Mflg	= 1 << 1,
	Aflg	= 1 << 2,
	Uflg	= 1 << 3,
	/* everything after this is not an error */
	Tflg	= 1 << 4,
};

struct Idxed {
	char**	cache;
	int	n;
	int	max;
};

Idxed	idxtab[NCACHE];
char	repopath[1024];
char	wdirpath[1024];
char	relapath[1024];
char	*rstr	= "R ";
char	*mstr	= "M ";
char	*astr	= "A ";
char	*ustr	= "U ";
char	*tstr 	= "T ";
char	*bdir = ".git/fs/HEAD/tree";
int	nslash;
int	nrel;
int	quiet;
int	dirty;
int	isindexed = 1;
int	intree;
int	printflg;

Idxent	*idx;
int	idxsz;
int	nidx;

int	cleanidx = 0;	/* skip tree check for checkedin() */
int	staleidx = 0;

Idxent	*wdir;
int	wdirsz;
int	nwdir;

void	loadwdir(char*);

int
checkedin(Idxent *e, int change)
{
	char *p;
	int r;

	/* clean index, no need to check tree */
	if(cleanidx)
		return e >= &idx[0] && e < &idx[nidx] && e->state != 'R';

	if((p = smprint("%s/%s", bdir, e->path)) == nil)
		sysfatal("smprint: %r");

	r = access(p, AEXIST);
	if(r == 0 && change){
		if(e->state != 'R')
			e->state = 'T';
		staleidx = 1;
	}
	free(p);

	return r == 0;
}

/*
 * Tricky; we want to know if a file or dir is indexed,
 * but a dir is only indexed if we have a file with dir/
 * listed in the index.
 *
 * as a result, we need to add a virtual '/' at the end
 * of the path if we're doing it, so if we have:
 *	foo.bar/x
 *	foo/y
 * and we want to find out if foo is a directory we should
 * descend into, we need to compare as though foo/ ended
 * with a '/', or we'll bsearch down do foo.bar, not foo.
 *
 * this code resembles entcmp() in util.c, but differs
 * because we're comparing whole paths.
 */
int
pathcmp(char *sa, char *sb, int sadir)
{
	unsigned a, b;

	while(1){
		a = *sa++;
		b = *sb++;
		if(a != b){
			if(a == 0 && sadir)
				a = '/';
			if(a == '/' && b == '/')
				return 0;
			return (a > b) ? 1 : -1;
		}
		if(a == 0)
			return 0;
	}
}

int
indexed(char *path, int dir)
{
	int lo, hi, mid, r;

	r = -1;
	lo = 0;
	hi = nidx-1;
	while(lo <= hi){
		mid = (hi + lo) / 2;
		r = pathcmp(path, idx[mid].path, dir);
		if(r < 0)
			hi = mid-1;
		else if(r > 0)
			lo = mid+1;
		else
			break;
	}
	return r == 0;
}

int
idxcmp(void *pa, void *pb)
{
	Idxent *a, *b;
	int c;

	a = (Idxent*)pa;
	b = (Idxent*)pb;
	if((c = strcmp(a->path, b->path)) != 0)
		return c;
	/* maintain load order if name is identical */
	return a->order < b->order ? -1 : 1;
}

/*
 * compares whether the indexed entry 'a'
 * has the same contents and mode as
 * the entry on disk 'b'; if the indexed
 * entry is nil, does a deep comparison
 * of the checked out file and the file
 * checked in.
 */
int
samedata(Idxent *a, Idxent *b)
{
	char *gitpath, ba[IOUNIT], bb[IOUNIT];
	int fa, fb, na, nb, same;
	Dir *da, *db;

	if(a != nil){
		if(a->qid.path == b->qid.path
		&& a->qid.vers == b->qid.vers
		&& a->qid.type == b->qid.type
		&& a->mode == b->mode
		&& a->mode != 0)
			return 1;
	}

	same = 0;
	da = nil;
	db = nil;
	if((gitpath = smprint("%s/%s", bdir, b->path)) == nil)
		sysfatal("smprint: %r");
	fa = open(gitpath, OREAD);
	fb = open(b->path, OREAD);
	if(fa == -1 || fb == -1)
		goto mismatch;
	da = dirfstat(fa);
	db = dirfstat(fb);
	if(da == nil || db == nil)
		goto mismatch;
	if((da->mode&0100) != (db->mode&0100))
		goto mismatch;
	if(da->length != db->length)
		goto mismatch;
	while(1){
		if((na = readn(fa, ba, sizeof(ba))) == -1)
			goto mismatch;
		if((nb = readn(fb, bb, sizeof(bb))) == -1)
			goto mismatch;
		if(na != nb)
			goto mismatch;
		if(na == 0)
			break;
		if(memcmp(ba, bb, na) != 0)
			goto mismatch;
	}
	if(a != nil){
		a->qid = db->qid;
		a->mode = db->mode;
		staleidx = 1;
	}
	same = 1;

mismatch:
	free(da);
	free(db);
	if(fa != -1)
		close(fa);
	if(fb != -1)
		close(fb);
	return same;
}

void
loadent(char *dir, Dir *d, int fullpath)
{
	char *path;
	Idxent *e;

	if(fullpath)
		path = estrdup(dir);
	else if((path = smprint("%s/%s", dir, d->name)) == nil)
		sysfatal("smprint: %r");

	cleanname(path);
	if(!intree && (printflg & Uflg) == 0 && !indexed(path, d->qid.type & QTDIR)){
		free(path);
		return;
	}
	if(d->qid.type & QTDIR){
		loadwdir(path);
		free(path);
	}else{
		if(nwdir == wdirsz){
			wdirsz += wdirsz/2;
			wdir = erealloc(wdir, wdirsz*sizeof(Idxent));
		}
		e = wdir + nwdir;
		e->path = path;
		e->qid = !intree? d->qid : (Qid){-1,-1,-1};
		e->mode = d->mode;
		e->order = nwdir;
		e->state = 'T';
		nwdir++;
	}
}

void
loadwdir(char *path)
{
	int fd, i, n;
	Dir *d, *e;

	d = nil;
	e = nil;
	cleanname(path);
	if(!intree
	&& strncmp(path, ".git", 4) == 0
	&& (path[4] == '/' || path[4] == 0))
		return;

	if((fd = open(path, OREAD)) < 0)
		goto error;
	if((e = dirfstat(fd)) == nil)
		fprint(2, "fstat: %r");
	if(e->qid.type & QTDIR)
		while((n = dirread(fd, &d)) > 0){
			for(i = 0; i < n; i++)
				loadent(path, &d[i], 0);
			free(d);
		}
	else
		loadent(path, e, 1);
error:
	free(e);
	if(fd != -1)
		close(fd);
}

int
pfxmatch(char *p, char **pfx, int *pfxlen, int npfx)
{
	int i;

	if(p == nil)
		return 0;
	if(npfx == 0)
		return 1;
	for(i = 0; i < npfx; i++){
		if(strncmp(p, pfx[i], pfxlen[i]) != 0)
			continue;
		if(p[pfxlen[i]] == '/' || p[pfxlen[i]] == 0)
			return 1;
		if(strcmp(pfx[i], ".") == 0 || *pfx[i] == 0)
			return 1;
	}
	return 0;
}


char*
reporel(char *s)
{
	char *p;
	int n;

	if(*s == '/')
		s = estrdup(s);
	else if((s = smprint("%s/%s", wdirpath, s)) == nil)
		sysfatal("smprint: %r");

	p = cleanname(s);
	n = strlen(repopath);
	if(strncmp(s, repopath, n) != 0)
		sysfatal("path outside repo: %s", s);
	p += n;
	if(*p == '/')
		p++;
	memmove(s, p, strlen(p)+1);
	return s;
}

void
show(Biobuf *o, int flg, char *str, char *path)
{
	char *pa, *pb;
	int n;

	dirty |= flg;
	if(!quiet && (printflg & flg)){
		Bprint(o, str);
		n = nslash;
		if(n){
			for(pa = relapath, pb = path; *pa && *pb; pa++, pb++){
				if(*pa != *pb)
					break;
				if(*pa == '/'){
					n--;
					path = pb+1;
				}
			}
			while(n-- > 0)
				Bprint(o, "../");
		}
		Bprint(o, "%s\n", path);
	}
}

void
findslashes(char *path)
{
	char *p;

	p = cleanname(path);
	if(p[0] == '.'){
		if(p[1] == '\0')
			return;
		else if(p[1] == '.' && (p[2] == '/' || p[2] == '\0'))
			sysfatal("relative path escapes git root");
	}
	
	snprint(relapath, sizeof relapath, "%s/", p);
	p = relapath;
	if(*p == '/')
		p++;

	for(; *p; p++)
		if(*p == '/')
			nslash++;
}

void
usage(void)
{
	fprint(2, "usage: %s [-qbcI] [-f filt] [-b base] [paths...]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *p, *e, *ln, *base, **argrel, *parts[4], xbuf[8];
	int i, j, c, line, wfd, *argn;
	Biobuf *f, *o, *w;
	Hash h;
	Dir rn;

	gitinit(repopath, sizeof(repopath), &nrel);
	if(getwd(wdirpath, sizeof(wdirpath)) == nil)
		sysfatal("getwd: %r");
	if(chdir(repopath) == -1)
		sysfatal("chdir: %r");
	if(access(".git/fs/ctl", AEXIST) != 0)
		sysfatal("no running git/fs");
	ARGBEGIN{
	case 'q':
		quiet++;
		break;
	case 'c':
		rstr = "";
		tstr = "";
		mstr = "";
		astr = "";
		ustr = "";
		break;
	case 'f':
		for(p = EARGF(usage()); *p; p++)
			switch(*p){
			case 'T':	printflg |= Tflg;	break;
			case 'A':	printflg |= Aflg;	break;
			case 'M':	printflg |= Mflg;	break;
			case 'R':	printflg |= Rflg;	break;
			case 'U':	printflg |= Uflg;	break;
			default:	usage();		break;
		}
		break;
	case 'b':
		isindexed = 0;
		base = EARGF(usage());
		if(resolveref(&h, base) == -1)
			sysfatal("no such ref '%s'", base);
		bdir = smprint(".git/fs/object/%H/tree", h);
		break;
	case 'I':
		/* invalidate index */
		staleidx = 1;
		break;
	case 'r':
		findslashes(EARGF(usage()));
		break;
	default:
		usage();
	}ARGEND;

	if(printflg == 0)
		printflg = Tflg | Aflg | Mflg | Rflg;

	argrel = emalloc(argc*sizeof(char*));
	argn = emalloc(argc*sizeof(int));
	for(i = 0; i < argc; i++){
		argrel[i] = reporel(argv[i]);
		argn[i] = strlen(argrel[i]);
	}

	if(isindexed && !staleidx){
		if((f = Bopen(".git/INDEX9", OREAD)) == nil){
			if(access(".git/index9", AEXIST) == 0){
				fprint(2, "index format conversion needed:\n");
				fprint(2, "\tcd %s && git/fs\n", repopath);
				fprint(2, "\t@{cd .git/index9/removed >[2]/dev/null && walk -f | sed 's/^/R NOQID 0 /'} >> .git/INDEX9\n");
				fprint(2, "\t@{cd .git/fs/HEAD/tree && walk -f | sed 's/^/T NOQID 0 /'} >> .git/INDEX9\n");
				exits("noindex");
			}
			staleidx = 1;
			goto Stale;
		}

		nidx = 0;
		idxsz = 32;
		idx = emalloc(idxsz*sizeof(Idxent));

		line = 0;
		while((ln = Brdstr(f, '\n', 1)) != nil){
			line++;
			/* allow blank lines */
			if(ln[0] == 0 || ln[0] == '\n')
				continue;
			if(getfields(ln, parts, nelem(parts), 0, " \t") != nelem(parts))
				sysfatal(".git/INDEX9:%d: corrupt index", line);
			cleanname(parts[3]);
			if(nidx == idxsz){
				idxsz += idxsz/2;
				idx = erealloc(idx, idxsz*sizeof(Idxent));
			}
			idx[nidx].state = *parts[0];
			idx[nidx].qid = parseqid(parts[1]);
			idx[nidx].mode = strtol(parts[2], nil, 8);
			idx[nidx].path = estrdup(parts[3]);
			idx[nidx].order = nidx;
			nidx++;
			free(ln);
		}
		Bterm(f);
	} else {
Stale:
		nwdir = 0;
		wdirsz = 32;
		wdir = emalloc(wdirsz*sizeof(Idxent));

		if(chdir(bdir) == -1)
			sysfatal("chdir: %r");

		/* load whole tree into index when stale */
		intree = 1;
		if(staleidx || argc == 0)
			loadwdir(".");
		else for(i = 0; i < argc; i++)
			loadwdir(argrel[i]);

		if(chdir(repopath) == -1)
			sysfatal("chdir: %r");

		/* use as index */
		idx = wdir;
		nidx = nwdir;
		idxsz = wdirsz;

		cleanidx = 1;
	}
	qsort(idx, nidx, sizeof(Idxent), idxcmp);

	nwdir = 0;
	wdirsz = 32;
	wdir = emalloc(wdirsz*sizeof(Idxent));

	intree = 0;
	if(argc == 0)
		loadwdir(".");
	else for(i = 0; i < argc; i++)
		loadwdir(argrel[i]);
	qsort(wdir, nwdir, sizeof(Idxent), idxcmp);

	if((o = Bfdopen(1, OWRITE)) == nil)
		sysfatal("open out: %r");

	i = 0;
	j = 0;
	while(i < nidx || j < nwdir){
		/* find the last entry we tracked for a path */
		while(i+1 < nidx && strcmp(idx[i].path, idx[i+1].path) == 0){
			staleidx = 1;
			i++;
		}
		while(j+1 < nwdir && strcmp(wdir[j].path, wdir[j+1].path) == 0)
			j++;
		if(i < nidx && !pfxmatch(idx[i].path, argrel, argn, argc)){
			i++;
			continue;
		}
		if(i >= nidx)
			c = 1;
		else if(j >= nwdir)
			c = -1;
		else
			c = strcmp(idx[i].path, wdir[j].path);
		/* exists in both index and on disk */
		if(c == 0){
			if(idx[i].state == 'R'){
				if(checkedin(&idx[i], 0))
					show(o, Rflg, rstr, idx[i].path);
				else{
					idx[i].state = 'U';
					staleidx = 1;
				}
			}else if(idx[i].state == 'A' && !checkedin(&idx[i], 1))
				show(o, Aflg, astr, idx[i].path);
			else if(!samedata(&idx[i], &wdir[j]))
				show(o, Mflg, mstr, idx[i].path);
			else
				show(o, Tflg, tstr, idx[i].path);
			i++;
			j++;
		/* only exists in index */
		}else if(c < 0){
			if(checkedin(&idx[i], 0))
				show(o, Rflg, rstr, idx[i].path);
			else{
				idx[i].state = 'U';
				staleidx = 1;
			}
			i++;
		/* only exists on disk */
		}else{
			if(checkedin(&wdir[j], 0)){
				if(samedata(nil, &wdir[j]))
					show(o, Tflg, tstr, wdir[j].path);
				else
					show(o, Mflg, mstr, wdir[j].path);
			}else if(printflg & Uflg && pfxmatch(wdir[j].path, argrel, argn, argc))
				show(o, Uflg, ustr, wdir[j].path);
			j++;
		}
	}
	Bterm(o);

	if(isindexed && staleidx)
	if((wfd = create(".git/INDEX9.new", OWRITE, 0644)) != -1){
		if((w = Bfdopen(wfd, OWRITE)) == nil){
			close(wfd);
			goto Nope;
		}
		for(i = 0; i < nidx; i++){
			while(i+1 < nidx && strcmp(idx[i].path, idx[i+1].path) == 0)
				i++;
			if(idx[i].state == 'U')
				continue;
			Bprint(w, "%c %Q %o %s\n",
				idx[i].state,
				idx[i].qid, 
				idx[i].mode,
				idx[i].path);
		}
		Bterm(w);
		nulldir(&rn);
		rn.name = "INDEX9";
		if(remove(".git/INDEX9") == -1)
			if(access(".git/INDEX9", AEXIST) == 0)
				goto Nope;
		if(dirwstat(".git/INDEX9.new", &rn) == -1)
			sysfatal("rename: %r");
	}

Nope:
	if(!dirty)
		exits(nil);

	p = xbuf;
	e = p + sizeof(xbuf);
	for(i = 0; (1 << i) != Tflg; i++)
		if(dirty & (1 << i))
			p = seprint(p, e, "%c", "RMAUT"[i]);
	*p = '\0';
	exits(xbuf);
}
