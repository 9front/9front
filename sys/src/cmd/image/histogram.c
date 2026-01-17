#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <mouse.h>
#include <keyboard.h>
#include <geometry.h>
#include "fns.h"

enum {
	Hmargin	= 10,
	Vmargin = 15,

	DOrange	= 0xFFA500FF,
};

typedef struct Sampler Sampler;
typedef struct Histogram Histogram;
typedef struct Slider Slider;

struct Sampler
{
	Memimage	*i;
	uchar		*a;
	int		bpl;
	int		cmask;
};

struct Histogram
{
	char	*title;
	Image	*img;
	uvlong	vals[256];
	uvlong	avg;
	uvlong	max;
	ulong	col;
};

struct Slider
{
	Point2	p0;
	Point2	p1;
	Point2	kp;	/* knob position */
	int	left;	/* or right */
	int	min;	/* value at p0 */
	int	max;	/* value at p1 */
	int	val;	/* current value */
};

Histogram histos[4] = {
	{ .title = "alpha", .col = DBlack },
	{ .title = "blue",  .col = 0x0000CCFF },
	{ .title = "green", .col = 0x00BB00FF },
	{ .title = "red",   .col = 0xCC0000FF },
};

Screen *histscr;
Image *histwin;
Point histspan;
Channel *histrefc;
Memimage *mimage;
Image *image;
char title[64];
char winname[128];
Matrix warpmat;
Warp warp;
int smoothen;
Rectangle region;
Image *regioncol;

int
sgn(double n)
{
	return n > 0? 1: (n < 0? -1: 0);
}

void
mktranslate(Matrix m, double x, double y)
{
	identity(m);
	m[0][2] = x;
	m[1][2] = y;
}

void
mkscale(Matrix m, double s)
{
	identity(m);
	m[0][0] = m[1][1] = s;
}

void
translate(Matrix m, double x, double y)
{
	Matrix t;

	memmove(t, m, sizeof(Matrix));
	mktranslate(m, x, y);
	mulm(m, t);
}

void
scale(Matrix m, double s)
{
	Matrix t;

	memmove(t, m, sizeof(Matrix));
	mkscale(m, s);
	mulm(m, t);
}

void
initsampler(Sampler *s, Memimage *i)
{
	s->i = i;
	s->a = i->data->bdata + i->zero;
	s->bpl = sizeof(ulong)*i->width;
	s->cmask = (1ULL << i->depth) - 1;
}

ulong
getpixel(Sampler *s, Point pt)
{
	uchar *p, r, g, b, a;
	ulong val, chan, ctype, ov, v;
	int nb, off, bpp, npack;

	val = 0;
	a = 0xFF;
	r = g = b = 0xAA;	/* garbage */
	p = s->a + pt.y*s->bpl + (pt.x*s->i->depth >> 3);

	/* pixelbits() */
	switch(bpp = s->i->depth){
	case 1:
	case 2:
	case 4:
		npack = 8/bpp;
		off = pt.x%npack;
		val = p[0] >> bpp*(npack-1-off);
		val &= s->cmask;
		break;
	case 8:
		val = p[0];
		break;
	case 16:
		val = p[0]|(p[1]<<8);
		break;
	case 24:
		val = p[0]|(p[1]<<8)|(p[2]<<16);
		break;
	case 32:
		val = p[0]|(p[1]<<8)|(p[2]<<16)|(p[3]<<24);
		break;
	}

	while(bpp < 32){
		val |= val<<bpp;
		bpp <<= 1;
	}

	/* imgtorgba() */
	for(chan = s->i->chan; chan; chan >>= 8){
		if((ctype = TYPE(chan)) == CIgnore){
			val >>= s->i->nbits[ctype];
			continue;
		}
		nb = s->i->nbits[ctype];
		ov = v = val & s->i->mask[ctype];
		val >>= nb;

		while(nb < 8){
			v |= v<<nb;
			nb <<= 1;
		}
		v >>= nb-8;

		switch(ctype){
		case CRed:
			r = v;
			break;
		case CGreen:
			g = v;
			break;
		case CBlue:
			b = v;
			break;
		case CAlpha:
			a = v;
			break;
		case CGrey:
			r = g = b = v;
			break;
		case CMap:
			p = s->i->cmap->cmap2rgb+3*ov;
			r = p[0];
			g = p[1];
			b = p[2];
			break;
		}
	}
	return (r<<24)|(g<<16)|(b<<8)|a;
}

ulong
clralpha(ulong c)
{
	int r, g, b, a;

	r = (c >> 3*8) & 0xFF;
	g = (c >> 2*8) & 0xFF;
	b = (c >> 1*8) & 0xFF;
	a = (c >> 0*8) & 0xFF;
	r = (r * 255)/a;
	g = (g * 255)/a;
	b = (b * 255)/a;
	return (r<<24)|(g<<16)|(b<<8)|a;
}

