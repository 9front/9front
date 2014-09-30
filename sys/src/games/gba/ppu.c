#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

int ppux, ppuy;
uchar pic[240*160*2*3*3];
u8int bldy, blda, bldb;

typedef struct bg bg;
struct bg {
	uchar n;
	uchar depth;

	s32int rpx0, rpy0, rpx, rpy;
	s32int sx, sy;
	
	u16int tx, ty;
	u8int tnx, tny;
	u16int t;
	u8int *chr;
	u16int *pal;
};
static u8int mode=-1;
static bg bgst[4] = {{.n = 0}, {.n = 1}, {.n = 2}, {.n = 3}};
static u32int pixeldat[2], pixelpri[2];
static u16int bgmask;
static u8int objwin, objtrans;

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
};
static sprite sprt[128], *sp = sprt;
enum {
	SPRROT = 1<<8,
	SPRDIS = 1<<9,
	SPRDOUB = 1<<9,
	SPR8 = 1<<13,
	SPRWIDE = 1<<14,
	SPRTALL = 1<<15,
	SPRHFLIP = 1<<28,
	SPRVFLIP = 1<<29,
	SPRSIZE0 = 1<<30,
	SPRSIZE1 = 1<<31
};

void
pixeldraw(int x, int y, u16int v)
{
	uchar *p;
	u16int *q;
	union { u16int w; u8int b[2]; } u;

	if(scale == 1){
		p = pic + (x + y * 240) * 2;
		p[0] = v;
		p[1] = v >> 8;
		return;
	}
	u.b[0] = v;
	u.b[1] = v >> 8;
	if(scale == 2){
		q = (u16int*)pic + (x + y * 240) * 2;
		q[0] = u.w;
		q[1] = u.w;
	}else{
		q = (u16int*)pic + (x + y * 240) * 3;
		q[0] = u.w;
		q[1] = u.w;
		q[2] = u.w;
	}
}

void
pixel(u16int c, int n, int p)
{
	if(p < pixelpri[0]){
		pixeldat[1] = pixeldat[0];
		pixelpri[1] = pixelpri[0];
		pixelpri[0] = p;
		pixeldat[0] = c | n << 16;
	}else if(p < pixelpri[1]){
		pixelpri[1] = p;
		pixeldat[1] = c | n << 16;
	}
}

void
tile(bg *b)
{
	u16int bgcnt, ta, tx, ty, y, t;
	u8int d;
	u8int *chr;
	
	bgcnt = reg[BG0CNT + b->n];
	d = bgcnt >> 7 & 1;
	tx = b->tx;
	ty = b->ty;
	ta = (bgcnt << 3 & 0xf800) + ((tx & 0x1f) << 1) + ((ty & 0x1f) << 6);
	switch(bgcnt >> 14){
	case 1: ta += tx << 6 & 0x800; break;
	case 2: ta += ty << 6 & 0x800; break;
	case 3: ta += tx << 6 & 0x800 | ty << 7 & 0x1000; break;
	}
	t = vram[ta] | vram[ta+1] << 8;
	b->t = t;
	chr = vram + (bgcnt << 12 & 0xc000) + ((t & 0x3ff) << 5+d);
	y = b->tny;
	if((t & 1<<11) != 0)
		y ^= 7;
	chr = chr + (y << 2+d);
	b->chr = chr;
	if(d != 0)
		b->pal = pram;
	else
		b->pal = pram + (t >> 8 & 0xf0);
}

void
bginit(bg *b, int scal, int)
{
	u16int cnt, x, y;
	u16int *rr;
	
	cnt = reg[DISPCNT];
	if(scal){
		rr = reg + (b->n - 2 << 3);
		if(ppuy == 0){
			b->rpx0 = (s32int)(rr[BG2XL] | rr[BG2XH] << 16) << 4 >> 4;
			b->rpy0 = (s32int)(rr[BG2YL] | rr[BG2YH] << 16) << 4 >> 4;
		}
		b->rpx = b->rpx0;
		b->rpy = b->rpy0;
		b->rpx0 += (s16int)rr[BG2PB];
		b->rpy0 += (s16int)rr[BG2PD];
		switch(cnt & 7){
		case 3:
		case 4:
			b->sx = 240 << 8;
			b->sy = 160 << 8;
			b->depth = (cnt & 7) == 3;
			break;
		case 5:
			b->sx = 160 << 8;
			b->sy = 128 << 8;
			b->depth = 1;
			break;
		}
	}else{
		rr = reg + (b->n << 1);
		x = rr[BG0HOFS] & 0x1ff;
		y = (rr[BG0VOFS] & 0x1ff) + ppuy;
		b->tx = x >> 3;
		b->ty = y >> 3;
		b->tnx = x & 7;
		b->tny = y & 7;
		tile(b);
	}
}

