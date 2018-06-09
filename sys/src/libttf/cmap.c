#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ttf.h>
#include "impl.h"

int
ttffindchar(TTFont *fx, Rune r)
{
	int i, j, k, rv;
	TTChMap *p;
	TTFontU *f;

	f = fx->u;
	i = 0;
	j = f->ncmap - 1;
	if(r < f->cmap[0].start || r > f->cmap[j].end) return 0;
	while(i < j){
		k = (i + j) / 2;
		if(f->cmap[k].end < r)
			i = k+1;
		else if(f->cmap[k].start > r)
			j = k-1;
		else
			i = j = k;
	}
	if(i > j) return 0;
	p = &f->cmap[i];
	if(r < p->start || r > p->end) return 0;
	if((p->flags & TTCINVALID) != 0) return 0;
	if(p->tab != nil)
		return p->tab[r - p->start];
	rv = r + p->delta;
	if((p->flags & TTCDELTA16) != 0)
		rv = (u16int)rv;
	return rv;
}

int
ttfenumchar(TTFont *fx, Rune r, Rune *rp)
{
	int i, j, k, rv;
	TTChMap *p;
	TTFontU *f;

	f = fx->u;
	i = 0;
	j = f->ncmap - 1;
	if(r > f->cmap[j].end) return 0;
	while(i < j){
		k = (i + j) / 2;
		if(f->cmap[k].end < r)
			i = k+1;
		else if(f->cmap[k].start > r)
			j = k-1;
		else
			i = j = k;
	}
	if(j < 0) j = 0;
	for(p = &f->cmap[j]; p < &f->cmap[f->ncmap]; p++){
		if((p->flags & TTCINVALID) != 0)
			continue;
		if(r < p->start)
			r = p->start;
		if(p->tab != nil){
			SET(rv);
			while(r <= p->end && (rv = p->tab[r - p->start], rv == 0))
				r++;
			if(r > p->end)
				continue;
			if(rp != nil)
				*rp = r;
			return rv;
		}
		while(r < p->end){
			rv = r + p->delta;
			if((p->flags & TTCDELTA16) != 0)
				rv = (u16int) rv;
			if(rv != 0){
				if(rp != nil)
					*rp = r;
				return rv;
			}
		}
	}
	return 0;
}

static int
ttfgotosub(TTFontU *f)
{
	int i;
	u16int nsub, id, sid, off;
	int rank, maxrank;
	u32int maxoff;
	#define SUBID(a,b) ((a)<<16|(b))

	if(ttfgototable(f, "cmap") < 0)
		return -1;
	ttfunpack(f, ".. w", &nsub);
	maxrank = 0;
	maxoff = 0;
	for(i = 0; i < nsub; i++){
		ttfunpack(f, "wwl", &id, &sid, &off);
		switch(id << 16 | sid){
		case SUBID(0, 4): /* Unicode 2.0 or later (BMP and non-BMP) */
			rank = 100;
			break;
		case SUBID(0, 0): /* Unicode default */
		case SUBID(0, 1): /* Unicode 1.1 */
		case SUBID(0, 2): /* ISO 10646 */
		case SUBID(0, 3): /* Unicode 2.0 (BMP) */
			rank = 80;
			break;
		case SUBID(3, 10): /* Windows, UCS-4 */
			rank = 60;
			break;
		case SUBID(3, 1): /* Windows, UCS-2 */
			rank = 40;
			break;
		case SUBID(3, 0): /* Windows, Symbol */
			rank = 20;
			break;
		default:
			rank = 0;
			break;
		}
		if(rank > maxrank){
			maxrank = rank;
			maxoff = off;
		}
	}
	if(maxrank == 0){
		werrstr("no suitable character table");
		return -1;
	}
	if(ttfgototable(f, "cmap") < 0)
		return -1;
	Bseek(f->bin, maxoff, 1);
	return 0;

}

static int
cmap0(TTFontU *f)
{
	u16int len;
	int i;
	u8int *p;
	int *q;

	ttfunpack(f, "w2", &len);
	if(len < 262){
		werrstr("character table too short");
		return -1;
	}
	f->cmap = mallocz(sizeof(TTChMap), 1);
	if(f->cmap == nil)
		return -1;
	f->ncmap = 1;
	f->cmap->start = 0;
	f->cmap->end = 0xff;
	f->cmap->tab = mallocz(256 * sizeof(int), 1);
	if(f->cmap->tab == nil)
		return -1;
	Bread(f->bin, f->cmap->tab, 256 * sizeof(int));
	p = (u8int*)f->cmap->tab + 256;
	q = f->cmap->tab + 256;
	for(i = 255; i >= 0; i--)
		*--q = *--p;
	return 0;
}