/*
 * to do this correctly you want a putpixel() that writes into b, but
 * we only use this with RGBA32, so it doesn't matter.
 */
int
fillcolor(Image *i, ulong c)
{
	uchar b[4];

	b[0] = (c >> 0*8) & 0xFF;
	b[1] = (c >> 1*8) & 0xFF;
	b[2] = (c >> 2*8) & 0xFF;
	b[3] = (c >> 3*8);
	return loadimage(i, i->r, b, (i->depth+7)/8);
}

void
drawgradient(Image *dst, Rectangle r, ulong min, ulong max)
{
	Image *a, *b;
	Rectangle dr;
	ulong dx, v;

	a = eallocimage(display, Rect(0,0,1,1), RGBA32, 1, DNofill);
	b = eallocimage(display, Rect(0,0,1,1), RGBA32, 1, DNofill);
	dr = r;
	dr.max.x = dr.min.x + 1;

	dx = Dx(r);
	for(; dr.min.x < r.max.x; dr.min.x = dr.max.x++){
		v = (dr.min.x-r.min.x)*0xFF/dx;
		fillcolor(a, setalpha(min, 0xFF - v));
		fillcolor(b, setalpha(max, v));
		draw(dst, dr, a, nil, ZP);
		draw(dst, dr, b, nil, ZP);
	}
	freeimage(a);
	freeimage(b);
}

void
measureimage(Memimage *i, Rectangle sr)
{
	static int inited;
	Histogram *h;
	Image *brush;
	Rectangle hr, dr, gr, r;
	Sampler s;
	Point sp;
	ulong c;
	int ht, j;

	if(inited)
		for(h = histos; h < histos+nelem(histos); h++){
			memset(h->vals, 0, sizeof(h->vals));
			h->max = h->avg = 0;
		}

	initsampler(&s, i);
	for(sp.y = sr.min.y; sp.y < sr.max.y; sp.y++)
	for(sp.x = sr.min.x; sp.x < sr.max.x; sp.x++){
		c = getpixel(&s, sp);
		if(i->flags & Falpha)
			c = clralpha(c);
		for(h = histos; h < histos+nelem(histos); h++){
			if(++h->vals[c&0xFF] > h->max)
				h->max = h->vals[c&0xFF];
			h->avg += c&0xFF;
			c >>= 8;
		}
	}

	for(h = histos; h < histos+nelem(histos); h++){
		if(h->max == 0)
			h->max = 1;
		h->avg /= Dx(sr)*Dy(sr);
	}

	brush = eallocimage(display, Rect(0,0,1,1), RGBA32, 1, DNofill);
	hr = Rect(0, 0, Hmargin+256+Hmargin, 4+font->height+Vmargin+150+3+8+1+Vmargin);
	dr = Rect(Hmargin, 4+font->height+Vmargin, hr.max.x-Hmargin, hr.max.y-Vmargin-1-8-3);
	gr = Rect(dr.min.x, dr.max.y+3, dr.max.x, dr.max.y+3+8);
	ht = Dy(dr);

	for(h = histos; h < histos+nelem(histos); h++){
		if(inited)
			draw(h->img, dr, display->white, nil, ZP);
		else{
			h->img = eallocimage(display, hr, screen->chan, 0, DWhite);
			string(h->img, addpt(hr.min, Pt(4, 4)), display->black, ZP, font, h->title);
			border(h->img, dr, -1, display->black, ZP);
		}

		/* draw the columns */
		if(fillcolor(brush, h->col) < 0)
			sysfatal("fillcolor: %r");
		r = dr;
		r.max.x = r.min.x + 1;
		for(j = 0; j < 256; j++){
			r.min.y = r.max.y;
			r.min.y -= h->vals[j]*ht / h->max;
			draw(h->img, r, brush, nil, ZP);
			r.min.x = r.max.x++;
		}

		/* draw the avg line */
		r = dr;
		r.min.x += h->avg;
		r.max.x = r.min.x + 1;
		if(fillcolor(brush, setalpha(DOrange, 0xBF)) < 0)
			sysfatal("fillcolor: %r");
		draw(h->img, r, brush, nil, ZP);

		if(!inited){
			border(h->img, gr, -1, display->black, ZP);
			drawgradient(h->img, gr, DBlack, DWhite);
		}
	}
	freeimage(brush);
	if(!inited)
		inited++;
}

int
winctl(Display *d, char *fmt, ...)
{
	char buf[128];
	va_list a;
	int fd, n;

	n = 0;
	snprint(buf, sizeof buf, "%s/wctl", d->windir);
	fd = open(buf, OWRITE|OCEXEC);
	if(fd >= 0){
		va_start(a, fmt);
		n = vfprint(fd, fmt, a);
		va_end(a);
		close(fd);
	}
	return n;
}

