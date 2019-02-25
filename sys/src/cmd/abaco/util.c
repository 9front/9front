#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <memdraw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <plumb.h>
#include <html.h>
#include <regexp.h>
#include "dat.h"
#include "fns.h"

static	Point		prevmouse;
static	Window	*mousew;

int
min(int a, int b)
{
	if(a < b)
		return a;
	return b;
}

int
max(int a, int b)
{
	if(a > b)
		return a;
	return b;
}

void
cvttorunes(char *p, int n, Rune *r, int *nb, int *nr, int *nulls)
{
	uchar *q;
	Rune *s;
	int j, w;

	/*
	 * Always guaranteed that n bytes may be interpreted
	 * without worrying about partial runes.  This may mean
	 * reading up to UTFmax-1 more bytes than n; the caller
	 * knows this.  If n is a firm limit, the caller should
	 * set p[n] = 0.
	 */
	q = (uchar*)p;
	s = r;
	for(j=0; j<n; j+=w){
		if(*q < Runeself){
			w = 1;
			*s = *q++;
		}else{
			w = chartorune(s, (char*)q);
			q += w;
		}
		if(*s)
			s++;
		else if(nulls)
			*nulls = TRUE;
	}
	*nb = (char*)q-p;
	*nr = s-r;
}

void
bytetorunestr(char *s, Runestr *rs)
{
	Rune *r;
	int nb, nr;

	nb = strlen(s);
	r = runemalloc(nb+1);
	cvttorunes(s, nb, r, &nb, &nr, nil);
	r[nr] = '\0';
	rs->nr = nr;
	rs->r = r;
}

void
error(char *s)
{
	fprint(2, "abaco: %s: %r\n", s);
//	abort();
	threadexitsall(s);
}

void*
emalloc(ulong n)
{
	void *p;

	p = malloc(n);
	if(p == nil)
		error("malloc failed");
	setmalloctag(p, getcallerpc(&n));
	memset(p, 0, n);
	return p;
}

void*
erealloc(void *p, ulong n)
{
	p = realloc(p, n);
	if(p == nil)
		error("realloc failed");
	setmalloctag(p, getcallerpc(&n));
	return p;
}

Rune*
erunestrdup(Rune *r)
{
	void *p;

	if(r == nil)
		return nil;
	p = runestrdup(r);
	if(p == nil)
		error("runestrdup failed");
	setmalloctag(p, getcallerpc(&r));
	return p;
}

char*
estrdup(char *s)
{
	char *t;

	t = strdup(s);
	if(t == nil)
		error("strdup failed");
	setmalloctag(t, getcallerpc(&s));
	return t;
}

int
runestreq(Runestr a, Runestr b)
{
	return runeeq(a.r, a.nr, b.r, b.nr);
}

int
runeeq(Rune *s1, uint n1, Rune *s2, uint n2)
{
	if(n1 != n2)
		return FALSE;
	return memcmp(s1, s2, n1*sizeof(Rune)) == 0;
}

void
closerunestr(Runestr *rs)
{

	rs->nr = 0;
	if(rs->r)
		free(rs->r);
	rs->r = nil;
}

void
copyrunestr(Runestr *a, Runestr *b)
{
	closerunestr(a);
	a->r = runemalloc(b->nr+1);
	runemove(a->r, b->r, b->nr);
	a->r[b->nr] = 0;
	a->nr = b->nr;
}

int
isalnum(Rune c)
{
	/*
	 * Hard to get absolutely right.  Use what we know about ASCII
	 * and assume anything above the Latin control characters is
	 * potentially an alphanumeric.
	 */
	if(c <= ' ')
		return FALSE;
	if(0x7F<=c && c<=0xA0)
		return FALSE;
	if(utfrune("!\"#$%&'()*+,-./:;<=>?@[\\]^`{|}~", c))
		return FALSE;
	return TRUE;
}

Rune*
skipbl(Rune *r, int n, int *np)
{
	while(n>0 && (*r==' ' || *r=='\t' || *r=='\n')){
		--n;
		r++;
	}
	*np = n;
	return r;
}

Rune*
findbl(Rune *r, int n, int *np)
{
	while(n>0 && *r!=' ' && *r!='\t' && *r!='\n'){
		--n;
		r++;
	}
	*np = n;
	return r;
}

