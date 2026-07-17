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

static void
affinewarp1(Image *dst, Point *dp, Rectangle *r, Image *src, Point *sp,
	Image *msk, Point *mp, Warp *w, int smooth, Drawop op)
{
	uchar *a;

	if(dst == nil || src == nil)
		return;
	if(msk == nil)
		msk = dst->display->opaque;

	_lockdisplay(dst->display);
	a = _bufimageop(dst->display, 1+4+2*4+4*4+4+2*4+4+2*4+3*3*4+1, op);
	if(a == nil){
		_unlockdisplay(dst->display);
		fprint(2, "affinewarp: %r\n");
		return;
	}
	a[0] = 'w';
	BPLONG(a+1, dst->id);
	BPLONG(a+5, dp->x);
	BPLONG(a+9, dp->y);
	BPLONG(a+13, r->min.x);
	BPLONG(a+17, r->min.y);
	BPLONG(a+21, r->max.x);
	BPLONG(a+25, r->max.y);
	BPLONG(a+29, src->id);
	BPLONG(a+33, sp->x);
	BPLONG(a+37, sp->y);
	BPLONG(a+41, msk->id);
	BPLONG(a+45, mp->x);
	BPLONG(a+49, mp->y);
	putwarp(a+53, w);
	a[53+3*3*4] |= smooth&1;
	_unlockdisplay(dst->display);
}

void
affinewarp(Image *dst, Rectangle r, Image *src, Image *msk, Point p, Warp *w, int smooth)
{
	affinewarp1(dst, &dst->r.min, &r, src, &p, msk, &p, w, smooth, SoverD);
}

void
affinewarpop(Image *dst, Rectangle r, Image *src, Image *msk, Point p, Warp *w, int smooth, Drawop op)
{
	affinewarp1(dst, &dst->r.min, &r, src, &p, msk, &p, w, smooth, op);
}

void
genaffinewarp(Image *dst, Point dp, Rectangle r, Image *src, Point sp,
	Image *msk, Point mp, Warp *w, int smooth)
{
	affinewarp1(dst, &dp, &r, src, &sp, msk, &mp, w, smooth, SoverD);
}

void
genaffinewarpop(Image *dst, Point dp, Rectangle r, Image *src, Point sp,
	Image *msk, Point mp, Warp *w, int smooth, Drawop op)
{
	affinewarp1(dst, &dp, &r, src, &sp, msk, &mp, w, smooth, op);
}
