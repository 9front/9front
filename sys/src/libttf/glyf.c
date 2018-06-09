#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <ctype.h>
#include <ttf.h>
#include "impl.h"

void
ttfputglyph(TTGlyph *g)
{
	if(g == nil) return;
	free(g->pt);
	free(g->ptorg);
	free(g->confst);
	free(g->bit);
	free(g->hint);
	free(g);
}

static void
glyphscale(TTGlyph *g)
{
	TTFont *f;
	int i;
	TTPoint *p;
	
	f = g->font;
	for(i = 0; i < g->npt; i++){
		p = &g->pt[i];
		p->x = ttfrounddiv(p->x * f->ppem * 64, f->u->emsize);
		p->y = ttfrounddiv(p->y * f->ppem * 64, f->u->emsize);
	}
	memmove(g->ptorg, g->pt, sizeof(TTPoint) * g->npt);
	g->pt[g->npt - 1].x = g->pt[g->npt - 1].x + 32 & -64;
}

static TTGlyph *
emptyglyph(TTFont *fs, int glyph, int render)
{
	TTGlyph *g;

	g = mallocz(sizeof(TTGlyph), 1);
	if(g == nil)
		return nil;
	g->font = fs;
	g->info = &fs->u->ginfo[glyph];
	g->confst = malloc(sizeof(int));
	g->npt = 2;
	g->pt = mallocz(sizeof(TTPoint) * 2, 1);
	g->ptorg = mallocz(sizeof(TTPoint) * 2, 1);
	if(g->confst == nil || g->pt == nil || g->ptorg == nil){
		ttfputglyph(g);
		return nil;
	}
	g->pt[1].x = g->info->advanceWidth;
	g->npt = 2;
	if(render)
		glyphscale(g);
	g->xmin = 0;
	g->ymin = 0;
	g->xmax = g->info->advanceWidth;
	g->ymax = 1;
	if(render){
		g->xminpx = 0;
		g->xmaxpx = (g->xmax * fs->ppem + fs->u->emsize - 1) / fs->u->emsize;
		g->yminpx = 0;
		g->ymaxpx = 1;
	}
	return g;
}

static TTGlyph *
simpglyph(TTFont *fs, int glyph, int nc, int render)
{
	u16int np;
	short x;
	u16int len;
	u16int temp16;
	u8int temp8;
	u8int *flags, *fp, *fq;
	TTPoint *p;
	int i, j, r;
	short lastx, lasty;
	TTFontU *f;
	TTGlyph *g;
	
	flags = nil;
	f = fs->u;
	g = mallocz(sizeof(TTGlyph), 1);
	if(g == nil)
		return nil;
	g->font = fs;
	g->info = &f->ginfo[glyph];
	g->confst = malloc(sizeof(u16int) * (nc + 1));
	if(g->confst == nil)
		goto err;
	x = -1;
	for(i = g->ncon; i < nc; i++){
		g->confst[i] = x + 1;
		ttfunpack(f, "w", &x);
	}
	g->confst[i] = x + 1;
	g->ncon = nc;

	np = x + 1;
	ttfunpack(f, "w", &len);
	g->nhint = len;
	g->hint = mallocz(len, 1);
	if(g->hint == nil)
		goto err;
	Bread(f->bin, g->hint, len);
	
	flags = mallocz(np, 1);
	if(flags == nil)
		goto err;
	for(i = 0; i < np; i++){
		j = Bgetc(f->bin);
		flags[i] = j;
		if((j & 8) != 0){
			r = Bgetc(f->bin);
			while(r-- > 0)
				flags[++i] = j;
		}
	}
	
	fp = flags;
	fq = flags;
	lastx = lasty = 0;
	g->pt = malloc(sizeof(TTPoint) * (np + 2));
	if(g->pt == nil)
		goto err;
	g->ptorg = malloc(sizeof(TTPoint) * (np + 2));
	if(g->ptorg == nil)
		goto err;
	for(i = 0; i < np; i++){
		p = &g->pt[g->npt + i];
		p->flags = *fp & 1;
		switch(*fp++ & 0x12){
		case 0x00: ttfunpack(f, "w", &temp16); p->x = lastx += temp16; break;
		case 0x02: ttfunpack(f, "b", &temp8); p->x = lastx -= temp8; break;
		case 0x10: p->x = lastx; break;
		case 0x12: ttfunpack(f, "b", &temp8); p->x = lastx += temp8; break;
		}
	}
	for(i = 0; i < np; i++){
		p = &g->pt[g->npt + i];
		switch(*fq++ & 0x24){
		case 0x00: ttfunpack(f, "w", &temp16); p->y = lasty += temp16; break;
		case 0x04: ttfunpack(f, "b", &temp8); p->y = lasty -= temp8; break;
		case 0x20: p->y = lasty; break;
		case 0x24: ttfunpack(f, "b", &temp8); p->y = lasty += temp8; break;
		}
	}
	g->pt[np] = (TTPoint){0,0,0};
	g->pt[np+1] = (TTPoint){f->ginfo[glyph].advanceWidth,0,0};
	g->npt = np + 2;
	free(flags);
	if(render){
		glyphscale(g);
		ttfhint(g);
	}
	return g;
err:
	free(flags);
	ttfputglyph(g);
	return nil;
}

