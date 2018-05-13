#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

int hblank, ppuy;
u8int bldy, blda, bldb;
u32int hblclock;
int ppux0;
u32int pixcol[480];
u8int pixpri[480];
u8int pixwin[240];
int objalpha;

typedef struct bg bg;
struct bg {
	uchar n;
	s32int rpx0, rpy0, rpx1, rpy1, rpx, rpy;
	u16int tx, ty;
	u8int tnx, tny;
	
	u8int mosaic, mctr, lasti;
	u32int curc;
	u8int curpri;
};
static bg bgst[4] = {{.n = 0}, {.n = 1}, {.n = 2}, {.n = 3}};

Var ppuvars[] = {
	VAR(hblank), VAR(ppuy), VAR(hblclock),
	VAR(bldy), VAR(blda), VAR(bldb), VAR(objalpha),
	VAR(bgst[2].rpx0), VAR(bgst[2].rpy0), VAR(bgst[3].rpx0), VAR(bgst[3].rpy0),
	{nil, 0, 0},
};

typedef struct sprite sprite;
struct sprite {
	uchar w, wb, h;
	s16int x;
	uchar ysh;
	
	uchar *base;
	u16int *pal;
	u16int inc;

	u32int t0;
	u16int t1;
	uchar depth;
	
	s32int rx, ry;
	s16int dx, dy;
	
	u8int mctr, mcol;
};
static sprite sprt[128], *sp = sprt;
enum {
	SPRROT = 1<<8,
	SPRDIS = 1<<9,
	SPRDOUB = 1<<9,
	SPRMOSA = 1<<12,
	SPR8 = 1<<13,
	SPRWIDE = 1<<14,
	SPRTALL = 1<<15,
	SPRHFLIP = 1<<28,
	SPRVFLIP = 1<<29,
	SPRSIZE0 = 1<<30,
	SPRSIZE1 = 1<<31,

	NOWIN = 0,
	OBJWIN = 1,
	WIN2 = 2,
	WIN1 = 4,
	
	OBJALPHA = 1<<16,
	SRCOBJ = 4<<17,
	SRCBACK = 5<<17,
	
	VACANT = 0x10,
	BACKDROP = 8,
};
#define SRCBG(n) ((n)<<17)

void
sprinit(void)
{
	u16int *p, *pp;
	u16int cnt, t1;
	u32int t0;
	int budg;
	uchar ws, h, hb, d, dy, s;
	static uchar wss[16] = {3, 4, 5, 6, 4, 5, 5, 6, 3, 3, 4, 5};
	static uchar hss[16] = {3, 4, 5, 6, 3, 3, 4, 5, 4, 5, 5, 6};

	sp = sprt;
	cnt = reg[DISPCNT];
	budg = (cnt & HBLFREE) != 0 ? 954 : 1210;
	for(p = oam; p < oam + 512; p += 4){
		t0 = p[0];
		if((t0 & (SPRROT|SPRDIS)) == SPRDIS)
			continue;
		t0 |= p[1] << 16;
		s = t0 >> 30 & 3 | t0 >> 12 & 12;
		hb = h = 1 << hss[s];
		dy = ppuy - (u8int) t0;
		if((t0 & (SPRROT|SPRDOUB)) == (SPRROT|SPRDOUB))
			hb <<= 1;
		if(dy >= hb || (u8int)t0 + hb > 256 && ppuy + 256 - (u8int)t0 >= hb)
			continue;
		if((t0 & SPRMOSA) != 0){
			dy = dy - dy % ((reg[MOSAIC] >> 12 & 15) + 1);
			sp->mctr = 0;
		}
		sp->x = (s32int)(t0 << 7) >> 23;
		sp->t0 = t0;
		ws = wss[s];
		sp->wb = sp->w = 1<<ws;
		sp->h = h;
		sp->t1 = t1 = p[2];
		sp->base = vram + 0x10000 + ((t1 & 0x3ff) << 5);
		d = (t0 & SPR8) != 0;
		sp->ysh = (cnt & OBJNOMAT) != 0 ? 2 + d + ws : 10;
		if((t0 & SPRROT) != 0){
			if((t0 & SPRDOUB) != 0)
				sp->wb <<= 1;
			budg -= 10 + sp->w*2;
			pp = oam + 3 + (t0 >> 21 & 0x1f0);
			sp->dx = pp[0];
			sp->dy = pp[8];
			sp->rx = (dy - hb/2) * (s16int) pp[4] + (sp->w << 7) - sp->dx * sp->wb/2;
			sp->ry = (dy - hb/2) * (s16int)pp[12] + (sp->h << 7) - sp->dy * sp->wb/2;
			if(sp->x < 0){
				sp->rx -= sp->x * sp->dx;
				sp->ry -= sp->x * sp->dy;
			}
		}else{
			budg -= sp->w;
			if((t0 & SPRVFLIP) != 0)
				dy = h - 1 - dy;
			sp->base += (dy & 7) << 2 + d;
			sp->base += dy >> 3 << sp->ysh;
			if((t0 & SPRHFLIP) != 0)
				sp->base += sp->w - 7 << 2 + d;
			sp->inc = (1 << 5 + d) - (1 << 2 + d);
			if(sp->x < 0)
				if((t0 & SPRHFLIP) != 0){
					sp->base -= ((-sp->x & 7) >> 1 - d) + (-sp->x >> 3 << 5 + d);
					if((t0 & SPR8) == 0 && (sp->x & 1) != 0)
						sp->base--;
				}else
					sp->base += ((-sp->x & 7) >> 1 - d) + (-sp->x >> 3 << 5 + d);
		}
		if((t0 & SPR8) != 0)
			sp->pal = pram + 0x100;
		else
			sp->pal = pram + 0x100 + (t1 >> 8 & 0xf0);
		if(budg < 0)
			break;
		sp++;
	}
}

