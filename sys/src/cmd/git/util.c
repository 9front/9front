#include <u.h>
#include <libc.h>
#include <ctype.h>

#include "git.h"

Reprog	*authorpat;
Hash	Zhash;
int	chattygit;
int	interactive = 1;
int	gitdirmode = -1;

enum {
	Seed		= 2928213749ULL
};

Object*
emptydir(void)
{
	static Object *e;

	if(e != nil)
		return ref(e);
	e = emalloc(sizeof(Object));
	e->hash = Zhash;
	e->type = GTree;
	e->tree = emalloc(sizeof(Tinfo));
	e->tree->ent = nil;
	e->tree->nent = 0;
	e->flag |= Cloaded|Cparsed;
	e->off = -1;
	ref(e);
	cache(e);
	return e;
}
int
entcmp(void *pa, void *pb)
{
	Dirent *ae, *be;
	uchar *a, *b;
	int ca, cb;

	ae = pa;
	be = pb;
	a = (uchar*)ae->name;
	b = (uchar*)be->name;
	/*
	 * If the files have the same name, they're equal.
	 * Otherwise, If they're trees, they sort as thoug
	 * there was a trailing slash.
	 *
	 * Wat.
	 */
	while(1){
		ca = *a++;
		cb = *b++;
		/*
		 * because these are dir entries in a tree,
		 * the only '/' allowable is the virtual '/'
		 * at the end of the file name.
		 */
		assert(ca != '/' && cb != '/');
		if(ca != cb){
			if(ca == 0 && (ae->mode & DMDIR))
				ca = '/';
			if(cb == 0 && (be->mode & DMDIR))
				cb = '/';
			return (ca > cb) ? 1 : -1;
		}
		if(ca == 0){
			if(ae->mode & DMDIR)
				ca = '/';
			if(be->mode & DMDIR)
				cb = '/';
			if(ca == cb)
				return 0;
			return (ca > cb) ? 1 : -1;
		}
	}
}

int
hasheq(Hash *a, Hash *b)
{
	return memcmp(a->h, b->h, sizeof(a->h)) == 0;
}

