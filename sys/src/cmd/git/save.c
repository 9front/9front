#include <u.h>
#include <libc.h>
#include "git.h"

typedef struct Objbuf Objbuf;
struct Objbuf {
	int off;
	char *hdr;
	int nhdr;
	char *dat;
	int ndat;
};

enum {
	Maxparents = 16,
};

char	*authorname;
char	*authoremail;
char	*committername;
char	*committeremail;
char	*commitmsg;
Hash	parents[Maxparents];
int	nparents;
Idxent	*idx;
int	idxsz;
int	nidx;

int
gitmode(Dirent *e)
{
	if(e->islink)
		return 0120000;
	else if(e->ismod)
		return 0160000;
	else if(e->mode & DMDIR)
		return 0040000;
	else if(e->mode & 0100)
		return 0100755;
	else
		return 0100644;
}

int
namecmp(void *pa, void *pb)
{
	return strcmp(*(char**)pa, *(char**)pb);
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
	assert(a->order != b->order);
	return a-> order < b->order ? -1 : 1;
}

static int
bwrite(void *p, void *buf, int nbuf)
{
	return Bwrite(p, buf, nbuf);
}

static int
objbytes(void *p, void *buf, int nbuf)
{
	Objbuf *b;
	int r, n, o;
	char *s;

	b = p;
	n = 0;
	if(b->off < b->nhdr){
		r = b->nhdr - b->off;
		r = (nbuf < r) ? nbuf : r;
		memcpy(buf, b->hdr, r);
		b->off += r;
		nbuf -= r;
		n += r;
	}
	if(b->off < b->ndat + b->nhdr){
		s = buf;
		o = b->off - b->nhdr;
		r = b->ndat - o;
		r = (nbuf < r) ? nbuf : r;
		memcpy(s + n, b->dat + o, r);
		b->off += r;
		n += r;
	}
	return n;
}

void
writeobj(Hash *h, char *hdr, int nhdr, char *dat, int ndat)
{
	Objbuf b = {.off=0, .hdr=hdr, .nhdr=nhdr, .dat=dat, .ndat=ndat};
	char s[64], o[256];
	SHA1state *st;
	Biobuf *f;
	int fd;

	st = sha1((uchar*)hdr, nhdr, nil, nil);
	st = sha1((uchar*)dat, ndat, nil, st);
	sha1(nil, 0, h->h, st);

	snprint(s, sizeof(s), "%H", *h);
	fd = create(".git/objects", OREAD, DMDIR|0755);
	close(fd);
	snprint(o, sizeof(o), ".git/objects/%c%c", s[0], s[1]);
	fd = create(o, OREAD, DMDIR | 0755);
	close(fd);
	snprint(o, sizeof(o), ".git/objects/%c%c/%s", s[0], s[1], s + 2);
	if(readobject(*h) == nil){
		if((f = Bopen(o, OWRITE)) == nil)
			sysfatal("could not open %s: %r", o);
		if(deflatezlib(f, bwrite, &b, objbytes, 9, 0) == -1)
			sysfatal("could not write %s: %r", o);
		Bterm(f);
	}
}

int
writetree(Dirent *ent, int nent, Hash *h)
{
	char *t, *txt, *etxt, hdr[128];
	int nhdr, n;
	Dirent *d, *p;

	t = emalloc((16+256+20) * nent);
	txt = t;
	etxt = t + (16+256+20) * nent;

	/* sqeeze out deleted entries */
	n = 0;
	p = ent;
	for(d = ent; d != ent + nent; d++)
		if(d->name)
			p[n++] = *d;
	nent = n;

	qsort(ent, nent, sizeof(Dirent), entcmp);
	for(d = ent; d != ent + nent; d++){
		if(strlen(d->name) >= 255)
			sysfatal("overly long filename: %s", d->name);
		t = seprint(t, etxt, "%o %s", gitmode(d), d->name) + 1;
		memcpy(t, d->h.h, sizeof(d->h.h));
		t += sizeof(d->h.h);
	}
	nhdr = snprint(hdr, sizeof(hdr), "%T %lld", GTree, (vlong)(t - txt)) + 1;
	writeobj(h, hdr, nhdr, txt, t - txt);
	free(txt);
	return nent;
}

void
blobify(Dir *d, char *path, int *mode, Hash *bh)
{
	char h[64], *buf;
	int f, nh;

	if((d->mode & DMDIR) != 0)
		sysfatal("not file: %s", path);
	*mode = d->mode;
	nh = snprint(h, sizeof(h), "%T %lld", GBlob, d->length) + 1;
	if((f = open(path, OREAD)) == -1)
		sysfatal("could not open %s: %r", path);
	buf = emalloc(d->length);
	if(readn(f, buf, d->length) != d->length)
		sysfatal("could not read blob %s: %r", path);
	writeobj(bh, h, nh, buf, d->length);
	free(buf);
	close(f);
}