void
spr(int x1)
{
	int x0, i, dx, sx0, sx1;
	u8int pri, v, d, *b;
	u16int x, y;
	u32int c, t0;
	sprite *s;
	
	x0 = ppux0;
	for(s = sprt; s < sp; s++){
		if(s->x >= x1 || s->x + s->wb <= x0)
			continue;
		t0 = s->t0;
		pri = s->t1 >> 10 & 3;
		sx0 = s->x >= x0 ? s->x : x0;
		sx1 = s->x + s->wb;
		if(x1 < sx1)
			sx1 = x1;
		dx = sx0 - s->x;
		for(i = sx0; i < sx1; i++, dx++){
			if((t0 & SPRROT) != 0){
				d = (t0 & SPR8) != 0;
				x = s->rx >> 8;
				y = s->ry >> 8;
				s->rx += s->dx;
				s->ry += s->dy;
				if(x < s->w && y < s->h){
					b = s->base;
					b += (y & 7) << 2 + d;
					b += y >> 3 << s->ysh;
					b += (x & 7) >> 1 - d;
					b += x >> 3 << 5 + d;
					v = *b;
					if(!d)
						if((x & 1) != 0)
							v >>= 4;
						else
							v &= 0xf;
				}else
					v = 0;
			}else if((t0 & SPRHFLIP) != 0){
				if((t0 & SPR8) != 0)
					v = *--s->base;
				else if((dx & 1) != 0)
					v = *s->base & 0x0f;
				else
					v = *--s->base >> 4;
				if((dx & 7) == 7)
					s->base -= s->inc;
			}else{
				v = *s->base;
				if((t0 & SPR8) != 0)
					s->base++;
				else if((dx & 1) != 0){
					v >>= 4;
					s->base++;
				}else
					v &= 0xf;
				if((dx & 7) == 7)
					s->base += s->inc;
			}
			if((t0 & SPRMOSA) != 0)
				if(s->mctr == 0){
					s->mctr = reg[MOSAIC] >> 8 & 15;
					s->mcol = v;
				}else{
					--s->mctr;
					v = s->mcol;
				}
			if(v != 0){
				c = s->pal[v] | SRCOBJ;
				switch(t0 >> 10 & 3){
				case 1:
					c |= OBJALPHA;
					objalpha++;
				case 0:
					if(pri < pixpri[i]){
						pixcol[i] = c;
						pixpri[i] = pri;
					}
					break;
				case 2:
					if((reg[DISPCNT] & 1<<15) != 0)
						pixwin[i] |= OBJWIN;
					break;
				}
			}
		}
	}
}

