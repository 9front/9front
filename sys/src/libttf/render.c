#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ttf.h>
#include "impl.h"

static int
ttfparsekern(TTFontU *f)
{
	u16int ver, len, cov, ntab;
	int i;

	if(ttfgototable(f, "kern") < 0)
		return -1;
	ttfunpack(f, "ww", &ver, &ntab);
	if(ver != 0)
		return -1;
	if(ntab == 0)
		return -1;
	for(i = 0; i < ntab; i++){
		ttfunpack(f, "www", &ver, &len, &cov);
		if((cov & 1) != 0) break;
		Bseek(f->bin, len - 6, 1);
	}
	ttfunpack(f, "w6", &len);
	f->nkern = len;
	f->kern = mallocz(sizeof(TTKern) * len, 1);
	for(i = 0; i < len; i++)
		ttfunpack(f, "lS", &f->kern[i].idx, &f->kern[i].val);
	return 0;
}

static int
ttfkern(TTFont *f, int l, int r)
{
	u32int idx;
	int a, b, c;
	TTFontU *u;
	
	u = f->u;
	if(u->nkern == 0)
		return 0;
	idx = l << 16 | r;
	a = 0;
	b = u->nkern - 1;
	if(u->kern[a].idx > idx || u->kern[b].idx < idx)
		return 0;
	while(a <= b){
		c = (a + b) / 2;
		if(u->kern[c].idx < idx){
			a = c + 1;
		}else if(u->kern[c].idx > idx){
			b = c - 1;
		}else
			return ttfrounddiv(u->kern[c].val * f->ppem, u->emsize);
	}
	return 0;
}

typedef struct {
	TTBitmap *b;
	TTFont *font;
	TTGlyph **glyph;
	int *gwidth;
	char **cpos;
	char *pp;
	int nglyph, aglyph;
	int linew;
	int adj;
	int nspc;
	int oy, lh;
	int spcidx;
	int flags;
	int spcw;
	int spcminus;
} Render;

static int
addglyph(Render *r, char *p, TTGlyph *g)
{
	void *v;
	int k;

	if(r->nglyph >= r->aglyph){
		r->aglyph += 32;
		v = realloc(r->glyph, sizeof(TTGlyph *) * r->aglyph);
		if(v == nil) return -1;
		r->glyph = v;
		v = realloc(r->gwidth, sizeof(int) * r->aglyph);
		if(v == nil) return -1;
		r->gwidth = v;
		v = realloc(r->cpos, sizeof(char *) * r->aglyph);
		if(v == nil) return -1;
		r->cpos = v;
	}
	r->glyph[r->nglyph] = g;
	r->cpos[r->nglyph] = p;
	r->gwidth[r->nglyph] = g->advanceWidthpx;
	if(r->nglyph > 0){
		k = ttfkern(r->font, r->glyph[r->nglyph-1]->idx, g->idx);
		r->gwidth[r->nglyph-1] += k;
		r->linew += k;
	}
	r->nglyph++;
	r->linew += r->gwidth[r->nglyph-1];
	if(g->idx == r->spcidx)
		r->nspc++;
	return 0;
}

