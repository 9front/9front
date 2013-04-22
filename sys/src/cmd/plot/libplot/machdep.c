#include "mplot.h"
Image *offscreen;
static int buffer;

static Point
xlp(Point p)
{
	p.x += screen->r.min.x + 4 - offscreen->r.min.x;
	p.y += screen->r.min.y + 4 - offscreen->r.min.y;
	return p;
}

static Rectangle
xlr(Rectangle r)
{
	int dx, dy;

	dx = screen->r.min.x + 4 - offscreen->r.min.x;
	dy = screen->r.min.y + 4 - offscreen->r.min.y;
	r.min.x += dx;
	r.min.y += dy;
	r.max.x += dx;
	r.max.y += dy;
	return r;
}

/*
 * Clear the window from x0, y0 to x1, y1 (inclusive) to color c
 */
void
m_clrwin(int x0, int y0, int x1, int y1, int c)
{
	draw(offscreen, Rect(x0, y0, x1+1, y1+1), getcolor(c), nil, ZP);
	if(offscreen != screen && !buffer)
		draw(screen, xlr(Rect(x0, y0, x1+1, y1+1)), getcolor(c), nil, ZP);
}
/*
 * Draw text between pointers p and q with first character centered at x, y.
 * Use color c.  Centered if cen is non-zero, right-justified if right is non-zero.
 * Returns the y coordinate for any following line of text.
 */
int
m_text(int x, int y, char *p, char *q, int c, int cen, int right)
{
	Point tsize;

	tsize = stringsize(font, p);
	if(cen)
		x -= tsize.x/2;
	else if(right)
		x -= tsize.x;
	stringn(offscreen, Pt(x, y-tsize.y/2), getcolor(c), ZP, font, p, q-p);
	if(offscreen != screen && !buffer)
		stringn(screen, xlp(Pt(x, y-tsize.y/2)), getcolor(c), ZP, font, p, q-p);
	return y+tsize.y;
}
/*
 * draw point x, y
 */
void
m_dpt(double x, double y)
{
	Image *c;

	c = getcolor(e1->foregr);
	draw(offscreen, Rect(SCX(x), SCY(y), SCX(x)+1, SCY(y)+1), c, nil, ZP);
	if(offscreen != screen && !buffer)
		draw(screen, xlr(Rect(SCX(x), SCY(y), SCX(x)+1, SCY(y)+1)), c, nil, ZP);
}

/*
 * Draw the vector from x0, y0 to x1, y1 in color c.
 * Clipped by caller
 */
void
m_vector(int x0, int y0, int x1, int y1, int c)
{
	line(offscreen, Pt(x0, y0), Pt(x1, y1), Endsquare, Endsquare, 0, getcolor(c), ZP);
	if(offscreen != screen && !buffer)
		line(screen, xlp(Pt(x0, y0)), xlp(Pt(x1, y1)), Endsquare, Endsquare, 0, getcolor(c), ZP);
}
/*
 * Startup initialization
 */
void m_initialize(char*)
{
	static int once;
	int dx, dy;

	if(once)
		return;
	once = 1;

	if(initdraw(nil, nil, "plot") < 0)
		sysfatal("initdraw: %r");
/////	einit(Emouse);
	offscreen = allocimage(display, insetrect(screen->r, 4), screen->chan, 0, -1);
	if(offscreen == nil)
		sysfatal("Can't double buffer\n");
	clipminx = mapminx = screen->r.min.x+4;
	clipminy = mapminy = screen->r.min.y+4;
	clipmaxx = mapmaxx = screen->r.max.x-5;
	clipmaxy = mapmaxy = screen->r.max.y-5;
	dx = clipmaxx-clipminx;
	dy = clipmaxy-clipminy;
	if(dx>dy){
		mapminx += (dx-dy)/2;
		mapmaxx = mapminx+dy;
	}
	else{
		mapminy += (dy-dx)/2;
		mapmaxy = mapminy+dx;
	}
}
/*
 * Clean up when finished
 */
void m_finish(void)
{
	m_swapbuf();
}
void m_swapbuf(void)
{
	draw(screen, insetrect(screen->r, 4), offscreen, nil, offscreen->r.min);
	flushimage(display, 1);
}
void m_dblbuf(void)
{
	buffer = 1;
}

/*
 * Use cache to avoid repeated allocation.
 */
struct{
	int		v;
	Image	*i;
}icache[32];

Image*
getcolor(int v)
{
	Image *i;
	int j;

	for(j=0; j<nelem(icache); j++)
		if(icache[j].v==v && icache[j].i!=nil)
			return icache[j].i;

	i = allocimage(display, Rect(0, 0, 1, 1), RGB24, 1, v);
	if(i == nil)
		sysfatal("plot: can't allocate image for color: %r");
	for(j=0; j<nelem(icache); j++)
		if(icache[j].i == nil){
			icache[j].v = v;
			icache[j].i = i;
			break;
		}
if(j == nelem(icache))sysfatal("icache: too small");
	return i;
}