void
bgpixel(bg *b, int i, u32int c, int pri)
{
	u8int *p;
	u32int *q;
	int j;

	if(b != nil){
		c |= SRCBG(b->n);
		if(b->mosaic){
			for(j = (u8int)(b->lasti+1); j <= i; j++){
				if(b->mctr == 0){
					if(j == i){
						b->curc = c;
						b->curpri = pri;
					}else
						b->curpri = VACANT;
					b->mctr = reg[MOSAIC] & 15;
				}else
					b->mctr--;
				if(b->curpri != VACANT && (pixwin[j] & 1<<b->n) == 0)
					bgpixel(nil, j, b->curc, b->curpri);
			}
			b->lasti = i;
			return;
		}
	}
	p = pixpri + i;
	q = pixcol + i;
	if(pri < p[0]){
		p[240] = p[0];
		p[0] = pri;
		q[240] = q[0];
		q[0] = c;
	}else if(pri < p[240]){
		p[240] = pri;
		q[240] = c;
	}
}



void
bginit(bg *b, int scal, int)
{
	u16int x, y;
	u16int *rr;
	int msz;

	b->mosaic = (reg[BG0CNT + b->n] & BGMOSAIC) != 0;
	if(b->mosaic){
		b->mctr = 0;
		b->lasti = -1;
	}
	if(scal){
		rr = reg + (b->n - 2 << 3);
		if(ppuy == 0){
			b->rpx0 = (s32int)(rr[BG2XL] | rr[BG2XH] << 16) << 4 >> 4;
			b->rpy0 = (s32int)(rr[BG2YL] | rr[BG2YH] << 16) << 4 >> 4;
		}
		if(!b->mosaic || ppuy % ((reg[MOSAIC] >> 4 & 15) + 1) == 0){
			b->rpx1 = b->rpx0;
			b->rpy1 = b->rpy0;
		}
		b->rpx = b->rpx1;
		b->rpy = b->rpy1;
		b->rpx0 += (s16int)rr[BG2PB];
		b->rpy0 += (s16int)rr[BG2PD];
	}else{
		rr = reg + (b->n << 1);
		x = rr[BG0HOFS] & 0x1ff;
		y = ppuy;
		if(b->mosaic){
			msz = (reg[MOSAIC] >> 4 & 15) + 1;
			y = y - y % msz;
		}
		y += (rr[BG0VOFS] & 0x1ff);
		b->tx = x >> 3;
		b->ty = y >> 3;
		b->tnx = x & 7;
		b->tny = y & 7;
	}
}

void
bgsinit(void)
{
	switch(reg[DISPCNT] & 7){
	case 0:
		bginit(&bgst[0], 0, 0);
		bginit(&bgst[1], 0, 0);
		bginit(&bgst[2], 0, 0);
		bginit(&bgst[3], 0, 0);
		break;
	case 1:
		bginit(&bgst[0], 0, 0);
		bginit(&bgst[1], 0, 0);
		bginit(&bgst[2], 1, 0);
		break;
	case 2:
		bginit(&bgst[2], 1, 0);
		bginit(&bgst[3], 1, 0);
		break;
	case 3:
	case 4:
	case 5:
		bginit(&bgst[2], 1, 1);
		break;
	}	
}

void
bitbg(bg *b, int x1)
{
	u8int *base, *p, pri, d;
	u16int cnt, *rr, sx, sy;
	int i, v;
	
	cnt = reg[DISPCNT];
	if((cnt & 1<<8 + b->n) == 0)
		return;
	rr = reg + (b->n - 2 << 3);
	if((cnt & 7) != 5){
		sx = 240 << 8;
		sy = 160 << 8;
		d = (cnt & 7) == 3;
	}else{
		sx = 160 << 8;
		sy = 128 << 8;
		d = 1;
	}
	base = vram;
	if((cnt & FRAME) != 0 && (cnt & 7) != 3)
		base += 0xa000;
	pri = reg[BG0CNT + b->n] & 3;
	for(i = ppux0; i < x1; i++){
		if(((pixwin[i] & 1<<b->n) == 0 || b->mosaic) && (u32int)b->rpx < sx && (u32int)b->rpy < sy){
			if(d){
				p = base + 2 * (b->rpx >> 8) + 480 * (b->rpy >> 8);
				v = p[0] | p[1] << 8;
			}else{
				v = base[(b->rpx >> 8) + 240 * (b->rpy >> 8)];
				if(v != 0)
					v = pram[v];
				else
					v = -1;
			}
			if(v >= 0)
				bgpixel(b, i, v, pri);
	
		}
		b->rpx += (s16int) rr[BG2PA];
		b->rpy += (s16int) rr[BG2PC];
	}
}