int
istextfield(Item *i)
{
	Formfield *ff;

	ff = ((Iformfield *)i)->formfield;
	if(ff->ftype==Ftext || ff->ftype==Ftextarea || ff->ftype==Fpassword)
		return TRUE;

	return FALSE;
}

int
forceitem(Item *i)
{
	if(i->state&IFwrap && i->tag!=Iruletag && i->tag!=Itabletag)
		return FALSE;

	return TRUE;
}

int
dimwidth(Dimen d, int w)
{
	int s, k;

	k = dimenkind(d);
	if(k == Dnone)
		return w;
	s = dimenspec(d);
	if(k == Dpixels)
		 w = s;
	else if(k==Dpercent && s<100)
		w = s*w/100;

	return w;
}

void
frdims(Dimen *d, int n, int t, int **ret)
{
	int totpix, totpcnt, totrel;
	double spix, spcnt, relu, vd;
	int tt, trest, totpixrel, minrelu, i;
	int *x, *spec, *kind;

	if(n == 1){
		*ret = x = emalloc(sizeof(int));
		x[0] = t;
		return;
	}
	totpix = totpcnt = totrel = 0;
	spec = emalloc(n*sizeof(int));
	kind = emalloc(n*sizeof(int));
	for(i=0; i<n; i++){
		spec[i] = dimenspec(d[i]);
		if(spec[i] < 0)
			spec[i] = 0;
		kind[i] = dimenkind(d[i]);
		switch(kind[i]){
		case Dpixels:
			totpix += spec[i];
			break;
		case Dpercent:
			totpcnt += spec[i];
			break;
		case Drelative:
			totrel += spec[i];
			break;
		case Dnone:
			totrel++;
			break;
		}
	}
	spix = spcnt = 1.0;
	minrelu = 0;
	if(totrel > 0)
		minrelu = Scrollsize+Scrollgap;
	relu = (double)minrelu;
	tt = totpix + t*totpcnt/100 + totrel*minrelu;
	if(tt < t){
		if(totrel == 0){
			if(totpcnt != 0)
				spcnt = (double)((t-totpix)*100)/(double)(t*totpcnt);
			else
				spix = (double)t/(double)totpix;
		}else
			relu += (double)(t-tt)/(double)totrel;
	}else{
		totpixrel = totpix + totrel*minrelu;
		if(totpixrel < t)
			spcnt = (double)((t-totpixrel)*100)/(double)(t*totpcnt);
		else{
			trest = t - totrel*minrelu;
			if(trest > 0)
				spcnt = (double)trest/(double)(totpix + (t*totpcnt/100));
			else{
				spcnt = (double)t/(double)tt;
				relu = 0.0;
			}
			spix = spcnt;
		}
	}
	x = emalloc(n * sizeof(int));
	tt = 0;
	for(i=0; i<n-1; i++){
		vd = (double)spec[i];
		switch(kind[i]){
		case Dpixels:
			vd = vd*spix;
			break;
		case Dpercent:
			vd = vd*(double)t*spcnt/100.0;
			break;
		case Drelative:
			vd = vd*relu;
			break;
		case Dnone:
			vd = relu;
			break;
		}
		x[i] = (int)(vd+.5);
		tt += x[i];
	}
	x[n - 1] = t - tt;
	*ret = x;
	free(spec);
	free(kind);
}

Image *
getbg(Page *p)
{
	Docinfo *d;
	Cimage *ci;
	Image *bg;

	d = p->doc;
	if(d->backgrounditem){
		if(d->backgrounditem->aux){
			ci = d->backgrounditem->aux;
			if(ci->mi)
				getimage(ci, d->backgrounditem->altrep);
			bg = ci->i;
		}else
			bg = display->white;
	}else
		bg = getcolor(d->background.color);

	return bg;
}

Rune *
getbase(Page *p)
{
	if(p->doc)
		return p->doc->base;
	if(p->url->act.r)
		return p->url->act.r;
	return p->url->src.r;
}

Image *
eallocimage(Display *d, Rectangle r, ulong chan, int repl, int col)
{
	Image *i;

	i = allocimage(d, r, chan, repl, col);
	if(i == nil)
		error("allocimage failed");
	return i;
}

