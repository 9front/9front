#include <u.h>
#include <libc.h>
#include <draw.h>

static void
putwarp(uchar *a, Warp *w)
{
	BPLONG(a+0*3*4+0*4, w->m[0][0]); BPLONG(a+0*3*4+1*4, w->m[0][1]); BPLONG(a+0*3*4+2*4, w->m[0][2]);
	BPLONG(a+1*3*4+0*4, w->m[1][0]); BPLONG(a+1*3*4+1*4, w->m[1][1]); BPLONG(a+1*3*4+2*4, w->m[1][2]);
	BPLONG(a+2*3*4+0*4, w->m[2][0]); BPLONG(a+2*3*4+1*4, w->m[2][1]); BPLONG(a+2*3*4+2*4, w->m[2][2]);
	a[3*3*4] = w->flags << 1;
}

void
affinewarp(Image *dst, Rectangle r, Image *src, Point p, Warp *w, int smooth)
{
	uchar *a;

	if(dst == nil || src == nil)
		return;

	_lockdisplay(dst->display);
	a = bufimage(dst->display, 1+4+4*4+4+2*4+3*3*4+1);
	if(a == nil){
		_unlockdisplay(dst->display);
		fprint(2, "affinewarp: %r\n");
		return;
	}
	a[0] = 'a';
	BPLONG(a+1, dst->id);
	BPLONG(a+5, r.min.x);
	BPLONG(a+9, r.min.y);
	BPLONG(a+13, r.max.x);
	BPLONG(a+17, r.max.y);
	BPLONG(a+21, src->id);
	BPLONG(a+25, p.x);
	BPLONG(a+29, p.y);
	putwarp(a+33, w);
	a[33+3*3*4] |= smooth&1;
	_unlockdisplay(dst->display);
}
