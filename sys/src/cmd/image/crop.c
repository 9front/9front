#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <geometry.h>
#include "fns.h"

enum {
	Senserad	= 2,	/* in px */
	SenseT		= 0,
	SenseB,
	SenseL,
	SenseR,

	DOrange	= 0xFFA500FF,
};

/* cursors imported from /sys/src/cmd/rio/data.c */
Cursor tl = {
	{-4, -4},
	{0xfe, 0x00, 0x82, 0x00, 0x8c, 0x00, 0x87, 0xff, 
	 0xa0, 0x01, 0xb0, 0x01, 0xd0, 0x01, 0x11, 0xff, 
	 0x11, 0x00, 0x11, 0x00, 0x11, 0x00, 0x11, 0x00, 
	 0x11, 0x00, 0x11, 0x00, 0x11, 0x00, 0x1f, 0x00, },
	{0x00, 0x00, 0x7c, 0x00, 0x70, 0x00, 0x78, 0x00, 
	 0x5f, 0xfe, 0x4f, 0xfe, 0x0f, 0xfe, 0x0e, 0x00, 
	 0x0e, 0x00, 0x0e, 0x00, 0x0e, 0x00, 0x0e, 0x00, 
	 0x0e, 0x00, 0x0e, 0x00, 0x0e, 0x00, 0x00, 0x00, }
};

Cursor t = {
	{-7, -8},
	{0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x06, 0xc0, 
	 0x1c, 0x70, 0x10, 0x10, 0x0c, 0x60, 0xfc, 0x7f, 
	 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0xff, 0xff, 
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, },
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 
	 0x03, 0x80, 0x0f, 0xe0, 0x03, 0x80, 0x03, 0x80, 
	 0x7f, 0xfe, 0x7f, 0xfe, 0x7f, 0xfe, 0x00, 0x00, 
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, }
};

Cursor tr = {
	{-11, -4},
	{0x00, 0x7f, 0x00, 0x41, 0x00, 0x31, 0xff, 0xe1, 
	 0x80, 0x05, 0x80, 0x0d, 0x80, 0x0b, 0xff, 0x88, 
	 0x00, 0x88, 0x0, 0x88, 0x00, 0x88, 0x00, 0x88, 
	 0x00, 0x88, 0x00, 0x88, 0x00, 0x88, 0x00, 0xf8, },
	{0x00, 0x00, 0x00, 0x3e, 0x00, 0x0e, 0x00, 0x1e, 
	 0x7f, 0xfa, 0x7f, 0xf2, 0x7f, 0xf0, 0x00, 0x70, 
	 0x00, 0x70, 0x00, 0x70, 0x00, 0x70, 0x00, 0x70, 
	 0x00, 0x70, 0x00, 0x70, 0x00, 0x70, 0x00, 0x00, }
};

Cursor l = {
	{-7, -7},
	{0x03, 0xe0, 0x02, 0x20, 0x02, 0x20, 0x1a, 0x20, 
	 0x16, 0x20, 0x36, 0x20, 0x60, 0x20, 0x40, 0x20, 
	 0x60, 0x20, 0x36, 0x20, 0x16, 0x20, 0x1a, 0x20, 
	 0x02, 0x20, 0x02, 0x20, 0x02, 0x20, 0x03, 0xe0, },
	{0x00, 0x00, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 
	 0x09, 0xc0, 0x09, 0xc0, 0x1f, 0xc0, 0x3f, 0xc0, 
	 0x1f, 0xc0, 0x09, 0xc0, 0x09, 0xc0, 0x01, 0xc0, 
	 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x00, 0x00, }
};

Cursor r = {
	{-8, -7},
	{0x07, 0xc0, 0x04, 0x40, 0x04, 0x40, 0x04, 0x58, 
	 0x04, 0x68, 0x04, 0x6c, 0x04, 0x06, 0x04, 0x02, 
	 0x04, 0x06, 0x04, 0x6c, 0x04, 0x68, 0x04, 0x58, 
	 0x04, 0x40, 0x04, 0x40, 0x04, 0x40, 0x07, 0xc0, },
	{0x00, 0x00, 0x03, 0x80, 0x03, 0x80, 0x03, 0x80, 
	 0x03, 0x90, 0x03, 0x90, 0x03, 0xf8, 0x03, 0xfc, 
	 0x03, 0xf8, 0x03, 0x90, 0x03, 0x90, 0x03, 0x80, 
	 0x03, 0x80, 0x03, 0x80, 0x03, 0x80, 0x00, 0x00, }
};

Cursor bl = {
	{-4, -11},
	{0x1f, 0x00, 0x11, 0x00, 0x11, 0x00, 0x11, 0x00, 
	 0x11, 0x00, 0x11, 0x00, 0x11, 0x00, 0x11, 0x00, 
	 0x11, 0xff, 0xd0, 0x01, 0xb0, 0x01, 0xa0, 0x01, 
	 0x87, 0xff, 0x8c, 0x00, 0x82, 0x00, 0xfe, 0x00, },
	{0x00, 0x00, 0x0e, 0x00, 0x0e, 0x00, 0x0e, 0x00, 
	 0x0e, 0x00, 0x0e, 0x00, 0x0e, 0x00, 0x0e, 0x00, 
	 0x0e, 0x00, 0x0f, 0xfe, 0x4f, 0xfe, 0x5f, 0xfe, 
	 0x78, 0x00, 0x70, 0x00, 0x7c, 0x00, 0x00, 0x0, }
};