void
bgsinit(void)
{
	mode = reg[DISPCNT] & 7;
	switch(mode){
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
bitbg(bg *b)
{
	u16int cnt;
	int v;
	uchar *p;
	u16int *rr;
	uchar *base;
	
	cnt = reg[DISPCNT];
	rr = reg - 8 + (b->n << 3);
	if((bgmask & 1<<b->n) == 0)
		goto next;
	if(b->rpx >= 0 && b->rpy >= 0 && b->rpx <= b->sx && b->rpy <= b->sy){
		base = vram;
		if((cnt & FRAME) != 0 && (cnt & 7) != 3)
			base += 0xa000;
		if(b->depth){
			p = base + 2 * (b->rpx >> 8) + 480 * (b->rpy >> 8);
			v = p[0] | p[1] << 8;
		}else{
			v = base[(b->rpx >> 8) + 240 * (b->rpy >> 8)];
			if(v != 0)
				v = pram[v];
			else
				v = -1;
		}
	}else
		v = -1;
	if(v >= 0)
		pixel(v, b->n, reg[BG0CNT + b->n] & 3);
next:
	b->rpx += (s16int) rr[BG2PA];
	b->rpy += (s16int) rr[BG2PC];
}

void
rotbg(bg *b)
{
	u16int *rr, ta;
	u16int bgcnt;
	int row, sz, x, y;
	uchar *p, v;

	rr = reg - 8 + (b->n << 3);
	if((bgmask & 1<<b->n) == 0)
		goto next;
	bgcnt = reg[BG0CNT + b->n];
	row = (bgcnt >> 14) + 4;
	sz = 1 << 3 + row;
	x = b->rpx >> 8;
	y = b->rpy >> 8;
	if((bgcnt & DISPWRAP) != 0){
		x &= sz - 1;
		y &= sz - 1;
	}else if((uint)x >= sz || (uint)y >= sz)
		goto next;
	ta = (bgcnt << 3 & 0xf800) + ((y >> 3) << row) + (x >> 3);
	p = vram + (bgcnt << 12 & 0xc000) + (vram[ta] << 6);
	p += (x & 7) + ((y & 7) << 3);
	if((v = *p) != 0)
		pixel(pram[v], b->n, bgcnt & 3);
next:
	b->rpx += (s16int) rr[BG2PA];
	b->rpy += (s16int) rr[BG2PC];
}

void
txtbg(bg *b)
{
	u16int bgcnt;
	u8int x, v;

	bgcnt = reg[BG0CNT + b->n];
	if((bgmask & 1<<b->n) == 0)
		goto next;
	x = b->tnx;
	if((b->t & 1<<10) != 0)
		x ^= 7;
	if((bgcnt & BG8) != 0)
		v = b->chr[x];
	else{
		v = b->chr[x>>1];
		if((x & 1) != 0)
			v >>= 4;
		else
			v &= 0xf;
	}
	if(v != 0)
		pixel(b->pal[v], b->n, bgcnt & 3);
next:
	if(++b->tnx == 8){
		b->tnx = 0;
		b->tx++;
		tile(b);
	}
}

void
bgs(void)
{
	switch(mode){
	case 0:
		txtbg(&bgst[0]);
		txtbg(&bgst[1]);
		txtbg(&bgst[2]);
		txtbg(&bgst[3]);
		break;
	case 1:
		txtbg(&bgst[0]);
		txtbg(&bgst[1]);
		rotbg(&bgst[2]);
		break;
	case 2:
		rotbg(&bgst[2]);
		rotbg(&bgst[3]);
		break;
	case 3:
	case 4:
	case 5:
		bitbg(&bgst[2]);
		break;
	}
}

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
spr(void)
{
	sprite *s;
	ushort dx;
	u32int t0;
	uchar v;
	ushort x, y;
	u16int c;
	int pv, ppri, pri;
	uchar d;
	uchar *b;
	
	pv = -1;
	ppri = 6;;
	for(s = sprt; s < sp; s++){
		dx = ppux - s->x;
		if(dx >= s->wb)
			continue;
		t0 = s->t0;
		if((t0 & SPRROT) != 0){
			x = s->rx >> 8;
			y = s->ry >> 8;
			if(x < s->w && y < s->h){
				b = s->base;
				d = (t0 & SPR8) != 0;
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
			s->rx += s->dx;
			s->ry += s->dy;
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
		if(v != 0){
			pri = s->t1 >> 10 & 3;
			c = s->pal[v];
			switch(s->t0 >> 10 & 3){
			case 1:
				c |= 1<<16;
			case 0:
				if(ppri > pri){
					pv = c;
					ppri = pri;
				}
				break;
			case 2:
				objwin = 1;
				break;
			}
		}
	}
	if(pv >= 0){
		pixel(pv, 4, ppri);
		if(pv >> 16 != 0)
			objtrans = 1;
	}
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
windows(void)
{
	u16int dispcnt;
	u16int v, h;

	dispcnt = reg[DISPCNT];
	bgmask = dispcnt >> 8 | 1<<5;
	if((dispcnt >> 13) != 0){
		if((dispcnt & 1<<13) != 0){
			v = reg[WIN0V];
			h = reg[WIN0H];
			if(ppuy < (u8int)v && ppuy >= v >> 8 &&
				ppux < (u8int)h && ppux >= h >> 8){
				bgmask &= reg[WININ];
				goto windone;
			}
		}
		if((dispcnt & 1<<14) != 0){
			v = reg[WIN1V];
			h = reg[WIN1H];
			if(ppuy < (u8int)v && ppuy >= v >> 8 &&
				ppux < (u8int)h && ppux >= h >> 8){
				bgmask &= reg[WININ] >> 8;
				goto windone;
			}
		}
		if((dispcnt & 1<<15) != 0 && objwin != 0){
			bgmask &= reg[WINOUT] >> 8;
			goto windone;
		}
		bgmask &= reg[WINOUT];
	}
windone:
	if(pixelpri[0] != 8 && (bgmask & 1<<4) == 0){
		pixelpri[0] = 8;
		pixeldat[0] = pram[0] | 5 << 16;
	}
}

void
colormath(void)
{
	u8int src0;
	u16int bldcnt;
	
	if((bgmask & 1<<5) == 0)
		return;
	bldcnt = reg[BLDCNT];
	src0 = pixeldat[0] >> 16;
	if(objtrans && src0 == 4)
		goto alpha;
	if((bldcnt & 3<<6) == 0 || (bldcnt & 1<<src0) == 0)
		return;
	switch(bldcnt >> 6 & 3){
	case 1:
	alpha:
		if((bldcnt & 1<<8+(pixeldat[1]>>16)) == 0)
			return;
		pixeldat[0] = mix(pixeldat[0], pixeldat[1]);
		break;
	case 2:
		pixeldat[0] = brighten(pixeldat[0]);
		break;
	case 3:
		pixeldat[0] = darken(pixeldat[0]);
		break;
	}
}

void
ppustep(void)
{
	u16int stat;
	u16int cnt;
	
	stat = reg[DISPSTAT];
	cnt = reg[DISPCNT];
	if(ppuy < 160 && ppux < 240)
		if((cnt & FBLANK) == 0){
			objwin = 0;
			objtrans = 0;
			pixelpri[0] = 8;
			pixeldat[0] = pram[0] | 5 << 16;
			if((cnt & 1<<12) != 0)
				spr();
			windows();
			bgs();
			colormath();
			pixeldraw(ppux, ppuy, pixeldat[0]);
		}else
			pixeldraw(ppux, ppuy, 0xffff);
	if(ppux == 240 && ppuy < 160){
		if((stat & IRQHBLEN) != 0)
			setif(IRQHBL);
		dmastart(DMAHBL);
	}
	if(++ppux >= 308){
		ppux = 0;
		if(++ppuy >= 228){
			ppuy = 0;
			flush();
		}
		if((stat & IRQVCTREN) != 0 && ppuy == stat >> 8)
			setif(IRQVCTR);
		if(ppuy < 160){
			bgsinit();
			sprinit();
		}else if(ppuy == 160){
			if((stat & IRQVBLEN) != 0)
				setif(IRQVBL);
			dmastart(DMAVBL);
		}
	}
}
