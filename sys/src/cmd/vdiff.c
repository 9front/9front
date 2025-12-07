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
typedef struct Patch Patch;

struct Block {
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

struct Patch {
	char *name;
	Block **blocks;
	int nblocks;
};

enum
{
	Lfile = 0,
	Lsep,
	Ladd,
	Ldel,
	Lnone,
	Ncols,

	Lterm = -1,
	Lhash = -2,
};

enum
{
	Mcollapse,
	Mexpand,
	Nmenu,
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
Image *trlcol;
Image *bord;
Image *expander[2];
Image *fb;
int totalh;
int viewh;
int scrollsize;
int offset;
int lineh;
int scrolling;
int oldbuttons;
Patch *patches;
int npatches;
Patch *cur;
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

Image*
eallocimage(Rectangle r, int repl, ulong n)
{
	Image *i;

	i = allocimage(display, r, screen->chan, repl, n);
	if(i == nil)
		sysfatal("allocimage: %r");
	return i;
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
	Point p0, p;
	Rectangle trlr;
	Rune  rn;
	char *s;
	int off, tab, nc, hastrl;

	draw(b, r, cols[lt].bg, nil, ZP);
	p = Pt(r.min.x + pad + Hpadding, r.min.y + (Dy(r)-font->height)/2);
	off = Δpan / spacew;
	hastrl = 0;
	for(s = ls, nc = -1, tab = 0; *s || tab > 0; nc++, tab--, off--){
		if(tab <= 0 && *s && *s == '\t'){
			tab = 4 - nc % 4;
			s++;
		}
		if(tab > 0){
			p0 = p;
			if(off <= 0)
				p = runestring(b, p, cols[lt].bg, ZP, font, L"█");
			if(hastrl)
				trlr.max = addpt(p, Pt(0, font->height));
			else{
				trlr.min = p0;
				hastrl = 1;
			}
		}else if((p.x+Hpadding+spacew+ellipsisw>=b->r.max.x)){
			string(b, p, cols[lt].fg, ZP, font, ellipsis);
			break;
		}else{
			s += chartorune(&rn, s);
			p0 = p;
			if(off <= 0)
				p = runestringn(b, p, cols[lt].fg, ZP, font, &rn, 1);
			if(isspacerune(rn)){
				if(hastrl)
					trlr.max = addpt(p, Pt(0, font->height));
				else{
					trlr.min = p0;
					hastrl = 1;
				}
			}else if(hastrl)
				hastrl = 0;
		}
	}

	if(hastrl)
		draw(b, trlr, trlcol, nil, ZP);
}

void
renderblock(Block *b, Rectangle sr)
{
	Rectangle r, lr, br;
	Line *l;
	int i, pad;

	pad = 0;
	r = insetrect(sr, 1);
	if(b->f != nil){
		pad = Margin;
		lr = r;
		lr.max.y = lr.min.y + lineh;
		br = rectaddpt(expander[0]->r, Pt(lr.min.x+Hpadding, lr.min.y+Vpadding));
		border(fb, sr, 1, bord, ZP);
		renderline(fb, lr, Dx(expander[0]->r)+Hpadding, Lfile, b->f);
		draw(fb, br, expander[b->v], nil, ZP);
		r.min.y += lineh;
	}
	if(b->v == 0)
		return;
	for(i = 0; i < b->nlines; i++){
		l = b->lines[i];
		lr = Rect(r.min.x, r.min.y+i*lineh, r.max.x, r.min.y+(i+1)*lineh);
		renderline(fb, lr, pad, l->t, l->s);
	}
}

void
redraw(void)
{
	Rectangle clipr;
	int i, h, y, ye, vmin, vmax;
	Block *b;

	draw(fb, fb->r, cols[Lnone].bg, nil, ZP);
	draw(fb, scrollr, scrlcol.bg, nil, ZP);
	if(viewh < totalh){
		h = ((double)viewh/totalh)*Dy(scrollr);
		y = ((double)offset/totalh)*Dy(scrollr);
		ye = scrollr.min.y + y + h;
		if(ye >= scrollr.max.y)
			ye = scrollr.max.y;
		scrposr = Rect(scrollr.min.x, scrollr.min.y+y+1, scrollr.max.x-1, ye);
	}else
		scrposr = Rect(scrollr.min.x, scrollr.min.y, scrollr.max.x-1, scrollr.max.y);
 	draw(fb, scrposr, scrlcol.fg, nil, ZP);
	vmin = viewr.min.y + offset;
	vmax = viewr.max.y + offset;
	clipr = screen->clipr;
	replclipr(fb, 0, viewr);
	for(i = 0; i < cur->nblocks; i++){
		b = cur->blocks[i];
		if(b->sr.min.y <= vmax && b->sr.max.y >= vmin)
			renderblock(b, rectaddpt(b->sr, Pt(0, -offset)));
	}
	replclipr(fb, 0, clipr);
	draw(screen, screen->r, fb, nil, fb->r.min);
	flushimage(display, 1);
}

void
pan(int off)
{
	int max;

	max = Dx(scrollr) + Margin + Hpadding + maxlength*spacew + 2*ellipsisw + Hpadding + Margin - Dx(cur->blocks[0]->r)/2;
	Δpan += off * spacew;
	if(Δpan < 0 || max <= 0)
		Δpan = 0;
	else if(Δpan > max)
		Δpan = max;
	redraw();
}

void
clampoffset(int off)
{
	if(offset<0)
		offset = 0;
	if(offset+viewh>totalh){
		if(off > 0)
			offset = totalh - viewh;
		else
			offset = 0;
	}
}

void
scroll(int off)
{
	if(off<0 && offset<=0)
		return;
	if(off>0 && offset+viewh>totalh)
		return;
	offset += off;
	clampoffset(off);
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
	for(i = 0; i < cur->nblocks; i++){
		b = cur->blocks[i];
		blockresize(b);
		b->sr = rectaddpt(b->r, p);
		p.y += Margin + Dy(b->r);
		totalh += Margin + Dy(b->r);
	}
	totalh = totalh - Margin + Vpadding;
	scrollsize = viewh / 2.0;
	if(viewh <= totalh)
		clampoffset(1);
	else
		clampoffset(0);
	freeimage(fb);
	fb = eallocimage(screen->r, 0, DBlack);
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

char*
genmenu(int i)
{
	switch(i){
	case Mcollapse:
		return "collapse";
	case Mexpand:
		return "expand";
	default:
		i -= Nmenu;
		if(i >= npatches || npatches == 1)
			return nil;
		if(patches[i].name == nil)
			if((patches[i].name = smprint("%d", i)) == nil)
				sysfatal("smprint: %r");
		return patches[i].name;
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

Menu menu = {
	nil,
	genmenu,
};

void
collapse(int v)
{
	int i;

	for(i = 0; i < cur->nblocks; i++)
		if(cur->blocks[i]->f != nil)
			cur->blocks[i]->v = v;
	eresize(0);
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

	n = (m.xy.y - viewr.min.y - Margin)/lineh * lineh;
	if(scrolling){
		if(m.buttons&1){
			scroll(-n);
			return;
		}else if(m.buttons&2){
			offset = (m.xy.y - scrollr.min.y) * totalh/Dy(scrollr);
			offset = offset/lineh * lineh;
			if(viewh <= totalh)
				clampoffset(1);
			else
				clampoffset(0);
			redraw();
		}else if(m.buttons&4){
			scroll(n);
			return;
		}
	}else if(!scrolling && m.buttons&2){
		n = menuhit(2, mctl, &menu, nil);
		switch(n){
		case -1:
			break;
		case Mexpand: case Mcollapse:
			collapse(n);
			break;
		default:
			n -= Nmenu;
			if(cur == patches+n)
				break;
			cur = patches+n;
			eresize(0);
		}
	}else if(m.buttons&8){
		scroll(-n);
	}else if(m.buttons&16){
		scroll(n);
	}else if((oldbuttons^m.buttons) != 0 && ptinrect(m.xy, viewr)){
		for(i = 0; i < cur->nblocks; i++){
			b = cur->blocks[i];
			if(ptinrect(addpt(m.xy, Pt(0, offset)), b->sr)){
				blockmouse(b, m);
				break;
			}
		}
	}
	oldbuttons = m.buttons;
}

void
initcol(Col *c, ulong fg, ulong bg)
{
	Rectangle r;

	r = Rect(0, 0, 1, 1);
	c->fg = eallocimage(r, 1, fg);
	c->bg = eallocimage(r, 1, bg);
}

void
initcols(int black)
{
	Rectangle r;

	r = Rect(0, 0, 1, 1);
	if(black){
		bord = eallocimage(r, 1, 0x888888FF^(~0xFF));
		initcol(&scrlcol,     DBlack, 0x999999FF^(~0xFF));
		initcol(&cols[Lfile], DWhite, 0x333333FF);
		initcol(&cols[Lsep],  DBlack, DPurpleblue);
		initcol(&cols[Ladd],  DWhite, 0x002800FF);
		initcol(&cols[Ldel],  DWhite, 0x3F0000FF);
		initcol(&cols[Lnone], DWhite, DBlack);
		trlcol = eallocimage(r, 1, 0x9F0000FF);
	}else{
		bord = eallocimage(r, 1, 0x888888FF);
		initcol(&scrlcol,     DWhite, 0x999999FF);
		initcol(&cols[Lfile], DBlack, 0xEFEFEFFF);
		initcol(&cols[Lsep],  DBlack, 0xEAFFFFFF);
		initcol(&cols[Ladd],  DBlack, 0xE6FFEDFF);
		initcol(&cols[Ldel],  DBlack, 0xFFEEF0FF);
		initcol(&cols[Lnone], DBlack, DWhite);
		trlcol = eallocimage(r, 1, 0xFF8890FF);
	}
}

void
initicons(void)
{
	int w, h;
	Point p[4];

	w = font->height;
	h = font->height;
	expander[0] = eallocimage(Rect(0, 0, w, h), 0, DNofill);
	draw(expander[0], expander[0]->r, cols[Lfile].bg, nil, ZP);
	p[0] = Pt(0.25*w, 0.25*h);
	p[1] = Pt(0.25*w, 0.75*h);
	p[2] = Pt(0.75*w, 0.5*h);
	p[3] = p[0];
	fillpoly(expander[0], p, 4, 0, bord, ZP);
	expander[1] = eallocimage(Rect(0, 0, w, h), 0, DNofill);
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
	b->v = 1;
	b->f = nil;
	b->lines = nil;
	b->nlines = 0;
	if(cur->nblocks%Meminc == 0)
		cur->blocks = erealloc(cur->blocks, (cur->nblocks+Meminc)*sizeof *cur->blocks);
	cur->blocks[cur->nblocks++] = b;
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
	if(utfncmp(text, "⑨", 1) == 0)
		type = Lterm;
	else if(strncmp(text, "diff", 4)==0)
		type = Lhash;
	else if(strncmp(text, "+++", 3)==0)
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
	if(p == nil)
		sysfatal("strdup: %r");
	n = tokenize(p, t, 5);
	if(n<=0)
		return -1;
	l = atoi(t[2]);
	free(p);
	return l;
}

void
parse(int fd, char *name)
{
	Biobuf *bp;
	Block *b;
	char *s, *f, *tab;
	int t, n, ab, len;
	int gotterm;

	bp = Bfdopen(fd, OREAD);
	if(bp==nil)
		sysfatal("Bfdopen: %r");
	gotterm = 0;
	goto New;
	for(;;){
		s = Brdstr(bp, '\n', 1);
		if(s==nil)
			break;
		t = linetype(s);
		switch(t){
		case Lterm:
			gotterm = 1;
			/* remove '--' and extra newline */
			b->nlines--;
			free(s);
			free(Brdstr(bp, '\n', 1));
		New:
			npatches++;
			patches = erealloc(patches, sizeof *patches * npatches);
			cur = patches+npatches-1;
			cur->blocks = nil;
			cur->nblocks = 0;
			cur->name = name;
			b = addblock();
			n = 0;
			ab = 0;
			break;
		case Lfile:
			if(s[0] == '-'){
				b = addblock();
				b->f = s+4;
				if(strncmp(b->f, "a/", 2) == 0){
					ab = 1;
					b->f++;
				}
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
				if(strcmp(f, "/dev/null") != 0)
					b->f = f;
				else
					free(s);
			}else
				free(s);
			break;
		case Lsep:
			n = lineno(s) - 1; /* -1 as the separator is not an actual line */
			if(0){
		case Lhash:
			f = strchr(s, ' ');
			if(f != nil && (f = strchr(f+1, ' '))){
				f++;
				if(strcmp(f, "uncommitted") != 0){
					if(name != nil)
						cur->name = smprint("%s %.*s", name, 9, f);
					else
						cur->name = smprint("%.*s", 9, f);
					if(cur->name == nil)
						sysfatal("smprint: %r");
				}
			}
			t = Lnone;
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
	if(gotterm)
		npatches--;
	Bterm(bp);
}

void
usage(void)
{
	fprint(2, "usage: %s [-b] [-p nstrip] [patches...]\n", argv0);
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
	int i, b, fd;

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
	if(argc == 0)
		parse(0, nil);
	else for(i = 0; i < argc; i++){
		fd = open(argv[i], OREAD);
		if(fd < 0)
			sysfatal("open: %r");
		parse(fd, argv[i]);
		close(fd);
	}
	cur = patches;
	if(cur->nblocks==1 && cur->blocks[0]->nlines==0){
		fprint(2, "no diff\n");
		exits(nil);
	}
	if(initdraw(nil, nil, "vdiff")<0)
		sysfatal("initdraw: %r");
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