int
charval(int c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	werrstr("invalid hex char");
	return -1;
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
eamalloc(ulong n, ulong sz)
{
	uvlong na;
	void *v;

	na = (uvlong)n*(uvlong)sz;
	if(na >= (1ULL<<30))
		sysfatal("alloc: overflow");
	v = mallocz(na, 1);
	if(v == nil)
		sysfatal("malloc: %r");
	setmalloctag(v, getcallerpc(&n));
	return v;
}

void *
erealloc(void *p, ulong n)
{
	void *v;
	
	v = realloc(p, n);
	if(v == nil)
		sysfatal("realloc: %r");
	setmalloctag(v, getcallerpc(&p));
	return v;
}

void *
earealloc(void *p, ulong n, ulong sz)
{
	uvlong na;
	void *v;

	na = (uvlong)n*(uvlong)sz;
	if(na >= (1ULL<<30))
		sysfatal("alloc: overflow");
	v = realloc(p, na);
	if(v == nil)
		sysfatal("realloc: %r");
	setmalloctag(v, getcallerpc(&p));
	return v;
}

char*
estrdup(char *s)
{
	s = strdup(s);
	if(s == nil)
		sysfatal("strdup: %r");
	setmalloctag(s, getcallerpc(&s));
	return s;
}

int
Hfmt(Fmt *fmt)
{
	Hash h;
	int i, n, l;
	char c0, c1;

	l = 0;
	h = va_arg(fmt->args, Hash);
	for(i = 0; i < sizeof h.h; i++){
		n = (h.h[i] >> 4) & 0xf;
		c0 = (n >= 10) ? n-10 + 'a' : n + '0';
		n = h.h[i] & 0xf;
		c1 = (n >= 10) ? n-10 + 'a' : n + '0';
		l += fmtprint(fmt, "%c%c", c0, c1);
	}
	return l;
}

int
Tfmt(Fmt *fmt)
{
	int t;
	int l;

	t = va_arg(fmt->args, int);
	switch(t){
	case GNone:	l = fmtprint(fmt, "none");	break;
	case GCommit:	l = fmtprint(fmt, "commit");	break;
	case GTree:	l = fmtprint(fmt, "tree");	break;
	case GBlob:	l = fmtprint(fmt, "blob");	break;
	case GTag:	l = fmtprint(fmt, "tag");	break;
	case GOdelta:	l = fmtprint(fmt, "odelta");	break;
	case GRdelta:	l = fmtprint(fmt, "gdelta");	break;
	default:	l = fmtprint(fmt, "?%d?", t);	break;
	}
	return l;
}

int
Ofmt(Fmt *fmt)
{
	Object *o;
	int l;

	o = va_arg(fmt->args, Object *);
	print("== %H (%T) ==\n", o->hash, o->type);
	switch(o->type){
	case GTree:
		l = fmtprint(fmt, "tree\n");
		break;
	case GBlob:
		l = fmtprint(fmt, "blob %s\n", o->data);
		break;
	case GCommit:
		l = fmtprint(fmt, "commit\n");
		break;
	case GTag:
		l = fmtprint(fmt, "tag\n");
		break;
	default:
		l = fmtprint(fmt, "invalid: %d\n", o->type);
		break;
	}
	return l;
}

int
Qfmt(Fmt *fmt)
{
	Qid q;

	q = va_arg(fmt->args, Qid);
	if(q.path == ~0ULL && q.vers == ~0UL && q.type == 0xff)
		return fmtprint(fmt, "NOQID");
	else
		return fmtprint(fmt, "%llux.%lud.%hhx", q.path, q.vers, q.type);
}

/* Finds the directory containing the git repo. */
static void
findrepo(char *buf, int nbuf, int *nrel)
{
	char *p, *suff;

	suff = "/.git/HEAD";
	if(getwd(buf, nbuf - strlen(suff) - 1) == nil)
		sysfatal("getwd: %r");

	*nrel = 0;
	for(p = buf + strlen(buf); p != nil; p = strrchr(buf, '/')){
		strcpy(p, suff);
		if(access(buf, AEXIST) == 0){
			p[p == buf] = '\0';
			return;
		}
		*nrel += 1;
		*p = '\0';
	}
	sysfatal("not a git repository");
}

void
gitinit(char *root, int nroot, int *nrel)
{
	char repo[512] = ".git";
	Dir *d;

	fmtinstall('H', Hfmt);
	fmtinstall('T', Tfmt);
	fmtinstall('O', Ofmt);
	fmtinstall('Q', Qfmt);
	inflateinit();
	deflateinit();
	authorpat = regcomp("[\t ]*(.*)[\t ]+([0-9]+)[\t ]*([\\-+]?[0-9]+)?");
	osinit(&objcache);
	if(root != nil){
		findrepo(root, nroot, nrel);
		snprint(repo, sizeof(repo), "%s/.git", root);
	}
	if((d = dirstat(repo)) == nil)
		sysfatal("stat %s: %r", repo);
	gitdirmode = d->mode & 0777;
	free(d);
}

int
hparse(Hash *h, char *b)
{
	int i, c0, c1;

	for(i = 0; i < nelem(h->h); i++){
		if((c0 = charval(b[2*i+0])) == -1)
			return -1;
		if((c1 = charval(b[2*i+1])) == -1)
			return -1;
		h->h[i] = (c0 << 4) | c1;
	}
	return 0;
}

int
slurpdir(char *p, Dir **d)
{
	int r, f;

	if((f = open(p, OREAD)) == -1)
		return -1;
	r = dirreadall(f, d);
	close(f);
	return r;
}	

int
hassuffix(char *base, char *suf)
{
	int nb, ns;

	nb = strlen(base);
	ns = strlen(suf);
	if(ns <= nb && strcmp(base + (nb - ns), suf) == 0)
		return 1;
	return 0;
}

int
swapsuffix(char *dst, int dstsz, char *base, char *oldsuf, char *suf)
{
	int bl, ol, sl, l;

	bl = strlen(base);
	ol = strlen(oldsuf);
	sl = strlen(suf);
	l = bl + sl - ol;
	if(l + 1 > dstsz || ol > bl)
		return -1;
	memmove(dst, base, bl - ol);
	memmove(dst + bl - ol, suf, sl);
	dst[l] = 0;
	return l;
}

char *
strip(char *s)
{
	char *e;

	while(isspace(*s))
		s++;
	e = s + strlen(s);
	while(e > s && isspace(*--e))
		*e = 0;
	return s;
}

void
_dprint(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprint(2, fmt, ap);
	va_end(ap);
}

int
showprogress(int x, int pct)
{
	if(!interactive)
		return 0;
	if(x > pct){
		pct = x;
		fprint(2, "\b\b\b\b%3d%%", pct);
	}
	return pct;
}

void
qinit(Objq *q)
{
	memset(q, 0, sizeof(Objq));
	q->nheap = 0;
	q->heapsz = 8;
	q->heap = eamalloc(q->heapsz, sizeof(Qelt));
}

void
qclear(Objq *q)
{
	free(q->heap);
}

void
qput(Objq *q, Object *o, int color)
{
	Qelt t;
	int i;

	assert(o->type == GCommit);
	if(q->nheap == q->heapsz){
		q->heapsz *= 2;
		q->heap = earealloc(q->heap, q->heapsz, sizeof(Qelt));
	}
	q->heap[q->nheap].o = o;
	q->heap[q->nheap].color = color;
	q->heap[q->nheap].ctime = o->commit->ctime;
	for(i = q->nheap; i > 0; i = (i-1)/2){
		if(q->heap[i].ctime < q->heap[(i-1)/2].ctime)
			break;
		t = q->heap[i];
		q->heap[i] = q->heap[(i-1)/2];
		q->heap[(i-1)/2] = t;
	}
	q->nheap++;
}

int
qpop(Objq *q, Qelt *e)
{
	int i, l, r, m;
	Qelt t;

	if(q->nheap == 0)
		return 0;
	*e = q->heap[0];
	if(--q->nheap == 0)
		return 1;

	i = 0;
	q->heap[0] = q->heap[q->nheap];
	while(1){
		m = i;
		l = 2*i+1;
		r = 2*i+2;
		if(l < q->nheap && q->heap[m].ctime < q->heap[l].ctime)
			m = l;
		if(r < q->nheap && q->heap[m].ctime < q->heap[r].ctime)
			m = r;
		if(m == i)
			break;
		t = q->heap[m];
		q->heap[m] = q->heap[i];
		q->heap[i] = t;
		i = m;
	}
	return 1;
}

u64int
murmurhash2(void *pp, usize n)
{
	u32int m = 0x5bd1e995;
	u32int r = 24;
	u32int h, k;
	uchar *w, *e;
	
	h = Seed ^ n;
	e = pp;
	e += n & -4;
	for (w = pp; w != e; w += 4) {
		k = (u32int)w[0] | (u32int)w[1] << 8 | (u32int)w[2] << 16 | (u32int)w[3] << 24;
		k *= m;
		k ^= k >> r;
		k *= m;

		h *= m;
		h ^= k;
	}

	switch (n & 0x3) {
	case 3:	h ^= w[2] << 16;
	case 2:	h ^= w[1] << 8;
	case 1:	h ^= w[0] << 0;
		h *= m;
	}

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
}

Qid
parseqid(char *s)
{
	char *e;
	Qid q;

	if(strcmp(s, "NOQID") == 0)
		return (Qid){-1, -1, -1};		
	e = s;
	q.path = strtoull(e, &e, 16);
	if(*e != '.')
		sysfatal("corrupt qid: %s (%s)", s, e);
	q.vers = strtoul(e+1, &e, 10);
	if(*e != '.')
		sysfatal("corrupt qid: %s (%s)", s, e);
	q.type = strtoul(e+1, &e, 16);
	if(*e != '\0')
		sysfatal("corrupt qid: %s (%x)", s, *e);
	return q;
}

/*
 * Checks the rules for valid ref names, as defined in
 *   git/Documentation/protocol-common.txt.
 * It does not check that the ref begins with refs/
 * or is called HEAD, since that's only needed in the
 * clone protocol.
 */
int
okref(char *ref)
{
	int n, slashed;
	char *p;
	Rune r;

	slashed = 0;
	if(*ref == '/' || *ref == '.')
		return 0;
	for(p = ref; *p != 0; p += n) {
		n = chartorune(&r, p);
		switch(r){
		case '.':
			if(p[1]== 0 || p[1] == '.')
				return 0;
			if(strcmp(p, ".lock") == 0)
				return 0;
			break;
		case '/':
			if(p[1] == 0 || p[1] == '.' || p[1] == '/')
				return 0;
			slashed = 1;
			break;
		case '@':
			if(p[1] == '{')
				return 0;
			break;
		case ' ':
		case '~':
		case '^':
		case ':':
		case '?':
		case '*':
		case '[':
		case '\\':
		case Runeerror:
		case 0x7f: /* DEL */
			return 0;
		default:
			if(r < 0x20 || isspacerune(r))
				return 0;
		}
	}
	if(ref == p)
		return 0;
	return slashed;
}