static TTGlyph *getglyph(TTFont *, int, int);

enum {
	ARG_1_AND_2_ARE_WORDS = 1<<0,
	ARGS_ARE_XY_VALUES = 1<<1,
	ROUND_XY_TO_GRID = 1<<2,
	WE_HAVE_A_SCALE = 1<<3,
	MORE_COMPONENTS = 1<<5,
	WE_HAVE_AN_X_AND_Y_SCALE = 1<<6,
	WE_HAVE_A_TWO_BY_TWO = 1<<7,
	WE_HAVE_INSTRUCTIONS = 1<<8,
	USE_MY_METRICS = 1<<9,
	OVERLAP_COMPOUND = 1<<10,
};

static int
mergeglyph(TTGlyph *g, TTGlyph *h, int flags, int x, int y, int a, int b, int c, int d, int render)
{
	int i, m;
	TTPoint *p;
	TTFont *f;
	int dx, dy;

	f = g->font;
	g->confst = realloc(g->confst, sizeof(int) * (g->ncon + h->ncon + 1));
	for(i = 1; i <= h->ncon; i++)
		g->confst[g->ncon + i] = g->confst[g->ncon] + h->confst[i];
	g->ncon += h->ncon;
	g->pt = realloc(g->pt, sizeof(TTPoint) * (g->npt + h->npt - 2));
	if((flags & USE_MY_METRICS) == 0){
		memmove(g->pt + g->npt + h->npt - 4, g->pt + g->npt - 2, 2 * sizeof(TTPoint));
		m = h->npt - 2;
	}else
		m = h->npt;
	for(i = 0; i < m; i++){
		p = &g->pt[g->npt - 2 + i];
		*p = h->pt[i];
		dx = ttfrounddiv(p->x * a + p->y * b, 16384);
		dy = ttfrounddiv(p->x * c + p->y * d, 16384);
		p->x = dx;
		p->y = dy;
		if((flags & ARGS_ARE_XY_VALUES) != 0){
			if(render){
				dx = ttfrounddiv(x * f->ppem * 64, f->u->emsize);
				dy = ttfrounddiv(y * f->ppem * 64, f->u->emsize);
				if((flags & ROUND_XY_TO_GRID) != 0){
					dx = dx + 32 & -64;
					dy = dy + 32 & -64;
				}
			}
			p->x += dx;
			p->y += dy;
		}else
			abort();
	}
	g->npt += h->npt - 2;
	return 0;
}

