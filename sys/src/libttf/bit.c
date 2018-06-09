#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ttf.h>
#include "impl.h"

TTBitmap *
ttfnewbitmap(int w, int h)
{
	TTBitmap *b;

	b = mallocz(sizeof(TTBitmap), 1);
	if(b == nil) return nil;
	b->width = w;
	b->height = h;
	b->stride = w + 7 >> 3;
	b->bit = mallocz(b->stride * h, 1);
	if(b->bit == nil){
		free(b);
		return nil;
	}
	return b;
}

void
ttffreebitmap(TTBitmap *b)
{
	if(b == nil) return;
	free(b->bit);
	free(b);
}

void
ttfblit(TTBitmap *dst, int dx, int dy, TTBitmap *src, int sx0, int sy0, int sx1, int sy1)
{
	uchar *sp, *dp;
	u32int b;
	int x, y, ss, ds, dx1, dy1;

	if(sx0 < 0) sx0 = 0;
	if(sy0 < 0) sy0 = 0;
	if(sx1 > src->width) sx1 = src->width;
	if(sy1 > src->height) sy1 = src->height;
	if(dx < 0){
		sx0 -= dx;
		dx = 0;
	}
	if(dy < 0){
		sy0 -= dy;
		dy = 0;
	}
	dx1 = dx + sx1 - sx0;
	dy1 = dy + sy1 - sy0;
	if(dx1 > dst->width){
		sx1 -= dx1 - dst->width;
		dx1 = dst->width;
	}
	if(dy1 > dst->height) sy1 -= dy1 - dst->height;
	if(sx1 <= sx0 || sy1 <= sy0) return;
	ss = src->stride - ((sx1-1 >> 3) - (sx0 >> 3) + 1);
	ds = dst->stride - ((dx1-1 >> 3) - (dx >> 3) + 1);
	sp = src->bit + sy0 * src->stride + (sx0 >> 3);
	dp = dst->bit + dy * dst->stride + (dx >> 3);
	y = sy1 - sy0;
	do{
		if(sx0 >> 3 == sx1 >> 3){
			b = (*sp++ << 8 & 0xff << 8-(sx0 & 7)) & -0x10000 >> (sx1 & 7);
			if((sx0 & 7) == 0) b >>= 8;
			x = (dx & 7) + (sx1 - sx0);
		}else{
			if((sx0 & 7) != 0)
				b = *sp++ << 8 & 0xff << (-sx0 & 7);
			else
				b = 0;
			x = (sx1 >> 3) - (sx0+7 >> 3);
			while(--x >= 0){
				b |= *sp++;
				*dp++ |= b >> (dx & 7) + (-sx0 & 7);
				b <<= 8;
			}
			if((sx1 & 7) != 0)
				b |= *sp++ & -0x100 >> (sx1 & 7);
			x = (dx & 7) + (-sx0 & 7) + (sx1 & 7);
		}
		for(; x > 0; x -= 8){
			*dp++ |= b >> (dx & 7) + (-sx0 & 7);
			b <<= 8;
		}
		sp += ss;
		dp += ds;
	}while(--y > 0);
}
