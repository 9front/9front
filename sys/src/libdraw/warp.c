#include <u.h>
#include <libc.h>
#include <draw.h>

static void
affinewarp1(Image *dst, Point *dp, Rectangle *r, Image *src, Point *sp,
	Image *msk, Point *mp, Warp *w, int smooth, Drawop op)
{
	char flags;

	if(dst == nil || src == nil)
		return;
	if(msk == nil)
		msk = dst->display->opaque;

	flags = w->flags << 1 | smooth&1;
	_lockdisplay(dst->display);
	if(drawcmd(dst->display, "OblPRlPlPMb", op,
	    'w', dst->id, dp, r, src->id, sp, msk->id, mp, w, flags) < 0){
		_unlockdisplay(dst->display);
		fprint(2, "affinewarp: %r\n");
	}
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