void
txtbg(bg *b, int x1)
{
	u8int y, v, d, *cp;
	u16int bgcnt, ta0, ta, tx, ty, t, *pal;
	u32int ca;
	int i, x, mx;
	
	if((reg[DISPCNT] & 1<<8 + b->n) == 0)
		return;
	bgcnt = reg[BG0CNT + b->n];
	d = bgcnt >> 7 & 1;
	tx = b->tx;
	ty = b->ty;
	ta0 = (bgcnt << 3 & 0xf800) + ((ty & 0x1f) << 6);
	switch(bgcnt >> 14){
	case 2: ta0 += ty << 6 & 0x800; break;
	case 3: ta0 += ty << 7 & 0x1000; break;
	}
	x = ppux0;
	i = b->tnx;
	for(; x < x1; tx++, i = 0){
		ta = ta0 + ((tx & 0x1f) << 1);
		if((bgcnt & 1<<14) != 0)
			ta += tx << 6 & 0x800;
		t = vram[ta] | vram[ta+1] << 8;
		if(d)
			pal = pram;
		else
			pal = pram + (t >> 8 & 0xf0);
		ca = (bgcnt << 12 & 0xc000) + ((t & 0x3ff) << 5+d);
		if(ca >= 0x10000)
			continue;
		y = b->tny;
		if((t & 1<<11) != 0)
			y ^= 7;
		ca += y << 2+d;
		cp = vram + ca;
		for(; i < 8; i++, x++){
			if(x >= x1)
				goto out;
			if((pixwin[x] & 1<<b->n) != 0 && !b->mosaic)
				continue;
			mx = i;
			if((t & 1<<10) != 0)
				mx ^= 7;
			v = cp[mx >> 1-d];
			if(!d)
				if((mx & 1) != 0)
					v >>= 4;
				else
					v &= 0xf;
			if(v != 0)
				bgpixel(b, x, pal[v], bgcnt & 3);
		}
	}
out:
	b->tx = tx;
	b->tnx = i;
}

void
rotbg(bg *b, int x1)
{
	uchar *p, v;
	u16int bgcnt, *rr, ta;
	int i, row, sz, x, y;

	rr = reg + (b->n - 2 << 3);
	if((reg[DISPCNT] & 1<<8 + b->n) == 0)
		return;
	bgcnt = reg[BG0CNT + b->n];
	row = (bgcnt >> 14) + 4;
	sz = 1 << 3 + row;
	for(i = ppux0; i < x1; i++){
		x = b->rpx >> 8;
		y = b->rpy >> 8;
		b->rpx += (s16int) rr[BG2PA];
		b->rpy += (s16int) rr[BG2PC];
		if((pixwin[i] & 1<<b->n) != 0 && !b->mosaic)
			continue;
		if((bgcnt & DISPWRAP) != 0){
			x &= sz - 1;
			y &= sz - 1;
		}else if((uint)x >= sz || (uint)y >= sz)
			 continue;
		ta = (bgcnt << 3 & 0xf800) + ((y >> 3) << row) + (x >> 3);
		p = vram + (bgcnt << 12 & 0xc000) + (vram[ta] << 6);
		p += (x & 7) + ((y & 7) << 3);
		if((v = *p) != 0)
			bgpixel(b, i, pram[v], bgcnt & 3);
		
	}
}