int
tracked(char *path)
{
	int r, lo, hi, mid;

	lo = 0;
	hi = nidx-1;
	while(lo <= hi){
		mid = (hi + lo) / 2;
		r = strcmp(path, idx[mid].path);
		if(r < 0)
			hi = mid-1;
		else if(r > 0)
			lo = mid+1;
		else
			return idx[mid].state != 'R';
	}
	return 0; 
}

Dirent*
dirent(Dirent **ent, int *nent, char *name)
{
	Dirent *d;

	assert(strchr(name, '/') == nil);

	for(d = *ent; d != *ent + *nent; d++)
		if(d->name && strcmp(d->name, name) == 0)
			return d;
	*nent += 1;
	*ent = erealloc(*ent, *nent * sizeof(Dirent));
	d = *ent + (*nent - 1);
	memset(d, 0, sizeof(*d));
	d->name = estrdup(name);
	return d;
}

int
treeify(Object *t, char **path, char **epath, int off, Hash *h)
{
	int nent, ne, slash, isdir;
	char *s, **p, **ep;
	Dirent *e, *ent;
	Object *o;
	Dir *d;

	nent = t->tree->nent;
	ent = eamalloc(nent, sizeof(*ent));
	memcpy(ent, t->tree->ent, nent*sizeof(*ent));
	for(p = path; p != epath; p = ep){
		s = *p;

		/*
		 * paths have been normalized already,
		 * no leading or double-slashes allowed.
		 */
		assert(off <= strlen(s));
		assert(off == 0 || s[off-1] == '/');
		assert(s[off] != '\0' && s[off] != '/');

		/* get next path element length (from off until '/' or nul) */
		for(ne = 1; s[off+ne] != '\0' && s[off+ne] != '/'; ne++)
			;

		/* truncate at '/' or nul */
		slash = s[off + ne];
		s[off + ne] = '\0';

		/* skip over children (having s as prefix) */
		for(ep = p + 1; slash && ep != epath; ep++){
			if(strncmp(s, *ep, off + ne) != 0)
				break;
			if((*ep)[off+ne] != '\0' && (*ep)[off+ne] != '/')
				break;
		}

		d = dirstat(s);
		e = dirent(&ent, &nent, s + off);
		if(e->islink)
			sysfatal("symlinks may not be modified: %s", s);
		if(e->ismod)
			sysfatal("submodules may not be modified: %s", s);

		s[off + ne] = slash;

		isdir = d != nil && (d->mode & DMDIR) != 0;
		/*
		 * exist? slash? dir?	track?
		 * n      _      _      _      -> remove: file gone
		 * y      n      n      y      -> blob: tracked non-dir
		 * y      n      y      n      -> remove: file untracked
		 * y      n      y      n      -> remove: file -> dir
		 * y      n      y      y      -> remove: file -> dir
		 * y      n      y      n      -> untracked dir, cli junk
		 * y      y      y      n      -> recurse
		 * y      y      y      y      -> recurse
		 */
		if(d == nil || !slash && isdir && tracked(s)){
			/*
			 * if a tracked file is removed or turned
			 * into a dir, we want to delete it. We
			 * only want to change files passed in, and
			 * not ones along the way, so ignore files
			 * that have a '/'.
			 */
			e->name = nil;
			s[off + ne] = slash;
			continue;
		} else if(slash && isdir){
			/*
			 * If we have a list of entries that go into
			 * a directory, create a tree node for this
			 * entry, and recurse down.
			 */
			e->mode = DMDIR | 0755;
			o = readobject(e->h);
			if(o == nil || o->type != GTree)
				o = emptydir();
			/*
			 * if after processing deletions, a tree is empty,
			 * mark it for removal from the parent.
			 *
			 * Note, it is still written to the object store,
			 * but this is fine -- and ensures that an empty
			 * repository will continue to work.
			 */
			if(treeify(o, p, ep, off + ne + 1, &e->h) == 0)
				e->name = nil;
		}else if(!slash && !isdir){
			/*
			 * If the file was explicitly passed in and is
			 * not a dir, we want to either remove it or
			 * track it, depending on the state of the index.
			 */
			if(tracked(s) && !isdir)
				blobify(d, s, &e->mode, &e->h);
			else
				e->name = nil;
		}
		free(d);
	}
	if(nent == 0)
		sysfatal("%.*s: refusing to update empty directory", off, *path);
	nent = writetree(ent, nent, h);
	free(ent);
	return nent;		
}