void
rect3d(Image *im, Rectangle r, int i, Image **c, Point sp)
{
	Point p[6];

	if(i < 0){
		r = insetrect(r, i);
		sp = addpt(sp, Pt(i,i));
		i = -i;
	}
	draw(im, Rect(r.min.x+i, r.min.y+i, r.max.x-i, r.max.y-i), c[2], nil, sp);
	p[0] = r.min;
	p[1] = Pt(r.min.x, r.max.y);
	p[2] = Pt(r.min.x+i, r.max.y-i);
	p[3] = Pt(r.min.x+i, r.min.y+i);
	p[4] = Pt(r.max.x-i, r.min.y+i);
	p[5] = Pt(r.max.x, r.min.y);
	fillpoly(im, p, 6, 0, c[0], sp);
	p[0] = r.max;
	p[1] = Pt(r.min.x, r.max.y);
	p[2] = Pt(r.min.x+i, r.max.y-i);
	p[3] = Pt(r.max.x-i, r.max.y-i);
	p[4] = Pt(r.max.x-i, r.min.y+i);
	p[5] = Pt(r.max.x, r.min.y);
	fillpoly(im, p, 6, 0, c[1], sp);
}

void
ellipse3d(Image *im, Point p, int rad, int i, Image **c, Point sp)
{
	fillarc(im, p, rad, rad, c[0], sp, 45, 180);
	fillarc(im, p, rad, rad, c[1], sp,  45, -180);
	fillellipse(im, p, rad-i, rad-i, c[2], sp);
}

void
colarray(Image **c, Image *c0, Image *c1, Image *c2, int checked)
{
	if(checked){
		c[0] = c0;
		c[1] = c1;
	}else{
		c[0] = c1;
		c[1] = c0;
	}
	c[2] = c2;
}

static char *deffontpaths[] = {
#include "fonts.h"
};

static char *fontpaths[NumFnt];
static Font *fonts[NumFnt];

void
initfontpaths(void)
{
	Biobufhdr *bp;
	char buf[128];
	char *s;
	int i;

	/* we don't care if getenv(2) fails */
	s = getenv("home");
	snprint(buf, sizeof(buf)-1, "%s/lib/abaco.fonts", s);
	free(s);
	if((bp=Bopen(buf, OREAD)) == nil)
		goto Default;

	for(i=0; i<NumFnt; i++)
		if((fontpaths[i]=Brdstr(bp, '\n', 1)) == nil)
			goto Error;

	Bterm(bp);
	return;
Error:
	fprint(2, "abaco: not enough fontpaths in '%s'\n", buf);
	Bterm(bp);
	for(i--; i>=0; i--)
		free(fontpaths[i]);
Default:
	for(i=0; i<NumFnt; i++)
		fontpaths[i] = deffontpaths[i];
}

Font *
getfont(int i)
{
	if(fonts[i] == nil){
		fonts[i] = openfont(display, fontpaths[i]);
		if(fonts[i] == nil)
			error("can't open font file");
	}
	return fonts[i];
}

typedef struct Color Color;

struct Color {
	int	rgb;
	Image	*i;
	Color	*next;
};

enum {
	NHASH = 19,
};

static Color *colortab[NHASH];

Image *
getcolor(int rgb)
{
	Color *c;
	int h;

	if(rgb == 0xFFFFFF)
		return display->white;
	else if(rgb == 0x000000)
		return display->black;

	h = rgb%NHASH;
	for(c=colortab[h]; c!=nil; c=c->next)
		if(c->rgb == rgb){
			flushimage(display, 0);	/* BUG? */
			return c->i;
		}
	c = emalloc(sizeof(Color));
	c->i = eallocimage(display, Rect(0,0,1,1), screen->chan, 1, (rgb<<8)|0xFF);
	c->rgb = rgb;
	c->next = colortab[h];
	colortab[h] = c;

	return c->i;
}

int
plumbrunestr(Runestr *rs, char *attr)
{
	Plumbmsg *m;
	int i;

	i = -1;
	if(plumbsendfd >= 0){
		m = emalloc(sizeof(Plumbmsg));
		m->src = estrdup("abaco");
		m->dst = nil;
		m->wdir = estrdup("/tmp");
		m->type = estrdup("text");
		if(attr)
			m->attr = plumbunpackattr(attr);
		else
			m->attr = nil;
		m->data = smprint("%.*S", rs->nr, rs->r);
		m->ndata = -1;
		i = plumbsend(plumbsendfd, m);
		plumbfree(m);
	}
	return i;
}

int
hexdigit(int v)
{
	if(0<=v && v<=9)
		return '0' + v;
	else
		return 'A' + v - 10;
}