Cursor b = {
	{-7, -7},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	 0xff, 0xff, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 
	 0xfc, 0x7f, 0x0c, 0x60, 0x10, 0x10, 0x1c, 0x70, 
	 0x06, 0xc0, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, },
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	 0x00, 0x00, 0x7f, 0xfe, 0x7f, 0xfe, 0x7f, 0xfe, 
	 0x03, 0x80, 0x03, 0x80, 0x0f, 0xe0, 0x03, 0x80, 
	 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, }
};

Cursor br = {
	{-11, -11},
	{0x00, 0xf8, 0x00, 0x88, 0x00, 0x88, 0x00, 0x88, 
	 0x00, 0x88, 0x00, 0x88, 0x00, 0x88, 0x00, 0x88, 
	 0xff, 0x88, 0x80, 0x0b, 0x80, 0x0d, 0x80, 0x05, 
	 0xff, 0xe1, 0x00, 0x31, 0x00, 0x41, 0x00, 0x7f, },
	{0x00, 0x00, 0x00, 0x70, 0x00, 0x70, 0x00, 0x70, 
	 0x0, 0x70, 0x00, 0x70, 0x00, 0x70, 0x00, 0x70, 
	 0x00, 0x70, 0x7f, 0xf0, 0x7f, 0xf2, 0x7f, 0xfa, 
	 0x00, 0x1e, 0x00, 0x0e, 0x00, 0x3e, 0x00, 0x00, }
};

Memimage *mimage;
Image *image;
char title[64];
Matrix warpmat;
Warp warp;
Rectangle region;
Image *regioncol;
//Image *regionmsk;
Cursor *regioncursor[] = {
 [1<<SenseT|1<<SenseL]	&tl,
 [1<<SenseT]		&t,
 [1<<SenseT|1<<SenseR]	&tr,
 [1<<SenseL]		&l,
 [1<<SenseR]		&r,
 [1<<SenseB|1<<SenseL]	&bl,
 [1<<SenseB]		&b,
 [1<<SenseB|1<<SenseR]	&br,
};

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

static int
round(double n)
{
	return n < 0? n-0.5: n+0.5;
}

Rectangle
xformrect(Rectangle r, Matrix m)
{
	Point2 p0, p1;

	p0 = (Point2){r.min.x, r.min.y, 1};
	p1 = (Point2){r.max.x, r.max.y, 1};
	p0 = xform(p0, m);
	p1 = xform(p1, m);
	return Rect(round(p0.x), round(p0.y), round(p1.x), round(p1.y));
}

int
getregion(Mousectl *mc)
{
	Matrix invwarpmat;

	region = getrect(3, mc);
	memmove(invwarpmat, warpmat, sizeof(Matrix));
	invm(invwarpmat);
	region = rectsubpt(region, screen->r.min);
	region = xformrect(region, invwarpmat);
	if(Dx(region)*Dy(region) < 1
	|| !rectclip(&region, rectsubpt(image->r, image->r.min))){
		region = ZR;
		return 0;
	}
	return 1;
}

int
writeimg(void)
{
	Memimage *tmp;
	Rectangle dr;
	int rc;

	if(eqrect(region, ZR))
		return writememimage(1, mimage);
	dr = rectaddpt(region, mimage->r.min);
	tmp = eallocmemimage(dr, mimage->chan);
	memimagedraw(tmp, tmp->r, mimage, dr.min, nil, ZP, S);
	rc = writememimage(1, tmp);
	freememimage(tmp);
	return rc;
}

void
redraw(void)
{
	static Point offset = {10, 10};
	char buf[128];

	affinewarpop(screen, screen->r, image, nil, image->r.min, &warp, 0, S);
	if(!eqrect(region, ZR)){
		Point regpt;

		border(screen, rectaddpt(xformrect(region, warpmat), screen->r.min), -1, regioncol, ZP);
		snprint(buf, sizeof buf, "%R", rectaddpt(region, image->r.min));
		regpt = addpt(stringsize(font, buf), offset);
		regpt = subpt(screen->r.max, regpt);
		stringbg(screen, regpt, display->white, ZP, font, buf, display->black, ZP);
	}
	stringbg(screen, addpt(screen->r.min, offset), display->white, ZP, font, title, display->black, ZP);
	flushimage(display, 1);
}

void
resize(void)
{
	if(getwindow(display, Refnone) < 0)
		fprint(2, "can't reattach to window\n");
	redraw();
}