static TTGlyph *
compglyph(TTFont *fs, int glyph, int render)
{
	u16int flags, idx;
	int x, y;
	int a, b, c, d;
	TTFontU *f;
	uvlong off;
	TTGlyph *g, *h;
	u16int len;

	f = fs->u;
	g = mallocz(sizeof(TTGlyph), 1);
	if(g == nil)
		return nil;
	g->font = fs;
	g->info = &f->ginfo[glyph];
	g->pt = mallocz(sizeof(TTPoint) * 2, 1);
	if(g->pt == nil){
	err:
		ttfputglyph(g);
		return nil;
	}
	g->pt[1].x = ttfrounddiv(f->ginfo[glyph].advanceWidth * fs->ppem * 64, f->emsize);
	g->npt = 2;
	g->confst = mallocz(sizeof(int), 1);
	if(g->confst == nil)
		goto err;
	do{
		ttfunpack(f, "ww", &flags, &idx);
		switch(flags & (ARG_1_AND_2_ARE_WORDS | ARGS_ARE_XY_VALUES)){
		case 0: ttfunpack(f, "BB", &x, &y); break;
		case ARGS_ARE_XY_VALUES: ttfunpack(f, "BB", &x, &y); x = (char)x; y = (char)y; break;
		case ARG_1_AND_2_ARE_WORDS: ttfunpack(f, "WW", &x, &y); break;
		case ARG_1_AND_2_ARE_WORDS | ARGS_ARE_XY_VALUES: ttfunpack(f, "WW", &x, &y); x = (short)x; y = (short)y; break;
		}
		if((flags & WE_HAVE_A_SCALE) != 0){
			ttfunpack(f, "S", &a);
			d = a;
			b = c = 0;
		}else if((flags & WE_HAVE_AN_X_AND_Y_SCALE) != 0){
			ttfunpack(f, "SS", &a, &d);
			b = c = 0;
		}else if((flags & WE_HAVE_A_TWO_BY_TWO) != 0)
			ttfunpack(f, "SSSS", &a, &b, &c, &d);
		else{
			a = d = 1<<14;
			b = c = 0;
		}
		off = Bseek(f->bin, 0, 1);
		h = getglyph(fs, idx, render);
		if(h == nil){
			ttfputglyph(g);
			return nil;
		}
		if(mergeglyph(g, h, flags, x, y, a, b, c, d, render) < 0){
			ttfputglyph(h);
			ttfputglyph(g);
			return nil;
		}
		ttfputglyph(h);
		Bseek(f->bin, off, 0);
	}while((flags & MORE_COMPONENTS) != 0);
	g->ptorg = malloc(sizeof(TTPoint) * g->npt);
	memmove(g->ptorg, g->pt, sizeof(TTPoint) * g->npt);
//	g->pt[g->npt - 1].x = g->pt[g->npt - 1].x + 32 & -64;
	if(render && (flags & WE_HAVE_INSTRUCTIONS) != 0){
		ttfunpack(f, "w", &len);
		g->nhint = len;
		g->hint = mallocz(len, 1);
		if(g->hint == nil)
			goto err;
		Bread(f->bin, g->hint, len);
		ttfhint(g);
	}
	return g;
}

static TTGlyph *
getglyph(TTFont *fs, int glyph, int render)
{
	int i;
	short xmin, ymin, xmax, ymax, nc;
	TTFontU *f;
	TTGlyph *g;
	
	f = fs->u;
	if((uint)glyph >= f->numGlyphs){
		werrstr("no such glyph %d", glyph);
		return nil;
	}
	if(f->ginfo[glyph].loca == f->ginfo[glyph+1].loca){
		return emptyglyph(fs, glyph, render);
	}
	if(ttfgototable(f, "glyf") < 0)
		return nil;
	Bseek(f->bin, f->ginfo[glyph].loca, 1);
	ttfunpack(f, "wwwww", &nc, &xmin, &ymin, &xmax, &ymax);
	if(nc < 0)
		g = compglyph(fs, glyph, render);
	else
		g = simpglyph(fs, glyph, nc, render);
	if(g == nil)
		return nil;
	g->xmin = g->pt[0].x;
	g->xmax = g->pt[0].x;
	g->ymin = g->pt[0].y;
	g->ymax = g->pt[0].y;
	for(i = 1; i < g->npt - 2; i++){
		if(g->pt[i].x < g->xmin)
			g->xmin = g->pt[i].x;
		if(g->pt[i].x > g->xmax)
			g->xmax = g->pt[i].x;
		if(g->pt[i].y < g->ymin)
			g->ymin = g->pt[i].y;
		if(g->pt[i].y > g->ymax)
			g->ymax = g->pt[i].y;
	}
	if(render){
		g->xminpx = g->xmin >> 6;
		g->xmaxpx = g->xmax + 63 >> 6;
		g->yminpx = g->ymin >> 6;
		g->ymaxpx = g->ymax + 63 >> 6;
	}
	return g;
}

TTGlyph *
ttfgetglyph(TTFont *fs, int glyph, int render)
{
	TTGlyph *g;
	
	g = getglyph(fs, glyph, render);
	if(g == nil)
		return nil;
	g->idx = glyph;
	if(render){
		ttfscan(g);
		g->advanceWidthpx = (g->pt[g->npt - 1].x - g->pt[g->npt - 2].x + 63) / 64;
	}
	setmalloctag(g, getcallerpc(&fs));
	return g;
}
