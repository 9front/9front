#include <u.h>
#include <libc.h>
#include <plumb.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <keyboard.h>
#include <bio.h>

typedef struct Line Line;
typedef struct Col Col;

struct Line {
	int t;
	char *s;
	char *f;
	int l;
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
Rectangle listr;
Rectangle textr;
Col cols[Ncols];
Col scrlcol;
int scrollsize;
int lineh;
int nlines;
int offset;
int scrolling;
int oldbuttons;
Line **lines;
int lsize;
int lcount;
int maxlength;
int Δpan;
const char ellipsis[] = "...";

void
plumb(char *f, int l)
{
	int fd;
	char wd[256], addr[300]={0};

	fd = plumbopen("send", OWRITE);
	if(fd<0)
		return;
	getwd(wd, sizeof wd);
	snprint(addr, sizeof addr, "%s:%d", f, l);
	plumbsendtext(fd, "vdiff", "edit", wd, addr);
	close(fd);
}

void
drawline(Rectangle r, Line *l)
{
	Point p;
	Rune  rn;
	char *s;
	int off, tab, nc;

	draw(screen, r, cols[l->t].bg, nil, ZP);
	p = Pt(r.min.x + Hpadding, r.min.y + (Dy(r)-font->height)/2);
	off = Δpan / stringwidth(font, " ");
	for(s = l->s, nc = -1, tab = 0; *s; nc++, tab--, off--){
		if(tab <= 0 && *s == '\t'){
			tab = 4 - nc % 4;
			s++;
		}
		if(tab > 0){
			if(off <= 0)
				p = runestring(screen, p, cols[l->t].bg, ZP, font, L"█");
		}else if((p.x+Hpadding+stringwidth(font, " ")+stringwidth(font, ellipsis)>=textr.max.x)){
			string(screen, p, cols[l->t].fg, ZP, font, ellipsis);
			break;
		}else{
			s += chartorune(&rn, s);
			if(off <= 0)
				p = runestringn(screen, p, cols[l->t].fg, ZP, font, &rn, 1);
		}
	}
}

void
redraw(void)
{
	Rectangle lr;
	int i, h, y;

	draw(screen, sr, cols[Lnone].bg, nil, ZP);
	draw(screen, scrollr, scrlcol.bg, nil, ZP);
	if(lcount>0){
		h = ((double)nlines/lcount)*Dy(scrollr);
		y = ((double)offset/lcount)*Dy(scrollr);
		scrposr = Rect(scrollr.min.x, scrollr.min.y+y, scrollr.max.x-1, scrollr.min.y+y+h);
	}else
		scrposr = Rect(scrollr.min.x, scrollr.min.y, scrollr.max.x-1, scrollr.max.y);
	draw(screen, scrposr, scrlcol.fg, nil, ZP);
	for(i=0; i<nlines && offset+i<lcount; i++){
		lr = Rect(textr.min.x, textr.min.y+i*lineh, textr.max.x, textr.min.y+(i+1)*lineh);
		drawline(lr, lines[offset+i]);
	}
	flushimage(display, 1);
}

void
pan(int off)
{
	int max;

	max = Hpadding + maxlength * stringwidth(font, " ") + 2 * stringwidth(font, ellipsis) - Dx(textr);
	Δpan += off * stringwidth(font, " ");
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
	if(offset+nlines>lcount)
		offset = lcount-nlines+1;
}

void
scroll(int off)
{
	if(off<0 && offset<=0)
		return;
	if(off>0 && offset+nlines>lcount)
		return;
	offset += off;
	clampoffset();
	redraw();
}

int
indexat(Point p)
{
	int n;

	if (!ptinrect(p, textr))
		return -1;
	n = (p.y - textr.min.y) / lineh;
	if ((n+offset) >= lcount)
		return -1;
	return n;
}

void
eresize(void)
{
	if(getwindow(display, Refnone)<0)
		sysfatal("cannot reattach: %r");
	sr = screen->r;
	scrollr = sr;
	scrollr.max.x = scrollr.min.x+Scrollwidth+Scrollgap;
	listr = sr;
	listr.min.x = scrollr.max.x;
	textr = insetrect(listr, Margin);
	lineh = Vpadding+font->height+Vpadding;
	nlines = Dy(textr)/lineh;
	scrollsize = mousescrollsize(nlines);
	if(offset > 0 && offset+nlines>lcount)
		offset = lcount-nlines+1;
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
		scroll(-1000000);
		break;
	case Kend:
		scroll(1000000);
		break;
	case Kpgup:
		scroll(-nlines);
		break;
	case Kpgdown:
		scroll(nlines);
		break;
	case Kup:
		scroll(-1);
		break;
	case Kdown:
		scroll(1);
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
emouse(Mouse m)
{
	int n;

	if(oldbuttons == 0 && m.buttons != 0 && ptinrect(m.xy, scrollr))
		scrolling = 1;
	else if(m.buttons == 0)
		scrolling = 0;

	if(scrolling){
		if(m.buttons&1){
			n = (m.xy.y - scrollr.min.y) / lineh;
			if(-n<lcount-offset){
				scroll(-n);
			} else {
				scroll(-lcount+offset);
			}
			return;
		}else if(m.buttons&2){
			n = (m.xy.y - scrollr.min.y) * lcount / Dy(scrollr);
			offset = n;
			clampoffset();
			redraw();
		}else if(m.buttons&4){
			n = (m.xy.y - scrollr.min.y) / lineh;
			if(n<lcount-offset){
				scroll(n);
			} else {
				scroll(lcount-offset);
			}
			return;
		}
	}else{
		if(m.buttons&4){
			n = indexat(m.xy);
			if(n>=0 && lines[n+offset]->f != nil)
				plumb(lines[n+offset]->f, lines[n+offset]->l);
		}else if(m.buttons&8)
			scroll(-scrollsize);
		else if(m.buttons&16)
			scroll(scrollsize);
	}
	oldbuttons = m.buttons;
}

void
initcol(Col *c, ulong fg, ulong bg)
{
	c->fg = allocimage(display, Rect(0,0,1,1), screen->chan, 1, fg);
	c->bg = allocimage(display, Rect(0,0,1,1), screen->chan, 1, bg);
}

void
initcols(int black)
{
	if(black){
		initcol(&scrlcol,     DBlack, 0x999999FF^(~0xFF));
		initcol(&cols[Lfile], DWhite, 0x333333FF);
		initcol(&cols[Lsep],  DBlack, DPurpleblue);
		initcol(&cols[Ladd],  DWhite, 0x002800FF);
		initcol(&cols[Ldel],  DWhite, 0x3F0000FF);
		initcol(&cols[Lnone], DWhite, DBlack);
	}else{
		initcol(&scrlcol,     DWhite, 0x999999FF);
		initcol(&cols[Lfile], DBlack, 0xEFEFEFFF);
		initcol(&cols[Lsep],  DBlack, 0xEAFFFFFF);
		initcol(&cols[Ladd],  DBlack, 0xE6FFEDFF);
		initcol(&cols[Ldel],  DBlack, 0xFFEEF0FF);
		initcol(&cols[Lnone], DBlack, DWhite);
	}
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

Line*
parseline(char *f, int n, char *s)
{
	Line *l;
	int len;

	l = malloc(sizeof *l);
	if(l==nil)
		sysfatal("malloc: %r");
	l->t = linetype(s);
	l->s = s;
	l->l = n;
	if(l->t != Lfile && l->t != Lsep)
		l->f = f;
	else
		l->f = nil;
	len = strlen(s);
	if(len > maxlength)
		maxlength = len;
	return l;
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
	Line *l;
	char *s, *f, *t;
	int n, ab;

	ab = 0;
	n = 0;
	f = nil;
	lsize = 64;
	lcount = 0;
	lines = malloc(lsize * sizeof *lines);
	if(lines==nil)
		sysfatal("malloc: %r");
	bp = Bfdopen(fd, OREAD);
	if(bp==nil)
		sysfatal("Bfdopen: %r");
	for(;;){
		s = Brdstr(bp, '\n', 1);
		if(s==nil)
			break;
		l = parseline(f, n, s);
		if(l->t == Lfile && l->s[0] == '-' && strncmp(l->s+4, "a/", 2)==0)
			ab = 1;
		if(l->t == Lfile && l->s[0] == '+'){
			f = l->s+4;
			if(ab && strncmp(f, "b/", 2)==0){
				f += 1;
				if(access(f, AEXIST) < 0)
					f += 1;
			}
			t = strchr(f, '\t');
			if(t!=nil)
				*t = 0;
		}else if(l->t == Lsep)
			n = lineno(l->s);
		else if(l->t == Ladd || l->t == Lnone)
			++n;
		lines[lcount++] = l;
		if(lcount>=lsize){
			lsize *= 2;
			lines = realloc(lines, lsize*sizeof *lines);
			if(lines==nil)
				sysfatal("realloc: %r");
		}
	}
}

void
usage(void)
{
	fprint(2, "%s [-b]\n", argv0);
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
	default:
		usage();
		break;
	}ARGEND;

	parse(0);
	if(lcount==0){
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
	eresize();
	for(;;){
		switch(alt(a)){
		case Emouse:
			emouse(m);
			break;
		case Eresize:
			eresize();
			break;
		case Ekeyboard:
			ekeyboard(k);
			break;
		}
	}
}
