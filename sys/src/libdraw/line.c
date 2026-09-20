#include <u.h>
#include <libc.h>
#include <draw.h>

void
line(Image *dst, Point p0, Point p1, int end0, int end1, int radius, Image *src, Point sp)
{
	lineop(dst, p0, p1, end0, end1, radius, src, sp, SoverD);
}

void
lineop(Image *dst, Point p0, Point p1, int end0, int end1, int radius, Image *src, Point sp, Drawop op)
{
	_lockdisplay(dst->display);
	if(drawcmd(dst->display, "OblPPllllP", op,
	    'L', dst->id, &p0, &p1, end0, end1, radius, src->id, &sp) < 0){
		_unlockdisplay(dst->display);
		fprint(2, "image line: %r\n");
	}
	_unlockdisplay(dst->display);
}