int
winmove(Display *d, Point p)
{
	return winctl(d, "move -minx %d -miny %d", p.x, p.y);
}

int
winresize(Display *d, Point sz)
{
	return winctl(d, "resize -dx %d -dy %d", sz.x+2*Borderwidth, sz.y+2*Borderwidth);
}

int
winsetlabel(Display *d, char *fmt, ...)
{
	char buf[128];
	va_list a;
	int fd, n;

	n = 0;
	snprint(buf, sizeof buf, "%s/label", d->windir);
	fd = open(buf, OWRITE|OCEXEC);
	if(fd >= 0){
		va_start(a, fmt);
		n = vfprint(fd, fmt, a);
		va_end(a);
		close(fd);
	}
	return n;
}

void
histredraw(void)
{
	Histogram *h;
	Point off;

	off = histwin->r.min;
	off.y += histspan.y;
	for(h = histos; h < histos+nelem(histos); h++){
		off.y -= Dy(h->img->r);
		draw(histwin, rectaddpt(h->img->r, off), h->img, nil, ZP);
	}
	flushimage(display, 1);
}

void
histresize(void)
{
	lockdisplay(display);
	if(gengetwindow(display, winname, &histwin, &histscr, Refnone) < 0)
		sysfatal("gengetwindow2: %r");
	unlockdisplay(display);
	if(Dx(histwin->r) != histspan.x
	|| Dy(histwin->r) != histspan.y)
		winresize(display, histspan);
	histredraw();
}

void
histmouse(Mousectl *mc)
{
	static Mouse om;

	if((om.buttons & 4) && (mc->buttons & 4)){
		line(histwin, om.xy, mc->xy, Enddisc, Enddisc, 1, display->black, ZP);
		flushimage(display, 1);
	}
	om = mc->Mouse;
}

void
histkey(Rune r)
{
	switch(r){
	case 'q':
	case Kdel:
		threadexitsall(nil);
	}
}

