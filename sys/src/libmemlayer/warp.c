#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <memlayer.h>

struct Draw
{
	Memlayer	*dstlayer;
	Memimage	*src;
	Point		sp;
	Warp		warp;
	int		smooth;
};

static
void
ldrawop(Memimage *dst, Rectangle screenr, Rectangle clipr, void *etc, int insave)
{
	struct Draw *d;
	Rectangle r;

	d = etc;
	if(insave && d->dstlayer->save == nil)
		return;

	if(insave){
		r = rectsubpt(screenr, d->dstlayer->delta);
		clipr = rectsubpt(clipr, d->dstlayer->delta);
	}else
		r = screenr;

	/* now in logical coordinates */

	/* clipr may have narrowed what we should draw on, so clip if necessary */
	if(!rectinrect(r, clipr) && rectclip(&r, clipr) == 0)
		return;
	memlaffinewarp(dst, r, d->src, d->sp, d->warp, d->smooth);
}

void
memlaffinewarp(Memimage *dst, Rectangle r, Memimage *src, Point p0, Warp w, int smooth)
{
	struct Draw d;
	Rectangle srcr, tr;
	Memlayer *dl, *sl;

    Top:
	if(dst->layer == nil && src->layer == nil){
		memaffinewarp(dst, r, src, p0, w, smooth);
		return;
	}

	/*
 	 * Convert to screen coordinates.
	 */
	dl = dst->layer;
	if(dl != nil){
		r.min.x += dl->delta.x;
		r.min.y += dl->delta.y;
		r.max.x += dl->delta.x;
		r.max.y += dl->delta.y;
    Clearlayer:
		if(dl->clear){
			if(src == dst){
				p0.x += dl->delta.x;
				p0.y += dl->delta.y;
				src = dl->screen->image;
			}
			dst = dl->screen->image;
			goto Top;
		}
	}

	sl = src->layer;
	if(sl != nil){
		p0.x += sl->delta.x;
		p0.y += sl->delta.y;
		srcr.min.x += sl->delta.x;
		srcr.min.y += sl->delta.y;
		srcr.max.x += sl->delta.x;
		srcr.max.y += sl->delta.y;
	}

	/*
	 * Now everything is in screen coordinates.
	 * mask is an image.  dst and src are images or obscured layers.
	 */

	/*
	 * if dst and src are the same layer, just draw in save area and expose.
	 */
	if(dl != nil && dst == src){
		if(dl->save == nil)
			return;	/* refresh function makes this case unworkable */
		if(rectXrect(r, srcr)){
			tr = r;
			if(srcr.min.x < tr.min.x)
				tr.min.x = srcr.min.x;
			if(srcr.min.y < tr.min.y)
				tr.min.y = srcr.min.y;
			if(srcr.max.x > tr.max.x)
				tr.max.x = srcr.max.x;
			if(srcr.max.y > tr.max.y)
				tr.max.y = srcr.max.y;
			memlhide(dst, tr);
		}else{
			memlhide(dst, r);
			memlhide(dst, srcr);
		}
		memlaffinewarp(dl->save, rectsubpt(r, dl->delta), dl->save,
			subpt(srcr.min, src->layer->delta), w, smooth);
		memlexpose(dst, r);
		return;
	}

	if(sl != nil){
		if(sl->clear){
			src = sl->screen->image;
			if(dl != nil){
				r.min.x -= dl->delta.x;
				r.min.y -= dl->delta.y;
				r.max.x -= dl->delta.x;
				r.max.y -= dl->delta.y;
			}
			goto Top;
		}
		/* relatively rare case; use save area */
		if(sl->save == nil)
			return;	/* refresh function makes this case unworkable */
		memlhide(src, srcr);
		/* convert back to logical coordinates */
		p0.x -= sl->delta.x;
		p0.y -= sl->delta.y;
		srcr.min.x -= sl->delta.x;
		srcr.min.y -= sl->delta.y;
		srcr.max.x -= sl->delta.x;
		srcr.max.y -= sl->delta.y;
		src = src->layer->save;
	}

	/*
	 * src is now an image.  dst may be an image or a clear layer
	 */
	if(dst->layer == nil)
		goto Top;
	if(dst->layer->clear)
		goto Clearlayer;

	/*
	 * dst is an obscured layer
	 */
	d.dstlayer = dl;
	d.src = src;
	d.sp = p0;
	memmove(d.warp, w, sizeof(Warp));
	d.smooth = smooth;
	_memlayerop(ldrawop, dst, r, r, &d);
}
