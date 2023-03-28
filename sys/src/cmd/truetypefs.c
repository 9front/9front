#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
#include <bio.h>
#include <ttf.h>

static char Egreg[] = "my memory of truetype is fading";
static char Enoent[] = "not found";

static char *fontpath = "/lib/font/ttf";

enum { MAXSUB = 0x100 };

typedef struct TFont TFont;
typedef struct TSubfont TSubfont;

struct TFont {
	int ref;
	Qid qid;
	Qid fileqid;
	char *fontfile;
	int nfontfile;
	TTFont *ttf;
	char *name9p;
	char *name;
	int size;
	TFont *next, *prev;
	TSubfont *sub[256];
};

struct TSubfont {
	TFont *font;
	Rune start, end;
	Qid qid;
	char *data;
	int ndata;
	TSubfont *next;
};

typedef struct FidAux FidAux;

struct FidAux {
	enum {
		FIDROOT,
		FIDFONT,
		FIDFONTF,
		FIDSUB,
	} type;
	TFont *f;
	TSubfont *sub;
};

TFont fontl = {.next = &fontl, .prev = &fontl};

static void *
emalloc(ulong n)
{
	void *v;
	
	v = mallocz(n, 1);
	if(v == nil) sysfatal("malloc: %r");
	setmalloctag(v, getcallerpc(&n));
	return v;
}

static uvlong
qidgen(void)
{
	static uvlong x;
	
	return ++x;
}

static void
fsattach(Req *r)
{
	r->ofcall.qid = (Qid){0, 0, QTDIR};
	r->fid->qid = r->ofcall.qid;
	r->fid->aux = emalloc(sizeof(FidAux));
	respond(r, nil);
}

void
mksubfonts(TFont *f)
{
	int k;
	TTChMap *c;
	TTFontU *u;
	TSubfont *s;
	Fmt fmt;
	int got0;
	
	u = f->ttf->u;
	fmtstrinit(&fmt);
	fmtprint(&fmt, "%d\t%d\n", f->ttf->ascentpx + f->ttf->descentpx, f->ttf->ascentpx);
	got0 = 0;
	for(c = u->cmap; c < u->cmap + u->ncmap; c++){
		for(k = c->start; k < c->end; k += MAXSUB){
			s = emalloc(sizeof(TSubfont));
			s->start = k;
			if(k == 0) got0 = 1;
			s->end = k + MAXSUB - 1;
			if(s->end > c->end)
				s->end = c->end;
			s->font = f;
			s->qid = (Qid){qidgen(), 0, 0};
			s->next = f->sub[k >> 8 & 0xff];
			f->sub[k >> 8 & 0xff] = s;
			fmtprint(&fmt, "%#.4ux\t%#.4ux\ts.%.4ux-%.4ux\n", s->start, s->end, s->start, s->end);
		}
	}
	if(!got0){
		s = emalloc(sizeof(TSubfont));
		s->start = 0;
		s->end = 0;
		s->font = f;
		s->qid = (Qid){qidgen(), 0, 0};
		s->next = f->sub[0];
		f->sub[0] = s;
		fmtprint(&fmt, "%#.4ux\t%#.4ux\ts.%.4ux-%.4ux\n", 0, 0, 0, 0);
	}
	f->fontfile = fmtstrflush(&fmt);
	f->nfontfile = strlen(f->fontfile);
}

static void
blit(uchar *t, int x, int y, int tstride, uchar *s, int w, int h)
{
	int tx, ty, sx, sy;
	u16int b;
	uchar *tp, *sp;
	
	if(y < 0) y = 0;
	ty = y;
	sp = s;
	for(sy = 0; sy < h; sy++, ty++){
		tx = x;
		tp = t + ty * tstride + (tx >> 3);
		b = 0;
		for(sx = 0; sx < w; sx += 8){
			b |= *sp++ << 8 - (tx & 7);
			*tp++ |= b >> 8;
			b <<= 8;
		}
		*tp |= b >> 8;
	}
}