void
histproc(void *)
{
	Mousectl *mc;
	Keyboardctl *kc;
	Rune r;

	threadsetname("histograms");

	lockdisplay(display);	/* avoid races while attaching to new window */
	newwindow(nil);
	winsetlabel(display, "histograms");
	histspan = Pt(Dx(histos[0].img->r), Dy(histos[0].img->r)*nelem(histos));
	winresize(display, histspan);
	winmove(display, ZP);

	if(gengetwindow(display, winname, &histwin, &histscr, Refnone) < 0)
		sysfatal("gengetwindow: %r");
	unlockdisplay(display);
	if((mc = initmouse(nil, histwin)) == nil)
		sysfatal("initmouse2: %r");
	if((kc = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard2: %r");

	histredraw();

	enum { MOUSE, RESIZE, KEY, REFRESH };
	Alt a[] = {
		{mc->c, &mc->Mouse, CHANRCV},
		{mc->resizec, nil, CHANRCV},
		{kc->c, &r, CHANRCV},
		{histrefc, nil, CHANRCV},
		{nil, nil, CHANEND}
	};
	for(;;)
		switch(alt(a)){
		default: sysfatal("alt interrupted");
		case MOUSE:
			histmouse(mc);
			break;
		case RESIZE:
			histresize();
			break;
		case KEY:
			histkey(r);
			break;
		case REFRESH:
			histredraw();
			break;
		}
}

Rectangle
xformrect(Rectangle r, Matrix m)
{
	Point2 p0, p1;

	p0 = (Point2){r.min.x, r.min.y, 1};
	p1 = (Point2){r.max.x, r.max.y, 1};
	p0 = xform(p0, m);
	p1 = xform(p1, m);
	return Rect(p0.x+0.5, p0.y+0.5, p1.x+0.5, p1.y+0.5);
}

int
getregion(Mousectl *mc)
{
	Matrix invwarpmat;

	region = getrect(3, mc);
	if(Dx(region)*Dy(region) <= 4){
badregion:
		region = ZR;
		return 0;
	}
	memmove(invwarpmat, warpmat, sizeof(Matrix));
	invm(invwarpmat);
	region = xformrect(region, invwarpmat);
	if(!rectclip(&region, rectsubpt(image->r, image->r.min)))
		goto badregion;
	return 1;
}

void
redraw(void)
{
	static Point titlep = {10, 10};

	draw(screen, screen->r, display->black, nil, ZP);
	affinewarp(screen, screen->r, image, image->r.min, warp, smoothen);
	if(!eqrect(region, ZR))
		border(screen, xformrect(region, warpmat), -1, regioncol, ZP);
	stringbg(screen, addpt(screen->r.min, titlep), display->white, ZP, font, title, display->black, ZP);
	flushimage(display, 1);
}

void
resize(void)
{
	Point dp;

	dp = screen->r.min;
	lockdisplay(display);
	if(getwindow(display, Refnone) < 0)
		fprint(2, "can't reattach to window\n");
	unlockdisplay(display);
	dp = subpt(screen->r.min, dp);
	translate(warpmat, dp.x, dp.y);
	mkwarp(warp, warpmat);
	redraw();
}

static char *
genrmbmenu(int idx)
{
	switch(idx){
	case 0: return smoothen? "sharpen": "smoothen";
	case 1: return "select region";
	default: return nil;
	}
}

void
rmb(Mousectl *mc)
{
	static Menu menu = { .gen = genrmbmenu };
	static int therewas;	/* a region selected */

	switch(menuhit(3, mc, &menu, _screen)){
	case 0:
		smoothen ^= 1;
		break;
	case 1:
		if(getregion(mc)){
			measureimage(mimage, rectaddpt(region, mimage->r.min));
			therewas = 1;
			nbsend(histrefc, nil);
		}else if(therewas){
			measureimage(mimage, mimage->r);
			therewas = 0;
			nbsend(histrefc, nil);
		}
		break;
	}
	redraw();
}

void
mouse(Mousectl *mc)
{
	enum {
		ScrollzoomΔ	= 0.05,
		Scrollzoomin	= 1.00+ScrollzoomΔ,
		Scrollzoomout	= 1.00-ScrollzoomΔ,
	};
	static Mouse om;
	static Point p;
	int tainted;

	tainted = 0;
	if((om.buttons & 1) && (mc->buttons & 1)){
		translate(warpmat, mc->xy.x - om.xy.x, mc->xy.y - om.xy.y);
		tainted++;
	}else if(mc->buttons & 2){
		if((om.buttons & 2) == 0)
			p = mc->xy;
		switch(sgn(mc->xy.y - om.xy.y)){
		case  1: goto zoomout;
		case -1: goto zoomin;
		}
	}else if((om.buttons & 4) == 0 && (mc->buttons & 4))
		rmb(mc);
	if(mc->buttons & 8){
		p = mc->xy;
zoomin:
		translate(warpmat, -p.x, -p.y);
		scale(warpmat, Scrollzoomin);
		translate(warpmat, p.x, p.y);
		tainted++;
	}else if(mc->buttons & 16){
		p = mc->xy;
zoomout:
		translate(warpmat, -p.x, -p.y);
		scale(warpmat, Scrollzoomout);
		translate(warpmat, p.x, p.y);
		tainted++;
	}
	if(tainted){
		mkwarp(warp, warpmat);
		redraw();
	}
	om = mc->Mouse;
}

void
key(Rune r)
{
	switch(r){
	case Kdel:
	case 'q':
		threadexitsall(nil);
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [file]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	Mousectl *mc;
	Keyboardctl *kc;
	Rune r;
	char cs[10];
	int fd;

	fd = 0;
	ARGBEGIN{
	default: usage();
	}ARGEND;
	if(argc == 1){
		fd = open(argv[0], OREAD);
		if(fd < 0)
			sysfatal("open: %r");
	}else if(argc > 1)
		usage();

	if(initdraw(nil, nil, "histogram") < 0)
		sysfatal("initdraw: %r");
	if((mc = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((kc = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");

	mimage = ereadmemimage(fd);
	image = memimage2image(display, mimage);
	snprint(title, sizeof title, "%s %dx%d %s",
		chantostr(cs, image->chan)? cs: "unknown", Dx(image->r), Dy(image->r),
		argc > 0? argv[0]: "main");
	regioncol = eallocimage(display, Rect(0,0,1,1), XRGB32, 1, DOrange);
	identity(warpmat);
	translate(warpmat, screen->r.min.x, screen->r.min.y);
	mkwarp(warp, warpmat);
	redraw();

	measureimage(mimage, mimage->r);
	snprint(winname, sizeof winname, "%s/winname", display->windir);
	histrefc = chancreate(sizeof(void*), 1);
	unlockdisplay(display);
	proccreate(histproc, nil, mainstacksize);

	enum { MOUSE, RESIZE, KEY };
	Alt a[] = {
		{mc->c, &mc->Mouse, CHANRCV},
		{mc->resizec, nil, CHANRCV},
		{kc->c, &r, CHANRCV},
		{nil, nil, CHANEND}
	};
	for(;;)
		switch(alt(a)){
		default: sysfatal("alt interrupted");
		case MOUSE:
			mouse(mc);
			break;
		case RESIZE:
			resize();
			break;
		case KEY:
			key(r);
			break;
		}
}
