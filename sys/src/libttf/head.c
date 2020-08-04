#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <ctype.h>
#include <ttf.h>
#include "impl.h"

void
ttfunpack(TTFontU *f, char *p, ...)
{
	va_list va;
	int n;
	uchar *p1;
	u16int *p2;
	u32int *p4;
	
	va_start(va, p);
	for(; *p != 0; p++)
		switch(*p){
		case 'b':
			p1 = va_arg(va, u8int *);
			*p1 = Bgetc(f->bin);
			break;
		case 'B':
			p4 = va_arg(va, u32int *);
			*p4 = Bgetc(f->bin);
			break;
		case 'w':
			p2 = va_arg(va, u16int *);
			*p2 = Bgetc(f->bin) << 8;
			*p2 |= Bgetc(f->bin);
			break;
		case 'W':
			p4 = va_arg(va, u32int *);
			*p4 = Bgetc(f->bin) << 8;
			*p4 |= Bgetc(f->bin);
			break;
		case 'S':
			p4 = va_arg(va, u32int *);
			*p4 = (char)Bgetc(f->bin) << 8;
			*p4 |= Bgetc(f->bin);
			break;
		case 'l':
			p4 = va_arg(va, u32int *);
			*p4 = Bgetc(f->bin) << 24;
			*p4 |= Bgetc(f->bin) << 16;
			*p4 |= Bgetc(f->bin) << 8;
			*p4 |= Bgetc(f->bin);
			break;
		case '.': Bgetc(f->bin); break;
		case ' ': break;
		default:
			if(isdigit(*p)){
				n = strtol(p, &p, 10);
				p--;
				Bseek(f->bin, n, 1);
			}else abort();
			break;
		}
}

static int
directory(TTFontU *f)
{
	u32int scaler;
	int i;

	ttfunpack(f, "lw .. .. ..", &scaler, &f->ntab);
	if(scaler != 0x74727565 && scaler != 0x10000){
		werrstr("unknown scaler type %#ux", scaler);
		return -1;
	}
	f->tab = mallocz(sizeof(TTTable) * f->ntab, 1);
	if(f->tab == nil) return -1;
	for(i = 0; i < f->ntab; i++)
		ttfunpack(f, "llll", &f->tab[i].tag, &f->tab[i].csum, &f->tab[i].offset, &f->tab[i].len);
	return 0;
}

int
ttfgototable(TTFontU *f, char *str)
{
	TTTable *t;
	u32int tag;
	
	tag = (u8int)str[0] << 24 | (u8int)str[1] << 16 | (u8int)str[2] << 8 | (u8int)str[3];
	for(t = f->tab; t < f->tab + f->ntab; t++)
		if(t->tag == tag){
			Bseek(f->bin, t->offset, 0);
			return t->len;
		}
	werrstr("no such table '%s'", str);
	return -1;
}

static int
ttfparseloca(TTFontU *f)
{
	int len, i;
	u32int x;
	
	len = ttfgototable(f, "loca");
	if(len < 0) return -1;
	x = 0;
	if(f->longloca){
		if(len > (f->numGlyphs + 1) * 4) len = (f->numGlyphs + 1) * 4;
		for(i = 0; i < len/4; i++){
			x = Bgetc(f->bin) << 24;
			x |= Bgetc(f->bin) << 16;
			x |= Bgetc(f->bin) << 8;
			x |= Bgetc(f->bin);
			f->ginfo[i].loca = x;
		}
	}else{
		if(len > (f->numGlyphs + 1) * 2) len = (f->numGlyphs + 1) * 2;
		for(i = 0; i < len/2; i++){
			x = Bgetc(f->bin) << 8;
			x |= Bgetc(f->bin);
			f->ginfo[i].loca = x * 2;
		}
	}
	for(; i < f->numGlyphs; i++)
		f->ginfo[i].loca = x;
	return 0;
}

static int
ttfparsehmtx(TTFontU *f)
{
	int i;
	u16int x, y;
	int len;
	int maxlsb;
	
	len = ttfgototable(f, "hmtx");
	if(len < 0)
		return -1;
	if(f->numOfLongHorMetrics > f->numGlyphs){
		werrstr("nonsensical header: numOfLongHorMetrics > numGlyphs");
		return -1;
	}
	for(i = 0; i < f->numOfLongHorMetrics; i++){
		ttfunpack(f, "ww", &x, &y);
		f->ginfo[i].advanceWidth = x;
		f->ginfo[i].lsb = y;
	}
	maxlsb = (len - 2 * f->numOfLongHorMetrics) / 2;
	if(maxlsb > f->numGlyphs){
		werrstr("nonsensical header: maxlsb > f->numGlyphs");
		return -1;
	}
	for(; i < maxlsb; i++){
		ttfunpack(f, "w", &y);
		f->ginfo[i].advanceWidth = x;
		f->ginfo[i].lsb = y;
	}
	for(; i < f->numGlyphs; i++){
		f->ginfo[i].advanceWidth = x;
		f->ginfo[i].lsb = y;
	}
	return 0;
}

int
ttfparsecvt(TTFontU *f)
{
	int len;
	int i;
	int x;
	u8int *p;
	short *w;

	len = ttfgototable(f, "cvt ");
	if(len <= 0) return 0;
	f->cvtu = mallocz(len, 1);
	if(f->cvtu == 0) return -1;
	Bread(f->bin, f->cvtu, len);
	p = (u8int *) f->cvtu;
	f->ncvtu = len / 2;
	w = f->cvtu;
	for(i = 0; i < f->ncvtu; i++){
		x = (short)(p[0] << 8 | p[1]);
		p += 2;
		*w++ = x;
	}
	return 0;
}