static void
compilesub(TFont *f, TSubfont *s)
{
	int n, i, w, x, h, g, sz;
	char *d, *p;
	TTGlyph **gs;
	TTFont *t;
	
	t = f->ttf;
	n = s->end - s->start + 1;
	gs = emalloc9p(sizeof(TTGlyph *) * n);
	w = 0;
	h = t->ascentpx + t->descentpx;
	for(i = 0; i < n; i++){
		if(s->start + i == 0)
			g = 0;
		else
			g = ttffindchar(t, s->start + i);
		if((gs[i] = ttfgetglyph(t, g, 1)) == nil && g != 0)
			gs[i] = ttfgetglyph(t, 0, 1);
		assert(gs[i] != nil);
	   w += gs[i]->width;
	}
	sz = 5 * 12 + (w+7>>3) * h + 3 * 12 + (n + 1) * 6;
	d = emalloc(sz);
	p = d + sprint(d, "%11s %11d %11d %11d %11d ", "k1", 0, 0, w, h);
	x = 0;
	for(i = 0; i < n; i++){
		blit((uchar*)p, x, t->ascentpx - gs[i]->ymaxpx, w+7>>3, gs[i]->bit, gs[i]->width, gs[i]->height);
		x += gs[i]->width;
	}
	p += (w+7>>3) * h;
	p += sprint(p, "%11d %11d %11d ", n, h, t->ascentpx);
	x = 0;
	for(i = 0; i < n; i++){
		*p++ = x;
		*p++ = x >> 8;
		*p++ = 0;
		*p++ = h;
		*p++ = gs[i]->xminpx;
		if(gs[i]->advanceWidthpx != 0)
			*p++ = gs[i]->advanceWidthpx;
		else
			*p++ = gs[i]->width;
		x += gs[i]->width;
	}
	*p++ = x;
	*p = x >> 8;
	s->data = d;
	s->ndata = sz;
	for(i = 0; i < n; i++)
		ttfputglyph(gs[i]);
	free(gs);
}


TFont *
tryfont(char *name)
{
	TTFont *ttf;
	TFont *f;
	char *d, *buf, *p;
	int sz;
	
	for(f = fontl.next; f != &fontl; f = f->next)
		if(strcmp(f->name9p, name) == 0)
			return f;
	d = strrchr(name, '.');
	if(d == nil){
	inval:
		werrstr("invalid file name");
		return nil;
	}
	sz = strtol(d + 1, &p, 10);
	if(d[1] == 0 || *p != 0)
		goto inval;
	buf = estrdup9p(name);
	buf[d - name] = 0;
	p = smprint("%s/%s", fontpath, buf);
	if(p == nil)
		sysfatal("smprint: %r");
	ttf = ttfopen(p, sz, 0);
	free(p);
	if(ttf == nil){
		free(buf);
		return nil;
	}
	f = emalloc(sizeof(TFont));
	f->ttf = ttf;
	f->name9p = strdup(name);
	f->name = buf;
	f->size = sz;
	f->qid = (Qid){qidgen(), 0, QTDIR};
	f->fileqid = (Qid){qidgen(), 0, 0};
	f->next = &fontl;
	f->prev = fontl.prev;
	f->next->prev = f;
	f->prev->next = f;
	mksubfonts(f);
	return f;
}

static char *
fsclone(Fid *old, Fid *new)
{
	new->aux = emalloc(sizeof(FidAux));
	*(FidAux*)new->aux = *(FidAux*)old->aux;
	return nil;
}

static void
fsdestroyfid(Fid *f)
{
	FidAux *fa;
	
	fa = f->aux;
	free(fa);
	f->aux = nil;
}

static TSubfont *
findsubfont(TFont *f, char *name)
{
	char *p, *q;
	char buf[16];
	int a, b;
	TSubfont *s;

	if(name[0] != 's' || name[1] != '.' || name[2] == '-')
		return nil;
	a = strtol(name + 2, &p, 16);
	if(*p != '-')
		return nil;
	b = strtol(p + 1, &q, 16);
	if(p + 1 == q || *q != 0)
		return nil;
	snprint(buf, nelem(buf), "s.%.4ux-%.4ux", a, b);
	if(strcmp(buf, name) != 0)
		return nil;
	for(s = f->sub[a>>8&0xff]; s != nil; s = s->next)
		if(s->start == a && s->end == b)
			break;
	return s;
}