int
senseregion(Mousectl *mc)
{
	static Rectangle sensq = {
		.min = {-Senserad,   -Senserad},
		.max = { Senserad+1,  Senserad+1}
	};
	Rectangle sensors[4], regr;
	int c;

	if(eqrect(region, ZR))
		return 0;

	regr = rectaddpt(xformrect(region, warpmat), screen->r.min);
	sensors[SenseT] = rectaddpt(sensq, regr.min);
	sensors[SenseL] = sensors[SenseT];
	sensors[SenseT].max.x += Dx(regr);
	sensors[SenseL].max.y += Dy(regr);
	sensors[SenseB] = rectaddpt(sensors[SenseT], Pt(0, Dy(regr)));
	sensors[SenseR] = rectaddpt(sensors[SenseL], Pt(Dx(regr), 0));

	c = 0;
	if(ptinrect(mc->xy, sensors[SenseT]))
		c |= 1<<SenseT;
	else if(ptinrect(mc->xy, sensors[SenseB]))
		c |= 1<<SenseB;
	if(ptinrect(mc->xy, sensors[SenseL]))
		c |= 1<<SenseL;
	else if(ptinrect(mc->xy, sensors[SenseR]))
		c |= 1<<SenseR;

	setcursor(mc, regioncursor[c]);
	return c;
}

void
rmb(Mousectl *mc)
{
	static char *menustr[] = {
		"crop",
		"write",
		nil
	};
	static Menu menu = { .item = menustr };

	switch(menuhit(3, mc, &menu, _screen)){
	case 0:
		getregion(mc);
		break;
	case 1:
		if(writeimg() < 0)
			fprint(2, "writeimg: %r\n");
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
	Matrix invwarpmat;
	Rectangle regr;
	int tainted;
	int rsc;		/* region sense code, nothing to do with russ... */

	rsc = senseregion(mc);

	tainted = 0;
	if(mc->buttons & 1){
		if((om.buttons & 1) == 0 && rsc != 0){
			memmove(invwarpmat, warpmat, sizeof(Matrix));
			invm(invwarpmat);
			do{
				regr = xformrect(region, warpmat);
				if(rsc & 1<<SenseT)
					regr.min.y = mc->xy.y - screen->r.min.y;
				else if(rsc & 1<<SenseB)
					regr.max.y = mc->xy.y - screen->r.min.y;
				if(rsc & 1<<SenseL)
					regr.min.x = mc->xy.x - screen->r.min.x;
				else if(rsc & 1<<SenseR)
					regr.max.x = mc->xy.x - screen->r.min.x;
				om = mc->Mouse;
				readmouse(mc);

				regr = canonrect(regr);
				regr = xformrect(regr, invwarpmat);
				if(Dx(regr)*Dy(regr) >= 1
				&& rectclip(&regr, rectsubpt(image->r, image->r.min))){
					region = regr;
					redraw();
				}
			}while(mc->buttons & 1);
		}else if(om.buttons & 1){
			translate(warpmat, mc->xy.x - om.xy.x, mc->xy.y - om.xy.y);
			tainted++;
		}
	}else if(mc->buttons & 2){
		if((om.buttons & 2) == 0)
			p = subpt(mc->xy, screen->r.min);
		switch(sgn(mc->xy.y - om.xy.y)){
		case  1: goto zoomout;
		case -1: goto zoomin;
		}
	}else if((om.buttons & 4) == 0 && (mc->buttons & 4))
		rmb(mc);
	if(mc->buttons & 8){
		p = subpt(mc->xy, screen->r.min);
zoomin:
		translate(warpmat, -p.x, -p.y);
		scale(warpmat, Scrollzoomin);
		translate(warpmat, p.x, p.y);
		tainted++;
	}else if(mc->buttons & 16){
		p = subpt(mc->xy, screen->r.min);
zoomout:
		translate(warpmat, -p.x, -p.y);
		scale(warpmat, Scrollzoomout);
		translate(warpmat, p.x, p.y);
		tainted++;
	}
	if(tainted){
		warp = mkwarp(warpmat);
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
	fprint(2, "usage: %s [-w] [file]\n", argv0);
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
	case 'w':
		newwindow(nil);		/* to use within paint(1) for example */
		break;
	default: usage();
	}ARGEND;
	if(argc == 1){
		fd = open(argv[0], OREAD);
		if(fd < 0)
			sysfatal("open: %r");
	}else if(argc > 1)
		usage();

	if(initdraw(nil, nil, "crop") < 0)
		sysfatal("initdraw: %r");
	if(memimageinit() != 0)
		sysfatal("memimageinit: %r");
	if((mc = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((kc = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");

	mimage = ereadmemimage(fd);
	image = memimage2image(display, mimage);
	snprint(title, sizeof title, "%s %R %dx%d %s",
		chantostr(cs, image->chan)? cs: "unknown", image->r,
		Dx(image->r), Dy(image->r), argc > 0? argv[0]: "main");
	regioncol = eallocimage(display, Rect(0,0,1,1), XRGB32, 1, DOrange);
	identity(warpmat);
	warp = mkwarp(warpmat);
	redraw();

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
