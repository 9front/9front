#include <u.h>
#include <libc.h>
#include <plumb.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <keyboard.h>
#include <bio.h>

enum { Meminc = 32 };

typedef struct Block Block;
typedef struct Line Line;
typedef struct Col Col;

struct Block {
	Image *b;
	Rectangle r;
	Rectangle sr;
	int v;
	char *f;
	Line **lines;
	int nlines;
};

struct Line {
	int t;
	int n;
	char *s;
};

struct Col {
	Image *bg;
	Image *fg;
};

enum
{
	Lfile = 0,
	Lsep,
	Ladd,
	Ldel,
	Lnone,
	Ncols,
};	

enum
{
	Scrollwidth = 12,
	Scrollgap = 2,
	Margin = 8,
	Hpadding = 4,
	Vpadding = 2,
};

Mousectl *mctl;
Keyboardctl *kctl;
Rectangle sr;
Rectangle scrollr;
Rectangle scrposr;
Rectangle viewr;
Col cols[Ncols];
Col scrlcol;
Image *bord;
Image *expander[2];
int totalh;
int viewh;
int scrollsize;
int offset;
int lineh;
int scrolling;
int oldbuttons;
Block **blocks;
int nblocks;
int maxlength;
int Δpan;
int nstrip;
const char ellipsis[] = "...";
int ellipsisw;
int spacew;

void*
emalloc(ulong n)
{
	void *p;

	p = malloc(n);
	if(p == nil)
		sysfatal("malloc: %r");
	return p;
}

void*
erealloc(void *p, ulong n)
{
	void *q;

	q = realloc(p, n);
	if(q == nil)
		sysfatal("realloc: %r");
	return q;
}

void
plumb(char *f, int l)
{
	int fd, i;
	char *p, wd[256], addr[300]={0};

	fd = plumbopen("send", OWRITE);
	if(fd<0)
		return;
	for(i = 0; i < nstrip; i++)
		if((p = strchr(f, '/')) != nil)
			f = p+1;
	getwd(wd, sizeof wd);
	snprint(addr, sizeof addr, "%s:%d", f, l);
	plumbsendtext(fd, "vdiff", "edit", wd, addr);
	close(fd);
}

void
renderline(Image *b, Rectangle r, int pad, int lt, char *ls)
{
	Point p;
	Rune  rn;
	char *s;
	int off, tab, nc;

	draw(b, r, cols[lt].bg, nil, ZP);
	p = Pt(r.min.x + pad + Hpadding, r.min.y + (Dy(r)-font->height)/2);
	off = Δpan / spacew;
	for(s = ls, nc = -1, tab = 0; *s; nc++, tab--, off--){
		if(tab <= 0 && *s == '\t'){
			tab = 4 - nc % 4;
			s++;
		}
		if(tab > 0){
			if(off <= 0)
				p = runestring(b, p, cols[lt].bg, ZP, font, L"█");
		}else if((p.x+Hpadding+spacew+ellipsisw>=b->r.max.x)){
			string(b, p, cols[lt].fg, ZP, font, ellipsis);
			break;
		}else{
			s += chartorune(&rn, s);
			if(off <= 0)
				p = runestringn(b, p, cols[lt].fg, ZP, font, &rn, 1);
		}
	}
}

void
renderblock(Block *b)
{
	Rectangle r, lr, br;
	Line *l;
	int i, pad;

	pad = 0;
	r = insetrect(b->r, 1);
	draw(b->b, b->r, cols[Lnone].bg, nil, ZP);
	if(b->f != nil){
		pad = Margin;
		lr = r;
		lr.max.y = lineh;
		br = rectaddpt(expander[0]->r, Pt(lr.min.x+Hpadding, lr.min.y+Vpadding));
		border(b->b, b->r, 1, bord, ZP);
		renderline(b->b, lr, Dx(expander[0]->r)+Hpadding, Lfile, b->f);
		draw(b->b, br, expander[b->v], nil, ZP);
		r.min.y += lineh;
	}
	if(b->v == 0)
		return;
	for(i = 0; i < b->nlines; i++){
		l = b->lines[i];
		lr = Rect(r.min.x, r.min.y+i*lineh, r.max.x, r.min.y+(i+1)*lineh);
		renderline(b->b, lr, pad, l->t, l->s);
	}
}