void
windows(int x1)
{
	static u8int wintab[8] = {2, 3, 1, 1, 0, 0, 0, 0};
	int i, sx0, sx1;
	u16int v, h;
	u16int cnt;
	
	cnt = reg[DISPCNT];
	if((cnt >> 13) != 0){
		if((cnt & 1<<13) != 0){
			v = reg[WIN0V];
			h = reg[WIN0H];
			if(ppuy < (u8int)v && ppuy >= v >> 8){
				sx1 = (u8int)h;
				sx0 = h >> 8;
				if(sx0 < ppux0)
					sx0 = ppux0;
				if(sx1 > x1)
					sx1 = x1;
				for(i = sx0; i < sx1; i++)
					pixwin[i] |= WIN1;
			}
		}
		if((cnt & 1<<14) != 0){
			v = reg[WIN1V];
			h = reg[WIN1H];
			if(ppuy < (u8int)v && ppuy >= v >> 8){
				sx1 = (u8int)h;
				sx0 = h >> 8;
				if(sx0 < ppux0)
					sx0 = ppux0;
				if(sx1 > x1)
					sx1 = x1;
				for(i = sx0; i < sx1; i++)
					pixwin[i] |= WIN2;
			}
		}
		for(i = ppux0; i < x1; i++){
			v = wintab[pixwin[i]];
			h = reg[WININ + (v & 2) / 2];
			if((v & 1) != 0)
				h >>= 8;
			pixwin[i] = ~h;
		}
	}
	for(i = ppux0; i < x1; i++)
		if(pixpri[i] == VACANT || (pixwin[i] & 1<<4) != 0){
			pixcol[i] = pram[0] | SRCBACK;
			pixpri[i] = BACKDROP;
		}else{
			pixcol[i+240] = pram[0] | SRCBACK;
			pixpri[i+240] = BACKDROP;
		}
	objalpha = 0;
}

u16int
mix(u16int c1, u16int c2)
{
	u16int r, g, b, eva, evb;

	eva = blda;
	evb = bldb;
	b = ((c1 & 0x7c00) * eva + (c2 & 0x7c00) * evb) >> 4;
	g = ((c1 & 0x03e0) * eva + (c2 & 0x03e0) * evb) >> 4;
	r = ((c1 & 0x001f) * eva + (c2 & 0x001f) * evb) >> 4;
	if(b > 0x7c00) b = 0x7c00;
	if(g > 0x03e0) g = 0x03e0;
	if(r > 0x001f) r = 0x001f;
	return b & 0x7c00 | g & 0x03e0 | r;
}

u16int
brighten(u16int c1)
{
	u16int r, g, b, y;
	
	y = bldy;
	b = c1 & 0x7c00;
	b = b + (0x7c00 - b) * y / 16;
	g = c1 & 0x03e0;
	g = g + (0x03e0 - g) * y / 16;
	r = c1 & 0x001f;
	r = r + (0x001f - r) * y / 16;
	if(b > 0x7c00) b = 0x7c00;
	if(g > 0x03e0) g = 0x03e0;
	if(r > 0x001f) r = 0x001f;
	return b & 0x7c00 | g & 0x03e0 | r;
}

u16int
darken(u16int c1)
{
	u16int r, g, b, y;

	y = 16 - bldy;
	b = c1 & 0x7c00;
	b = b * y / 16;
	g = c1 & 0x03e0;
	g = g * y / 16;
	r = c1 & 0x001f;
	r = r * y / 16;
	return b & 0x7c00 | g & 0x03e0 | r;
}

void
colormath(int x1)
{
	u16int bldcnt;
	u32int *p;
	int i;
	
	bldcnt = reg[BLDCNT];
	if((bldcnt & 3<<6) == 0 && objalpha == 0)
		return;
	p = pixcol + ppux0;
	for(i = ppux0; i < x1; i++, p++){
		if((*p & OBJALPHA) != 0)
			goto alpha;
		if((pixwin[i] & 1<<5) != 0 || (bldcnt & 1<<(*p >> 17)) == 0)
			continue;
		switch(bldcnt >> 6 & 3){
		case 1:
		alpha:
			if((bldcnt & 1<<8+(p[240] >> 17)) == 0)
				continue;
			*p = mix(*p, p[240]);
			break;
		case 2:
			*p = brighten(*p);
			break;
		case 3:
			*p = darken(*p);
			break;
		}
	}
}