static void
flushglyphs(Render *r, int justify)
{
	int i, n, k, x, y;
	int llen;
	int adj, spcw, nspc, c;
	TTFont *f;

	f = r->font;
	if((r->flags & TTFMODE) == TTFLALIGN && !justify)
		while(r->nglyph > 0 && r->glyph[r->nglyph - 1]->idx == r->spcidx){
			r->linew -= r->gwidth[--r->nglyph];
			r->nspc--;
		}
	llen = r->linew;
	k = n = r->nglyph;
	nspc = r->nspc;
	adj = (nspc * r->spcminus + 63) / 64;
	if(r->linew - adj > r->b->width){
		n = r->nglyph;
		while(n > 0 && r->glyph[n - 1]->idx != r->spcidx)
			llen -= r->gwidth[--n];
		k = n;
		while(n > 0 && r->glyph[n - 1]->idx == r->spcidx){
			llen -= r->gwidth[--n];
			nspc--;
		}
		if(n == 0){
			while(n < r->nglyph && llen + r->gwidth[n] < r->b->width)
				llen += r->gwidth[n++];
			k = n;
		}
	}
	if(justify){
		if(nspc == 0)
			spcw = 0;
		else
			spcw = (r->b->width - llen + nspc * r->spcw) * 64 / nspc;
	}else
		spcw = r->spcw * 64;
	switch(r->flags & TTFMODE | justify * TTFJUSTIFY){
	case TTFRALIGN:
		x = r->b->width - llen;
		break;
	case TTFCENTER:
		x = (r->b->width - llen)/2;
		break;
	default:
		x = 0;
	}
	y = r->oy + f->ascentpx;
	c = 0;
	for(i = 0; i < k; i++){
		if(r->glyph[i]->idx == r->spcidx){
			c += spcw;
			x += c >> 6;
			c &= 63;
			r->nspc--;
		}else{
			ttfblit(r->b, x + r->glyph[i]->xminpx, y - r->glyph[i]->ymaxpx, r->glyph[i], 0, 0, r->glyph[i]->width, r->glyph[i]->height);
			x += r->gwidth[i];
		}
		r->linew -= r->gwidth[i];
		ttfputglyph(r->glyph[i]);
	}
	if(n > 0)
		r->pp = r->cpos[n-1];
	r->oy += r->lh;
	memmove(r->glyph, r->glyph + k, (r->nglyph - k) * sizeof(TTGlyph *));
	memmove(r->cpos, r->cpos + k, (r->nglyph - k) * sizeof(char *));
	memmove(r->gwidth, r->gwidth + k, (r->nglyph - k) * sizeof(int));
	r->nglyph -= k;
}

TTBitmap *
_ttfrender(TTFont *f, int (*getrune)(Rune *, char *), char *p, char *end, int w, int h, int flags, char **rp)
{
	Render r;
	Rune ch;
	int i, adj;
	TTGlyph *g;
	
	if(rp != nil) *rp = p;
	if(f->u->nkern < 0 && ttfparsekern(f->u) < 0)
		f->u->nkern = 0;
	memset(&r, 0, sizeof(Render));
	r.flags = flags;
	r.font = f;
	r.b = ttfnewbitmap(w, h);
	if(r.b == nil) goto error;
	r.oy = 0;
	r.lh = f->ascentpx + f->descentpx;
	r.pp = p;
	
	g = ttfgetglyph(f, ttffindchar(f, ' '), 1);
	r.spcidx = g->idx;
	r.spcw = g->advanceWidthpx;
	if((flags & TTFJUSTIFY) != 0)
		r.spcminus = r.spcw * 21;
	else
		r.spcminus = 0;
		
	while(p < end && r.oy + r.lh < h){
		p += getrune(&ch, p);
		if(ch == '\n'){
			flushglyphs(&r, 0);
			continue;
		}
		g = ttfgetglyph(f, ttffindchar(f, ch), 1);
		if(g == nil){
			g = ttfgetglyph(f, 0, 1);
			if(g == nil)
				continue;
		}
		if(addglyph(&r, p, g) < 0)
			goto error;
		adj = (r.nspc * r.spcminus + 63) / 64;
		if(r.linew - adj > r.b->width){
			flushglyphs(&r, (flags & TTFJUSTIFY) != 0);
		}
	}
	if(r.oy + r.lh < h)
		flushglyphs(&r, 0);
	for(i = 0; i < r.nglyph; i++)
		ttfputglyph(r.glyph[i]);
	free(r.glyph);
	free(r.gwidth);
	free(r.cpos);
	if(rp != nil)
		*rp = r.pp;
	return r.b;
error:
	ttffreebitmap(r.b);
	free(r.glyph);
	free(r.gwidth);
	return nil;
}

TTBitmap *
ttfrender(TTFont *f, char *str, char *end, int w, int h, int flags, char **rstr)
{
	if(str == nil)
		end = nil;
	else if(end == nil)
		end = str + strlen(str);
	return _ttfrender(f, chartorune, str, end, w, h, flags, rstr);
}

static int
incrune(Rune *r, char *s)
{
	*r = *(Rune*)s;
	return sizeof(Rune);
}

TTBitmap *
ttfrunerender(TTFont *f, Rune *str, Rune *end, int w, int h, int flags, Rune **rstr)
{
	if(str == nil)
		end = nil;
	else if(end == nil)
		end = str + runestrlen(str);
	return _ttfrender(f, incrune, (char *) str, (char *) end, w, h, flags, (char **) rstr);
}