void
redraw(void)
{
	Rectangle clipr;
	int i, h, y, ye, vmin, vmax;
	Block *b;

	draw(screen, sr, cols[Lnone].bg, nil, ZP);
	draw(screen, scrollr, scrlcol.bg, nil, ZP);
	if(viewh < totalh){
		h = ((double)viewh/totalh)*Dy(scrollr);
		y = ((double)offset/totalh)*Dy(scrollr);
		ye = scrollr.min.y + y + h - 1;
		if(ye >= scrollr.max.y)
			ye = scrollr.max.y - 1;
		scrposr = Rect(scrollr.min.x, scrollr.min.y+y+1, scrollr.max.x-1, ye);
	}else
		scrposr = Rect(scrollr.min.x, scrollr.min.y, scrollr.max.x-1, scrollr.max.y);
	draw(screen, scrposr, scrlcol.fg, nil, ZP);
	vmin = viewr.min.y + offset;
	vmax = viewr.max.y + offset;
	clipr = screen->clipr;
	replclipr(screen, 0, viewr);
	for(i = 0; i < nblocks; i++){
		b = blocks[i];
		if(b->sr.min.y <= vmax && b->sr.max.y >= vmin){
			renderblock(b);
			draw(screen, rectaddpt(b->sr, Pt(0, -offset)), b->b, nil, ZP);
		}
	}
	replclipr(screen, 0, clipr);
	flushimage(display, 1);
}

void
pan(int off)
{
	int max;

	max = Hpadding + Margin + Hpadding + maxlength * spacew + 2 * ellipsisw - Dx(blocks[0]->r);
	Δpan += off * spacew;
	if(Δpan < 0 || max <= 0)
		Δpan = 0;
	else if(Δpan > max)
		Δpan = max;
	redraw();
}

void
clampoffset(void)
{
	if(offset<0)
		offset = 0;
	if(offset+viewh>totalh)
		offset = totalh - viewh;
}

void
scroll(int off)
{
	if(off<0 && offset<=0)
		return;
	if(off>0 && offset+viewh>totalh)
		return;
	offset += off;
	clampoffset();
	redraw();
}

void
blockresize(Block *b)
{
	int w, h;

	w = Dx(viewr) - 2; /* add 2 for border */
	h = 0 + 2;
	if(b->f != nil)
		h += lineh;
	if(b->v)
		h += b->nlines*lineh;
	b->r = Rect(0, 0, w, h);
	freeimage(b->b);
	b->b = allocimage(display, b->r, screen->chan, 0, DNofill);
}

void
eresize(int new)
{
	Rectangle listr;
	Block *b;
	Point p;
	int i;

	if(new && getwindow(display, Refnone)<0)
		sysfatal("cannot reattach: %r");
	sr = screen->r;
	scrollr = sr;
	scrollr.max.x = scrollr.min.x+Scrollwidth+Scrollgap;
	listr = sr;
	listr.min.x = scrollr.max.x;
	viewr = insetrect(listr, Margin);
	viewh = Dy(viewr);
	lineh = Vpadding+font->height+Vpadding;
	totalh = - Margin + Vpadding + 1;
	p = addpt(viewr.min, Pt(0, totalh));
	for(i = 0; i < nblocks; i++){
		b = blocks[i];
		blockresize(b);
		b->sr = rectaddpt(b->r, p);
		p.y += Margin + Dy(b->r);
		totalh += Margin + Dy(b->r);
	}
	totalh = totalh - Margin + Vpadding;
	scrollsize = viewh / 2.0;
	if(offset > 0 && offset+viewh>totalh)
		offset = totalh - viewh;
	redraw();
}

void
ekeyboard(Rune k)
{
	switch(k){
	case 'q':
	case Kdel:
		threadexitsall(nil);
		break;
	case Khome:
		scroll(-totalh);
		break;
	case Kend:
		scroll(totalh);
		break;
	case Kpgup:
		scroll(-viewh);
		break;
	case Kpgdown:
		scroll(viewh);
		break;
	case Kup:
		scroll(-scrollsize);
		break;
	case Kdown:
		scroll(scrollsize);
		break;
	case Kleft:
		pan(-4);
		break;
	case Kright:
		pan(4);
		break;
	}
}