static char *
fswalk(Fid *fid, char *name, Qid *qid)
{
	static char errbuf[ERRMAX];
	FidAux *fa;

	fa = fid->aux;
	assert(fa != nil);
	switch(fa->type){
	case FIDROOT:
		fa->f = tryfont(name);
		if(fa->f == nil){
			rerrstr(errbuf, nelem(errbuf));
			return errbuf;
		}
		fa->f->ref++;
		fa->type = FIDFONT;
		fid->qid = fa->f->qid;
		*qid = fa->f->qid;
		return nil;
	case FIDFONT:
		if(strcmp(name, "font") == 0){
			fa->type = FIDFONTF;
			fid->qid = fa->f->fileqid;
			*qid = fa->f->fileqid;
			return nil;
		}
		fa->sub = findsubfont(fa->f, name);
		if(fa->sub == nil)
			return Enoent;
		fa->type = FIDSUB;
		fid->qid = fa->sub->qid;
		*qid = fa->sub->qid;
		return nil;
	default:
		return Egreg;
	}
}

static void
fsstat(Req *r)
{
	FidAux *fa;

	fa = r->fid->aux;
	assert(fa != nil);
	r->d.uid = estrdup9p(getuser());
	r->d.gid = estrdup9p(getuser());
	r->d.muid = estrdup9p(getuser());
	r->d.mtime = r->d.atime = time(0);
	r->d.qid = r->fid->qid;
	switch(fa->type){
	case FIDROOT:
		r->d.mode = 0777;
		r->d.name = estrdup9p("/");
		respond(r, nil);
		break;
	case FIDFONT:
		r->d.mode = 0777;
		r->d.name = estrdup9p(fa->f->name9p);
		respond(r, nil);
		break;
	case FIDFONTF:
		r->d.mode = 0666;
		r->d.name = estrdup9p("font");
		r->d.length = fa->f->nfontfile;
		respond(r, nil);
		break;
	case FIDSUB:
		r->d.mode = 0666;
		r->d.name = smprint("s.%.4ux-%.4ux", fa->sub->start, fa->sub->end);
		r->d.length = fa->sub->ndata;
		respond(r, nil);
		break;
	default:
		respond(r, Egreg);
	}
}

static int
fontdirread(int n, Dir *d, void *aux)
{
	FidAux *fa;
	
	fa = aux;
	if(n == 0){
		d->name = estrdup9p("font");
		d->uid = estrdup9p(getuser());
		d->gid = estrdup9p(getuser());
		d->muid = estrdup9p(getuser());
		d->mode = 0666;
		d->qid = fa->f->fileqid;
		d->mtime = d->atime = time(0);
		d->length = fa->f->nfontfile;
		return 0;
	}
	return -1;
}

static void
fsread(Req *r)
{
	FidAux *fa;

	fa = r->fid->aux;
	assert(fa != nil);
	switch(fa->type){
	case FIDROOT:
		respond(r, nil);
		break;
	case FIDFONT:
		dirread9p(r, fontdirread, fa);
		respond(r, nil);
		break;
	case FIDFONTF:
		readbuf(r, fa->f->fontfile, fa->f->nfontfile);
		respond(r, nil);
		break;
	case FIDSUB:
		if(fa->sub->data == nil)
			compilesub(fa->f, fa->sub);
		readbuf(r, fa->sub->data, fa->sub->ndata);
		respond(r, nil);
		break;
	default:
		respond(r, Egreg);
	}
}

Srv fssrv = {
	.attach = fsattach,
	.walk1 = fswalk,
	.clone = fsclone,
	.stat = fsstat,
	.read = fsread,
	.destroyfid = fsdestroyfid,
};

static void
usage(void)
{
	fprint(2, "usage: %s [-F fontpath]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	ARGBEGIN {
	case 'F':
		fontpath = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND;
	
	unmount(nil, "/n/ttf");
	postmountsrv(&fssrv, nil, "/n/ttf", 0);
	exits(nil);
}
