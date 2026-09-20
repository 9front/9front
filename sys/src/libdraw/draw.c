#include <u.h>
#include <libc.h>
#include <draw.h>

static void
draw1(Image *dst, Rectangle *r, Image *src, Point *p0, Image *mask, Point *p1, Drawop op)
{
	if(src == nil)
		src = dst->display->black;
	if(mask == nil)
		mask = dst->display->opaque;

	_lockdisplay(dst->display);
	drawcmd(dst->display, "OblllRPP", op, 'd', dst->id, src->id, mask->id, r, p0, p1);
	_unlockdisplay(dst->display);
}

void
draw(Image *dst, Rectangle r, Image *src, Image *mask, Point p1)
{
	draw1(dst, &r, src, &p1, mask, &p1, SoverD);
}

void
drawop(Image *dst, Rectangle r, Image *src, Image *mask, Point p1, Drawop op)
{
	draw1(dst, &r, src, &p1, mask, &p1, op);
}

void
gendraw(Image *dst, Rectangle r, Image *src, Point p0, Image *mask, Point p1)
{
	draw1(dst, &r, src, &p0, mask, &p1, SoverD);
}

void
gendrawop(Image *dst, Rectangle r, Image *src, Point p0, Image *mask, Point p1, Drawop op)
{
	draw1(dst, &r, src, &p0, mask, &p1, op);
}