void
blockmouse(Block *b, Mouse m)
{
	Line *l;
	int n;

	n = (m.xy.y + offset - b->sr.min.y) / lineh;
	if(n == 0 && b->f != nil && m.buttons&1){
		b->v = !b->v;
		eresize(0);
	}else if(n > 0 && m.buttons&4){
		l = b->lines[n-1];
		if(l->t != Lsep)
			plumb(b->f, l->n);
	}
}

void
emouse(Mouse m)
{
	Block *b;
	int n, i;

	if(oldbuttons == 0 && m.buttons != 0 && ptinrect(m.xy, scrollr))
		scrolling = 1;
	else if(m.buttons == 0)
		scrolling = 0;

	n = (m.xy.y - scrollr.min.y);
	if(scrolling){
		if(m.buttons&1){
			scroll(-n);
			return;
		}else if(m.buttons&2){
			offset = (m.xy.y - scrollr.min.y) * totalh/Dy(scrollr);
			clampoffset();
			redraw();
		}else if(m.buttons&4){
			scroll(n);
			return;
		}
	}else if(m.buttons&8){
		scroll(-n);
	}else if(m.buttons&16){
		scroll(n);
	}else if(m.buttons != 0 && ptinrect(m.xy, viewr)){
		for(i = 0; i < nblocks; i++){
			b = blocks[i];
			if(ptinrect(addpt(m.xy, Pt(0, offset)), b->sr)){
				blockmouse(b, m);
				break;
			}
		}
	}
	oldbuttons = m.buttons;
}

Image*
ecolor(ulong n)
{
	Image *i;

	i = allocimage(display, Rect(0,0,1,1), screen->chan, 1, n);
	if(i == nil)
		sysfatal("allocimage: %r");
	return i;
}

void
initcol(Col *c, ulong fg, ulong bg)
{
	c->fg = ecolor(fg);
	c->bg = ecolor(bg);
}

void
initcols(int black)
{
	if(black){
		bord = ecolor(0x888888FF^(~0xFF));
		initcol(&scrlcol,     DBlack, 0x999999FF^(~0xFF));
		initcol(&cols[Lfile], DWhite, 0x333333FF);
		initcol(&cols[Lsep],  DBlack, DPurpleblue);
		initcol(&cols[Ladd],  DWhite, 0x002800FF);
		initcol(&cols[Ldel],  DWhite, 0x3F0000FF);
		initcol(&cols[Lnone], DWhite, DBlack);
	}else{
		bord = ecolor(0x888888FF);
		initcol(&scrlcol,     DWhite, 0x999999FF);
		initcol(&cols[Lfile], DBlack, 0xEFEFEFFF);
		initcol(&cols[Lsep],  DBlack, 0xEAFFFFFF);
		initcol(&cols[Ladd],  DBlack, 0xE6FFEDFF);
		initcol(&cols[Ldel],  DBlack, 0xFFEEF0FF);
		initcol(&cols[Lnone], DBlack, DWhite);
	}
}

void
initicons(void)
{
	int w, h;
	Point p[4];

	w = font->height;
	h = font->height;
	expander[0] = allocimage(display, Rect(0, 0, w, h), screen->chan, 0, DNofill);
	draw(expander[0], expander[0]->r, cols[Lfile].bg, nil, ZP);
	p[0] = Pt(0.25*w, 0.25*h);
	p[1] = Pt(0.25*w, 0.75*h);
	p[2] = Pt(0.75*w, 0.5*h);
	p[3] = p[0];
	fillpoly(expander[0], p, 4, 0, bord, ZP);
	expander[1] = allocimage(display, Rect(0, 0, w, h), screen->chan, 0, DNofill);
	draw(expander[1], expander[1]->r, cols[Lfile].bg, nil, ZP);
	p[0] = Pt(0.25*w, 0.25*h);
	p[1] = Pt(0.75*w, 0.25*h);
	p[2] = Pt(0.5*w, 0.75*h);
	p[3] = p[0];
	fillpoly(expander[1], p, 4, 0, bord, ZP);
	flushimage(display, 0);
}