void
mkcommit(Hash *c, vlong date, Hash tree)
{
	char *s, h[64];
	int ns, nh, i;
	Fmt f;

	fmtstrinit(&f);
	fmtprint(&f, "tree %H\n", tree);
	for(i = 0; i < nparents; i++)
		fmtprint(&f, "parent %H\n", parents[i]);
	fmtprint(&f, "author %s <%s> %lld +0000\n", authorname, authoremail, date);
	fmtprint(&f, "committer %s <%s> %lld +0000\n", committername, committeremail, date);
	fmtprint(&f, "\n");
	fmtprint(&f, "%s", commitmsg);
	s = fmtstrflush(&f);

	ns = strlen(s);
	nh = snprint(h, sizeof(h), "%T %d", GCommit, ns) + 1;
	writeobj(c, h, nh, s, ns);
	free(s);
}

Object*
findroot(void)
{
	Object *t, *c;
	Hash h;

	if(resolveref(&h, "HEAD") == -1)
		return emptydir();
	if((c = readobject(h)) == nil || c->type != GCommit)
		sysfatal("could not read HEAD %H", h);
	if((t = readobject(c->commit->tree)) == nil)
		sysfatal("could not read tree for commit %H", h);
	return t;
}

void
usage(void)
{
	fprint(2, "usage: %s -n name -e email -m message -d date [files...]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *ln, *dstr, *parts[4], cwd[1024];
	int i, r, line, ncwd;
	Hash th, ch;
	vlong date;
	Biobuf *f;
	Object *t;

	gitinit(nil, 0, nil);
	if(access(".git", AEXIST) != 0)
		sysfatal("could not find git repo: %r");
	if(getwd(cwd, sizeof(cwd)) == nil)
		sysfatal("getcwd: %r");
	dstr = nil;
	date = time(nil);
	ncwd = strlen(cwd);

	ARGBEGIN{
	case 'm':
		commitmsg = EARGF(usage());
		break;
	case 'n':
		authorname = EARGF(usage());
		break;
	case 'e':
		authoremail = EARGF(usage());
		break;
	case 'N':
		committername = EARGF(usage());
		break;
	case 'E':
		committeremail = EARGF(usage());
		break;
	case 'd':
		dstr = EARGF(usage());
		break;
	case 'p':
		if(nparents >= Maxparents)
			sysfatal("too many parents");
		if(resolveref(&parents[nparents++], EARGF(usage())) == -1)
			sysfatal("invalid parent: %r");
		break;
	default:
		usage();
		break;
	}ARGEND;

	if(commitmsg == nil)
		sysfatal("missing message");
	if(authorname == nil)
		sysfatal("missing name");
	if(authoremail == nil)
		sysfatal("missing email");
	if((committername == nil) != (committeremail == nil))
		sysfatal("partially specified committer");
	if(committername == nil && committeremail == nil){
		committername = authorname;
		committeremail = authoremail;
	}
	if(dstr){
		date=strtoll(dstr, &dstr, 10);
		if(strlen(dstr) != 0)
			sysfatal("could not parse date %s", dstr);
	}
	for(i = 0; i < argc; i++){
		cleanname(argv[i]);
		if(*argv[i] == '/' && strncmp(argv[i], cwd, ncwd) == 0)
			argv[i] += ncwd;
		while(*argv[i] == '/')
			argv[i]++;
	}
	qsort(argv, argc, sizeof(*argv), namecmp);

	t = findroot();
	nidx = 0;
	idxsz = 32;
	idx = emalloc(idxsz*sizeof(Idxent));
	if((f = Bopen(".git/INDEX9", OREAD)) == nil)
		sysfatal("open index: %r");
	line = 0;
	while((ln = Brdstr(f, '\n', 1)) != nil){
		line++;
		if(ln[0] == 0 || ln[0] == '\n')
			continue;
		if(getfields(ln, parts, nelem(parts), 0, " \t") != nelem(parts))
			sysfatal(".git/INDEX9:%d: corrupt index", line);
		if(nidx == idxsz){
			idxsz += idxsz/2;
			idx = realloc(idx, idxsz*sizeof(Idxent));
		}
		cleanname(parts[3]);
		idx[nidx].state = *parts[0];
		idx[nidx].qid = parseqid(parts[1]);
		idx[nidx].mode = strtol(parts[2], nil, 8);
		idx[nidx].path = strdup(parts[3]);
		idx[nidx].order = nidx;
		nidx++;
		free(ln);
	}
	Bterm(f);
	qsort(idx, nidx, sizeof(Idxent), idxcmp);
	r = treeify(t, argv, argv + argc, 0, &th);
	if(r == -1)
		sysfatal("could not commit: %r");
	mkcommit(&ch, date, th);
	print("%H\n", ch);
	exits(nil);
}