static int
ttfparseos2(TTFontU *f)
{
	int len;
	u16int usWinAscent, usWinDescent;
	
	len = ttfgototable(f, "OS/2 ");
	if(len < 0)
		return -1;
	if(len < 78){
		werrstr("OS/2 table too short");
		return -1;
	}
	ttfunpack(f, "68 6 ww", &usWinAscent, &usWinDescent);
	f->ascent = usWinAscent;
	f->descent = usWinDescent;
	return 0;
}

static void
ttfcloseu(TTFontU *u)
{
	int i;

	if(u == nil) return;
	Bterm(u->bin);
	for(i = 0; i < u->ncmap; i++)
		free(u->cmap[i].tab);
	free(u->cmap);
	free(u->ginfo);
	free(u->tab);
	free(u->cvtu);
	free(u);
}

void
ttfclose(TTFont *f)
{
	int i;

	if(f == nil) return;
	if(--f->u->ref <= 0)
		ttfcloseu(f->u);
	for(i = 0; i < f->u->maxFunctionDefs; i++)
		free(f->func[i].pgm);
	free(f->hintstack);
	free(f->func);
	free(f->storage);
	free(f->twilight);
	free(f->twiorg);
	free(f->cvt);
	free(f);
}

static TTFont *
ttfscaleu(TTFontU *u, int ppem)
{
	TTFont *f;
	int i;
	
	f = mallocz(sizeof(TTFont), 1);
	if(f == nil) return nil;
	f->u = u;
	u->ref++;
	f->ppem = ppem;
	f->ncvt = u->ncvtu;
	f->cvt = malloc(sizeof(int) * u->ncvtu);
	if(f->cvt == nil) goto error;
	for(i = 0; i < u->ncvtu; i++)
		f->cvt[i] = ttfrounddiv(u->cvtu[i] * ppem * 64, u->emsize);
	f->hintstack = mallocz(sizeof(u32int) * u->maxStackElements, 1);
	f->func = mallocz(sizeof(TTFunction) * u->maxFunctionDefs, 1);
	f->storage = mallocz(sizeof(u32int) * u->maxStorage, 1);
	f->twilight = mallocz(sizeof(TTPoint) * u->maxTwilightPoints, 1);
	f->twiorg = mallocz(sizeof(TTPoint) * u->maxTwilightPoints, 1);
	if(f->hintstack == nil || f->func == nil || f->storage == nil || f->twilight == nil || f->twiorg == nil) goto error;
	f->ascentpx = (u->ascent * ppem + u->emsize - 1) / (u->emsize);
	f->descentpx = (u->descent * ppem + u->emsize - 1) / (u->emsize);
	if(ttfrunfpgm(f) < 0) goto error;
	if(ttfruncvt(f) < 0) goto error;
	return f;

error:
	ttfclose(f);
	return nil;
}

TTFont *
ttfopen(char *name, int ppem, int)
{
	Biobuf *b;
	TTFontU *u;
	
	if(ppem < 0){
		werrstr("invalid ppem argument");
		return nil;
	}
	b = Bopen(name, OREAD);
	if(b == nil)
		return nil;
	u = mallocz(sizeof(TTFontU), 1);
	if(u == nil)
		return nil;
	u->bin = b;
	u->nkern = -1;
	if(directory(u) < 0) goto error;
	if(ttfgototable(u, "head") < 0) goto error;
	ttfunpack(u, "16 w W 16 wwww 6 w", &u->flags, &u->emsize, &u->xmin, &u->ymin, &u->xmax, &u->ymax, &u->longloca);
	if(ttfgototable(u, "maxp") < 0) goto error;
	ttfunpack(u, "4 wwwwwwwwwwwwww",
		&u->numGlyphs, &u->maxPoints, &u->maxCountours, &u->maxComponentPoints, &u->maxComponentCountours,
		&u->maxZones, &u->maxTwilightPoints, &u->maxStorage, &u->maxFunctionDefs, &u->maxInstructionDefs,
		&u->maxStackElements, &u->maxSizeOfInstructions, &u->maxComponentElements, &u->maxComponentDepth);
	u->ginfo = mallocz(sizeof(TTGlyphInfo) * (u->numGlyphs + 1), 1);
	if(u->ginfo == nil)
		goto error;
	if(ttfgototable(u, "hhea") < 0) goto error;
	ttfunpack(u, "10 wwww 16 w", &u->advanceWidthMax, &u->minLeftSideBearing, &u->minRightSideBearing, &u->xMaxExtent, &u->numOfLongHorMetrics);
	if(ttfparseloca(u) < 0) goto error;
	if(ttfparsehmtx(u) < 0) goto error;
	if(ttfparsecvt(u) < 0) goto error;
	if(ttfparsecmap(u) < 0) goto error;
	if(ttfparseos2(u) < 0) goto error;
	return ttfscaleu(u, ppem);

error:
	ttfcloseu(u);
	return nil;
}

TTFont *
ttfscale(TTFont *f, int ppem, int)
{
	return ttfscaleu(f->u, ppem);
}

int
ttfrounddiv(int a, int b)
{
	if(b < 0){ a = -a; b = -b; }
	if(a > 0)
		return (a + b/2) / b;
	else
		return (a - b/2) / b;
}

int
ttfvrounddiv(vlong a, int b)
{
	if(b < 0){ a = -a; b = -b; }
	if(a > 0)
		return (a + b/2) / b;
	else
		return (a - b/2) / b;
}