static int
inclass(char c, Rune* cl)
{
	int n, ans, negate, i;

	n = runestrlen(cl);
	if(n == 0)
		return 0;
	ans = 0;
	negate = 0;
	if(cl[0] == '^'){
		negate = 1;
		cl++;
		n--;
	}
	for(i=0; i<n; i++){
		if(cl[i]=='-' && i>0 && i<n-1){
			if(c>=cl[i - 1] && c<=cl[i+1]){
				ans = 1;
				break;
			}
			i++;
		}
		else if(c == cl[i]){
			ans = 1;
			break;
		}
	}
	if(negate)
		ans = !ans;
	return ans;
}

Rune*
ucvt(Rune* s)
{
	Rune* u;
	char *t;
	int i, c, n, j, len;

	t = smprint("%S", s);
	n = strlen(t);
	len = 0;
	for(i=0; i<n; i++){
		c = t[i];
		if(inclass(c, L"- /$_@.!*'(),a-zA-Z0-9"))
			len++;
		else
			len += 3;
	}
	u = runemalloc(len+1);
	j = 0;

	for(i=0; i<n; i++){
		c = t[i];
		if(inclass(c, L"-/$_@.!*'(),a-zA-Z0-9"))
			u[j++] = c;
		else if(c == ' ')
			u[j++] = '+';
		else {
			u[j++] = '%';
			u[j++] = hexdigit((c >> 4)&15);
			u[j++] = hexdigit(c&15);
		}
	}
	u[j] = 0;
	free(t);
	return u;
}

void
reverseimages(Iimage **head)
{
	Iimage *r, *c, *n;

	r = nil;
	for(c=*head; c!=nil; c=n){
		n = c->nextimage;
		c->nextimage = r;
		r = c;
	}
	*head = r;
}

char urlexpr[] = "^(https?|ftp|file|gopher|mailto|news|nntp|telnet|wais|"
	"prospero)://[^/]+";
Reprog	*urlprog;

int
validurl(Rune *r)
{
	Resub rs[10];

	if(urlprog == nil){
		urlprog = regcomp(urlexpr);
		if(urlprog == nil)
			error("regcomp");
	}
	memset(rs, 0, sizeof(rs));
	if(rregexec(urlprog, r, rs, nelem(rs)) == 0)
		return FALSE;
	return TRUE;
}

static void
execproc(void *v)
{
	Exec *e;

	threadsetname("execproc");
	e = v;
	rfork(RFFDG);
	dup(e->p[0], 0);
	close(e->p[0]);
	close(e->p[1]);
	if(e->q[0]){
		dup(e->q[1], 1);
		close(e->q[0]);
		close(e->q[1]);
	}
	if(!procstderr)
		close(2);
	procexecl(e->sync, "/bin/rc", "rc", "-c", e->cmd, nil);
	error("can't exec");
}

int
pipeline(int fd, char *cmd, ...)
{
	Exec *e;
	va_list a;

	e = emalloc(sizeof(Exec));
	if(pipe(e->p)<0 || pipe(e->q)<0)
		error("can't create pipe");
	close(e->p[0]);
	e->p[0] = fd;
	va_start(a, cmd);
	e->cmd = vsmprint(cmd, a);
	va_end(a);
	e->sync = chancreate(sizeof(ulong), 0);
	if(e->sync == nil)
		error("can't create channel");
	proccreate(execproc, e, STACK);
	recvul(e->sync);
	chanfree(e->sync);
	free(e->cmd);
	close(e->p[0]);
	close(e->p[1]);
	close(e->q[1]);
	fd = e->q[0];
	free(e);
	return fd;
}

static
int
isspace(char c)
{
	return c==' ' || c== '\t' || c=='\r' || c=='\n';
}

int
findctype(char *b, int l, char *keyword, char *s)
{
	char *p, *e, c;
	int i;

	p = cistrstr(s, keyword);
	if(!p)
		return -1;
	p += strlen(keyword);
	while(*p && isspace(*p))
		p++;
	if(*p != '=')
		return -1;
	p++;
	while(*p && isspace(*p))
		p++;
	if(!*p)
		return -1;
	switch (c = *p){
	case '"':
	case '\'':
		p++;
		e = strchr(p, c);
		if(!e)
			return -1;
		break;
	default:
		for(e = p; *e < 127 && *e > ' ' ; e++)
			;
	}
	i = utfnlen(p, e - p);
	if(i < 1)
		return -1;
	snprint(b, l, "%.*s", i, p);
	return 0;
}