Block*
addblock(void)
{
	Block *b;

	b = emalloc(sizeof *b);
	b->b = nil;
	b->v = 1;
	b->f = nil;
	b->lines = nil;
	b->nlines = 0;
	if(nblocks%Meminc == 0)
		blocks = erealloc(blocks, (nblocks+Meminc)*sizeof *blocks);
	blocks[nblocks++] = b;
	return b;
}

void
addline(Block *b, int t, int n, char *s)
{
	Line *l;

	l = emalloc(sizeof *l);
	l->t = t;
	l->n = n;
	l->s = s;
	if(b->nlines%Meminc == 0)
		b->lines = erealloc(b->lines, (b->nlines+Meminc)*sizeof(Line*));
	b->lines[b->nlines++] = l;
}

int
linetype(char *text)
{
	int type;

	type = Lnone;
	if(strncmp(text, "+++", 3)==0)
		type = Lfile;
	else if(strncmp(text, "---", 3)==0){
		if(strlen(text) > 4)
			type = Lfile;
	}else if(strncmp(text, "@@", 2)==0)
		type = Lsep;
	else if(strncmp(text, "+", 1)==0)
		type = Ladd;
	else if(strncmp(text, "-", 1)==0)
		type = Ldel;
	return type;
}

int
lineno(char *s)
{
	char *p, *t[5];
	int n, l;

	p = strdup(s);
	n = tokenize(p, t, 5);
	if(n<=0)
		return -1;
	l = atoi(t[2]);
	free(p);
	return l;
}

void
parse(int fd)
{
	Biobuf *bp;
	Block *b;
	char *s, *f, *tab;
	int t, n, ab, len;

	blocks = nil;
	nblocks = 0;
	ab = 0;
	n = 0;
	bp = Bfdopen(fd, OREAD);
	if(bp==nil)
		sysfatal("Bfdopen: %r");
	b = addblock();
	for(;;){
		s = Brdstr(bp, '\n', 1);
		if(s==nil)
			break;
		t = linetype(s);
		switch(t){
		case Lfile:
			if(s[0] == '-'){
				b = addblock();
				if(strncmp(s+4, "a/", 2) == 0)
					ab = 1;
			}else if(s[0] == '+'){
				f = s+4;
				if(ab && strncmp(f, "b/", 2) == 0){
					f += 1;
					if(access(f, AEXIST) < 0)
						f += 1;
				}
				tab = strchr(f, '\t');
				if(tab != nil)
					*tab = 0;
				b->f = f;
			}
			break;
		case Lsep:
			n = lineno(s) - 1; /* -1 as the separator is not an actual line */
			if(0){
		case Ladd:
		case Lnone:
			++n;
			}
		default:
			addline(b, t, n, s);
			len = strlen(s);
			if(len > maxlength)
				maxlength = len;
			break;
		}
	}
}

void
usage(void)
{
	fprint(2, "%s [-b] [-p n]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	enum { Emouse, Eresize, Ekeyboard, };
	Mouse m;
	Rune k;
	Alt a[] = {
		{ nil, &m,  CHANRCV },
		{ nil, nil, CHANRCV },
		{ nil, &k,  CHANRCV },
		{ nil, nil, CHANEND },
	};
	int b;

	scrolling = 0;
	oldbuttons = 0;
	b = 0;
	ARGBEGIN{
	case 'b':
		b = 1;
		break;
	case 'p':
		nstrip = atoi(EARGF(usage()));
		break;
	default:
		usage();
		break;
	}ARGEND;

	parse(0);
	if(nblocks==0){
		fprint(2, "no diff\n");
		exits(nil);
	}
	if(initdraw(nil, nil, "vdiff")<0)
		sysfatal("initdraw: %r");
	display->locking = 0;
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((kctl = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");
	a[Emouse].c = mctl->c;
	a[Eresize].c = mctl->resizec;
	a[Ekeyboard].c = kctl->c;
	initcols(b);
	initicons();
	spacew = stringwidth(font, " ");
	ellipsisw = stringwidth(font, ellipsis);
	eresize(0);
	for(;;){
		switch(alt(a)){
		case Emouse:
			emouse(m);
			break;
		case Eresize:
			eresize(1);
			break;
		case Ekeyboard:
			ekeyboard(k);
			break;
		}
	}
}
