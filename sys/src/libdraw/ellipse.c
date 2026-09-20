#include <u.h>
#include <libc.h>
#include <draw.h>

static
void
doellipse(int cmd, Image *dst, Point *c, int xr, int yr, int thick, Image *src, Point *sp, int alpha, int phi, Drawop op)
{
	_lockdisplay(dst->display);
	if(drawcmd(dst->display, "ObllPlllPll", op,
	    cmd, dst->id, src->id, c, xr, yr, thick, sp, alpha, phi) < 0){
		_unlockdisplay(dst->display);
		fprint(2, "image ellipse: %r\n");
	}
	_unlockdisplay(dst->display);
}

void
ellipse(Image *dst, Point c, int a, int b, int thick, Image *src, Point sp)
{
	doellipse('e', dst, &c, a, b, thick, src, &sp, 0, 0, SoverD);
}

void
ellipseop(Image *dst, Point c, int a, int b, int thick, Image *src, Point sp, Drawop op)
{
	doellipse('e', dst, &c, a, b, thick, src, &sp, 0, 0, op);
}

void
fillellipse(Image *dst, Point c, int a, int b, Image *src, Point sp)
{
	doellipse('E', dst, &c, a, b, 0, src, &sp, 0, 0, SoverD);
}

void
fillellipseop(Image *dst, Point c, int a, int b, Image *src, Point sp, Drawop op)
{
	doellipse('E', dst, &c, a, b, 0, src, &sp, 0, 0, op);
}

void
arc(Image *dst, Point c, int a, int b, int thick, Image *src, Point sp, int alpha, int phi)
{
	alpha |= 1<<31;
	doellipse('e', dst, &c, a, b, thick, src, &sp, alpha, phi, SoverD);
}

void
arcop(Image *dst, Point c, int a, int b, int thick, Image *src, Point sp, int alpha, int phi, Drawop op)
{
	alpha |= 1<<31;
	doellipse('e', dst, &c, a, b, thick, src, &sp, alpha, phi, op);
}

void
fillarc(Image *dst, Point c, int a, int b, Image *src, Point sp, int alpha, int phi)
{
	alpha |= 1<<31;
	doellipse('E', dst, &c, a, b, 0, src, &sp, alpha, phi, SoverD);
}

void
fillarcop(Image *dst, Point c, int a, int b, Image *src, Point sp, int alpha, int phi, Drawop op)
{
	alpha |= 1<<31;
	doellipse('E', dst, &c, a, b, 0, src, &sp, alpha, phi, op);
}