int
xtofchar(Rune *s, Font *f, long p)
{
	Rune *r;
	int q;

	if(p == 0)
		return 0;

	q = 0;
	for(r=s; *r!=L'\0'; r++){
		p -= runestringnwidth(f, r, 1);
		if(p < 0)
			break;
		q++;
	}
	return q;
}

int
istextsel(Page *p, Rectangle r, int *q0, int *q1, Rune *s, Font *f)
{
	int topinr, botinr;

	*q0 = *q1 = 0;
	topinr= ptinrect(p->top, r);
	if(topinr || (r.min.y>p->top.y && r.max.y<p->bot.y))
		p->selecting = TRUE;
	botinr = ptinrect(p->bot, r);
	if(botinr || r.min.y>p->bot.y)
		p->selecting = FALSE;

	if(topinr || botinr){
		if(topinr)
			*q0 = xtofchar(s, f, p->top.x-r.min.x);
		if(botinr)
			*q1 = xtofchar(s, f, p->bot.x-r.min.x);
		if(*q0!=0 || *q1!=0)
			return TRUE;
	}
	return p->selecting;
}

Point
getpt(Page *p, Point xy)
{
	xy.x = xy.x-p->r.min.x+p->pos.x;
	xy.y = xy.y-p->r.min.y+p->pos.y;

	return xy;
}

void
getimage(Cimage *ci, Rune *altr)
{
	Rectangle r;
	Memimage *mi;
	Image *i, *i2;
	char buf[128];
	uchar *bits;
	int nbits;

	mi = ci->mi;
	if(mi == nil){
		snprint(buf, sizeof(buf), "[%S]", altr ? altr : L"IMG");
		r.min = Pt(0, 0);
		r.max.x = 2*Space + stringwidth(font, buf);
		r.max.y = 2*Space + font->height;
		ci->i = eallocimage(display, r, GREY1, 1, DBlack);
		r.min.x += Space;
		r.min.y += Space;
		string(ci->i, r.min, display->white, ZP, font, buf);
		return;
	}
	nbits = bytesperline(mi->r, mi->depth)*Dy(mi->r);
	bits = emalloc(nbits);
	unloadmemimage(mi, mi->r, bits, nbits);
/*
	/* get rid of alpha channel from transparent gif * /

	if(mi->depth == 16){
		for(y=1; y<nbits; y+=2)
			bits[y>>1] = bits[y];
	}
*/
	i = eallocimage(display, mi->r, mi->chan, 0, DNofill);
	loadimage(i, i->r, bits, nbits);
	i2 = eallocimage(display, i->r, RGB24, 1, DNofill);
	draw(i2, i2->r, display->black, nil, ZP);
	draw(i2, i2->r, i, nil, i->r.min);
	free(bits);
	freememimage(mi);
	freeimage(i);
	ci->i = i2;
	ci->mi = nil;
}

static
void
fixtext1(Item **list)
{
	Itext *text, *ntext;
	Item *it, *prev;
	Rune *s, *s1, *s2;
	int n;

	if(*list == nil)
		return;

	prev = nil;
	for(it=*list; it!=nil; it=prev->next){
		if(it->tag!=Itexttag || forceitem(it))
			goto Continue;

		text = (Itext *)it;
		s = text->s;
		while(*s && isspacerune(*s))
			s++;
		if(!*s){
			if(prev == nil)
				prev = *list = it->next;
			else
				prev->next = it->next;

			it->next = nil;
			freeitems(it);
			if(prev == nil)
				return;
			continue;
		}
		n = 0;
		while(s[n] && !isspacerune(s[n]))
			n++;

		if(!s[n])
			goto Continue;

		s1 = runemalloc(n+1);
		s1 = runemove(s1, s, n);
		s1[n] = L'\0';
		s += n;

		while(*s && isspacerune(*s))
			s++;

		if(*s){
			n = runestrlen(s);
			s2 = runemalloc(n+1);
			runemove(s2, s, n);
			s2[n] = L'\0';
			ntext = emalloc(sizeof(Itext));
			ntext->s = s2;
			ntext->ascent = text->ascent;
			ntext->anchorid = text->anchorid;
			ntext->state = text->state&~(IFbrk|IFbrksp|IFnobrk|IFcleft|IFcright);
			ntext->tag = text->tag;
			ntext->fnt = text->fnt;
			ntext->fg = text->fg;
			ntext->ul = text->ul;
			ntext->next = (Item *)text->next;
			text->next = (Item *)ntext;
		}
		free(text->s);
		text->s = s1;
    Continue:
		prev = it;
	}
}