void
syncppu(int x1)
{
	int i;
	u16int cnt;

	if(hblank || ppuy >= 160)
		return;
	if(x1 >= 240)
		x1 = 240;
	else if(x1 <= ppux0)
		return;
	cnt = reg[DISPCNT];
	if((cnt & FBLANK) != 0){
		for(i = ppux0; i < x1; i++)
			pixcol[i] = 0xffff;
		ppux0 = x1;
		return;
	}

	if((cnt & 1<<12) != 0)
		spr(x1);
	windows(x1);
	switch(cnt & 7){
	case 0:
		txtbg(&bgst[0], x1);
		txtbg(&bgst[1], x1);
		txtbg(&bgst[2], x1);
		txtbg(&bgst[3], x1);
		break;
	case 1:
		txtbg(&bgst[0], x1);
		txtbg(&bgst[1], x1);
		rotbg(&bgst[2], x1);
		break;
	case 2:
		rotbg(&bgst[2], x1);
		rotbg(&bgst[3], x1);
		break;
	case 3:
	case 4:
	case 5:
		bitbg(&bgst[2], x1);
	}
	colormath(x1);
	ppux0 = x1;
}

void
linecopy(u32int *p, int y)
{
	uchar *q;
	u16int *r;
	u16int v;
	union { u16int w; u8int b[2]; } u;
	int n;

	q = pic + y * 240 * 2 * scale;
	r = (u16int*)q;
	n = 240;
	while(n--){
		v = *p++;
		u.b[0] = v;
		u.b[1] = v >> 8;
		switch(scale){
		case 16: *r++ = u.w;
		case 15: *r++ = u.w;
		case 14: *r++ = u.w;
		case 13: *r++ = u.w;
		case 12: *r++ = u.w;
		case 11: *r++ = u.w;
		case 10: *r++ = u.w;
		case 9: *r++ = u.w;
		case 8: *r++ = u.w;
		case 7: *r++ = u.w;
		case 6: *r++ = u.w;
		case 5: *r++ = u.w;
		case 4: *r++ = u.w;
		case 3: *r++ = u.w;
		case 2: *r++ = u.w;
		default: *r++ = u.w;
		}
	}
}

void
hblanktick(void *)
{
	extern Event evhblank;
	u16int stat;

	stat = reg[DISPSTAT];
	if(hblank){
		hblclock = clock + evhblank.time;
		addevent(&evhblank, 240*4);
		hblank = 0;
		ppux0 = 0;
		memset(pixpri, VACANT, sizeof(pixpri));
		memset(pixwin, 0, 240);
		if(++ppuy >= 228){
			ppuy = 0;
			flush();
		}
		if(ppuy < 160){
			sprinit();
			bgsinit();
		}else if(ppuy == 160){
			dmastart(DMAVBL);
			if((stat & IRQVBLEN) != 0)
				setif(IRQVBL);
		}
		if((stat & IRQVCTREN) != 0 && ppuy == stat >> 8)
			setif(IRQVCTR);
	}else{
		syncppu(240);
		if(ppuy < 160)
			linecopy(pixcol, ppuy);
		addevent(&evhblank, 68*4);
		hblank = 1;
		if((stat & IRQHBLEN) != 0)
			setif(IRQHBL);
		if(ppuy < 160)
			dmastart(DMAHBL);
	}
}

void
ppuwrite(u16int a, u16int v)
{
	syncppu((clock - hblclock) / 4);
	switch(a){
	case BLDALPHA*2:
		blda = v & 0x1f;
		if(blda > 16)
			blda = 16;
		bldb = v >> 8 & 0x1f;
		if(bldb > 16)
			bldb = 16;
		break;
	case BLDY*2:
		bldy = v & 0x1f;
		if(bldy > 16)
			bldy = 16;
		break;
	case BG2XL*2: bgst[2].rpx0 = bgst[2].rpx0 & 0xffff0000 | v; break;
	case BG2XH*2: bgst[2].rpx0 = bgst[2].rpx0 & 0xffff | (s32int)(v << 20) >> 4; break;
	case BG2YL*2: bgst[2].rpy0 = bgst[2].rpy0 & 0xffff0000 | v; break;
	case BG2YH*2: bgst[2].rpy0 = bgst[2].rpy0 & 0xffff | (s32int)(v << 20) >> 4; break;
	case BG3XL*2: bgst[3].rpx0 = bgst[3].rpx0 & 0xffff0000 | v; break;
	case BG3XH*2: bgst[3].rpx0 = bgst[3].rpx0 & 0xffff | (s32int)(v << 20) >> 4; break;
	case BG3YL*2: bgst[3].rpy0 = bgst[3].rpy0 & 0xffff0000 | v; break;
	case BG3YH*2: bgst[3].rpy0 = bgst[3].rpy0 & 0xffff | (s32int)(v << 20) >> 4; break;
	}
}