static int
cmap4(TTFontU *f)
{
	u16int len, ncmap;
	int i, j, n, n0, off;
	u16int v;
	u8int *buf;

	ttfunpack(f, "w2", &len);
	if(len < 16){
		werrstr("character table too short");
		return -1;
	}
	ttfunpack(f, "w6", &ncmap);
	ncmap /= 2;
	if(len < 16 + 8 * ncmap){
		werrstr("character table too short");
		return -1;
	}
	f->cmap = mallocz(sizeof(TTChMap) * ncmap, 1);
	if(f->cmap == nil) return -1;
	f->ncmap = ncmap;
	for(i = 0; i < ncmap; i++)
		f->cmap[i].flags = TTCDELTA16;
	for(i = 0; i < ncmap; i++)
		ttfunpack(f, "W", &f->cmap[i].end);
	ttfunpack(f, "..");
	for(i = 0; i < ncmap; i++)
		ttfunpack(f, "W", &f->cmap[i].start);
	for(i = 0; i < ncmap; i++)
		ttfunpack(f, "W", &f->cmap[i].delta);
	for(i = 0; i < ncmap; i++)
		ttfunpack(f, "W", &f->cmap[i].temp);
	len -= 10 + 8 * ncmap;
	buf = malloc(len);
	if(buf == nil)
		return -1;
	Bread(f->bin, buf, len);
	for(i = 0; i < ncmap; i++){
		if(f->cmap[i].temp == 0) continue;
		n0 = f->cmap[i].end - f->cmap[i].start + 1;
		n = n0;
		off = f->cmap[i].temp - (ncmap - i) * 2;
		if(off + 2 * n > len) n = (len - off) / 2;
		if(off < 0 || n <= 0){
			f->cmap[i].flags |= TTCINVALID;
			continue;
		}
		f->cmap[i].tab = mallocz(n0 * sizeof(int), 1);
		if(f->cmap[i].tab == nil)
			return -1;
		for(j = 0; j < n; j++){
			v = buf[off + 2*j] << 8 | buf[off + 2*j + 1];
			if(v != 0) v += f->cmap[i].delta;
			f->cmap[i].tab[j] = v;
		}
	}
	free(buf);
	return 0;
}

static int
cmap6(TTFontU *f)
{
	u16int len, first, cnt, v;
	int *p;
	u8int *q;

	ttfunpack(f, "w2", &len);
	if(len < 12){
		werrstr("character table too short");
		return -1;
	}
	ttfunpack(f, "ww", &first, &cnt);
	f->cmap = mallocz(sizeof(TTChMap), 1);
	if(f->cmap == nil)
		return -1;
	f->ncmap = 1;
	f->cmap->start = first;
	f->cmap->end = first + len - 1;
	f->cmap->tab = mallocz(cnt * sizeof(int), 1);
	if(f->cmap->tab == nil)
		return -1;
	if(len < 10 + 2 * cnt){
		werrstr("character table too short");
		return -1;
	}
	Bread(f->bin, f->cmap->tab, 2 * cnt);
	p = f->cmap->tab + cnt;
	q = (u8int*) f->cmap->tab + 2 * cnt;
	while(p > f->cmap->tab){
		v = *--q;
		v |= *--q << 8;
		*--p = v;
	}
	return 0;
}

static int
cmap12(TTFontU *f)
{
	u32int len;
	u32int ncmap;
	int i;
	
	ttfunpack(f, "2l4", &len);
	if(len < 16){
		werrstr("character table too short");
		return -1;
	}
	ttfunpack(f, "l", &ncmap);
	if(len < 16 + 12 * ncmap){
		werrstr("character table too short");
		return -1;
	}
	f->cmap = mallocz(sizeof(TTChMap) * ncmap, 1);
	if(f->cmap == nil)
		return -1;
	f->ncmap = ncmap;
	for(i = 0; i < ncmap; i++){
		ttfunpack(f, "lll", &f->cmap[i].start, &f->cmap[i].end, &f->cmap[i].delta);
		f->cmap[i].delta -= f->cmap[i].start;
	}
	return 0;
}

int (*cmaphand[])(TTFontU *) = {
	[0] cmap0,
	[4] cmap4,
	[6] cmap6,
	[12] cmap12,
};

int
ttfparsecmap(TTFontU *f)
{
	u16int format;

	if(ttfgotosub(f) < 0)
		return -1;
	ttfunpack(f, "w", &format);
	if(format >= nelem(cmaphand) || cmaphand[format] == nil){
		werrstr("character table in unknown format %d", format);
		return -1;
	}
	if(cmaphand[format](f) < 0)
		return -1;
	return 0;
}