void
fixtext(Page *p)
{
	Tablecell *c;
	Table *t;

	fixtext1(&p->items);
	for(t=p->doc->tables; t!=nil; t=t->next)
		for(c=t->cells; c!=nil; c=c->next)
			fixtext1(&c->content);
}

typedef struct Refresh Refresh;

struct Refresh
{
	Page *p;
	Refresh *next;
};

static Refresh *refreshs = nil;
static QLock refreshlock;

void
addrefresh(Page *p, char *fmt, ...)
{
	Refresh *r;
	Rune *s;
	va_list arg;

	if(p->aborting)
		return;

	va_start(arg, fmt);
	s = runevsmprint(fmt, arg);
	va_end(arg);
	if(s == nil)
		error("runevsmprint failed");

	qlock(&refreshlock);
	if(p->status){
		free(p->status);
		p->status = nil;
	}
	p->status = s;
	for(r=refreshs; r!=nil; r=r->next)
		if(r->p == p)
			goto Return;

	incref(p->w);				/* flushrefresh will decref */
	r = emalloc(sizeof(Refresh));
	r->p = p;
	r->next = refreshs;
	refreshs = r;

    Return:
	nbsendp(crefresh, nil);
	qunlock(&refreshlock);
}

/* called while row is locked */
void
flushrefresh(void)
{
	Refresh *r, *next;
	Page *p;

	qlock(&refreshlock);
	for(r=refreshs; r!=nil; r=next){
		p = r->p;
		if(p->changed==TRUE && p->aborting==FALSE){
			p->changed = FALSE;
			if(p->parent==nil || p->loading==FALSE)
				pagerender(p);
			if(!p->refresh.t)
				pagesetrefresh(p);
		}
		if(p->status){
			winsetstatus(p->w, p->status);
			free(p->status);
			p->status = nil;
		}
		winseturl(p->w);
		winsettag(p->w);
		decref(p->w);
		next = r->next;
		free(r);
	}
	refreshs = nil;
	qunlock(&refreshlock);
}

void
savemouse(Window *w)
{
	prevmouse = mouse->xy;
	mousew = w;
}

void
restoremouse(Window *w)
{
	if(mousew!=nil && mousew==w)
		moveto(mousectl, prevmouse);
	mousew = nil;
}

void
clearmouse()
{
	mousew = nil;
}

/*
 * Heuristic city.
 */
Window*
makenewwindow(Page *p)
{
	Column *c;
	Window *w, *bigw, *emptyw;
	Page *emptyp;
	int i, y, el;

	if(activecol)
		c = activecol;
	else if(selpage && selpage->col)
		c = selpage->col;
	else if(p && p->col)
		c = p->col;
	else{
		if(row.ncol==0 && rowadd(&row, nil, -1)==nil)
			error("can't make column");
		c = row.col[row.ncol-1];
	}
	activecol = c;
	if(p==nil || p->w==nil || c->nw==0)
		return coladd(c, nil, nil, -1);

	/* find biggest window and biggest blank spot */
	emptyw = c->w[0];
	bigw = emptyw;
	for(i=1; i<c->nw; i++){
		w = c->w[i];
		/* use >= to choose one near bottom of screen */
		if(Dy(w->page.all) >= Dy(bigw->page.all))
			bigw = w;
		if(w->page.lay==nil && Dy(w->page.all) >= Dy(emptyw->page.all))
			emptyw = w;
	}
	emptyp = &emptyw->page;
	el = Dy(emptyp->all);
	/* if empty space is big, use it */
	if(el>15 || (el>3 && el>(Dy(bigw->page.all)-1)/2))
		y = emptyp->all.max.y;
	else{
		/* if this window is in column and isn't much smaller, split it */
		if(p->col==c && Dy(p->w->r)>2*Dy(bigw->r)/3)
			bigw = p->w;
		y = (bigw->r.min.y + bigw->r.max.y)/2;
	}
	w = coladd(c, nil, nil, y);
	colgrow(w->col, w, 1);
	return w;
}
