#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

int ppuy, ppux, odd;

static void
pixel(int x, int y, int val, int back)
{
	union { u8int c[4]; u32int l; } u;
	u32int *p;
	static u8int palred[64] = {
		0x7C, 0x00, 0x00, 0x44, 0x94, 0xA8, 0xA8, 0x88, 
		0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
		0xBC, 0x00, 0x00, 0x68, 0xD8, 0xE4, 0xF8, 0xE4, 
		0xAC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
		0xF8, 0x3C, 0x68, 0x98, 0xF8, 0xF8, 0xF8, 0xFC, 
		0xF8, 0xB8, 0x58, 0x58, 0x00, 0x78, 0x00, 0x00, 
		0xFC, 0xA4, 0xB8, 0xD8, 0xF8, 0xF8, 0xF0, 0xFC, 
		0xF8, 0xD8, 0xB8, 0xB8, 0x00, 0xF8, 0x00, 0x00, 
	};
	static u8int palgreen[64] = {
		0x7C, 0x00, 0x00, 0x28, 0x00, 0x00, 0x10, 0x14, 
                0x30, 0x78, 0x68, 0x58, 0x40, 0x00, 0x00, 0x00, 
                0xBC, 0x78, 0x58, 0x44, 0x00, 0x00, 0x38, 0x5C, 
                0x7C, 0xB8, 0xA8, 0xA8, 0x88, 0x00, 0x00, 0x00, 
                0xF8, 0xBC, 0x88, 0x78, 0x78, 0x58, 0x78, 0xA0, 
                0xB8, 0xF8, 0xD8, 0xF8, 0xE8, 0x78, 0x00, 0x00, 
                0xFC, 0xE4, 0xB8, 0xB8, 0xB8, 0xA4, 0xD0, 0xE0, 
                0xD8, 0xF8, 0xF8, 0xF8, 0xFC, 0xD8, 0x00, 0x00, 
	};
	static u8int palblue[64] = {
		0x7C, 0xFC, 0xBC, 0xBC, 0x84, 0x20, 0x00, 0x00, 
                0x00, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 
                0xBC, 0xF8, 0xF8, 0xFC, 0xCC, 0x58, 0x00, 0x10, 
                0x00, 0x00, 0x00, 0x44, 0x88, 0x00, 0x00, 0x00, 
                0xF8, 0xFC, 0xFC, 0xF8, 0xF8, 0x98, 0x58, 0x44, 
                0x00, 0x18, 0x54, 0x98, 0xD8, 0x78, 0x00, 0x00, 
                0xFC, 0xFC, 0xF8, 0xF8, 0xF8, 0xC0, 0xB0, 0xA8, 
                0x78, 0x78, 0xB8, 0xD8, 0xFC, 0xF8, 0x00, 0x00, 
	};

	u.c[0] = palblue[val];
	u.c[1] = palgreen[val];
	u.c[2] = palred[val];
	u.c[3] = back ? 0 : 0xFF;
	p = (u32int *)pic + y * 256 * scale + x * scale;
	switch(scale){
	case 16: *p++ = u.l;
	case 15: *p++ = u.l;
	case 14: *p++ = u.l;
	case 13: *p++ = u.l;
	case 12: *p++ = u.l;
	case 11: *p++ = u.l;
	case 10: *p++ = u.l;
	case 9: *p++ = u.l;
	case 8: *p++ = u.l;
	case 7: *p++ = u.l;
	case 6: *p++ = u.l;
	case 5: *p++ = u.l;
	case 4: *p++ = u.l;
	case 3: *p++ = u.l;
	case 2: *p++ = u.l;
	default: *p = u.l;
	}
}

static int
iscolor(int x, int y)
{
	return pic[(scale * 4) * (y * 256 + x) + 3] != 0;
}

static int
pal(int c, int a, int spr)
{
	if(c == 0)
		return ppuread(0x3F00);
	return ppuread(0x3F00 | ((a&3)<<2) | (c & 3) | (spr << 4));
}

static void
incppuy(void)
{
	int y;

	if((ppuv & 0x7000) != 0x7000){
		ppuv += 0x1000;
		return;
	}
	y = (ppuv >> 5) & 31;
	if(y++ == 29){
		y = 0;
		ppuv ^= 0x800;
	}
	ppuv = (ppuv & 0x0C1F) | ((y & 31) << 5);
}

static void
drawbg(void)
{
	static int t;
	u8int c, a;
	static u8int nr1, nr2, na;
	static u16int r1, r2, a1, a2;
	
	if(ppux >= 2 && ppux <= 257 || ppux >= 322 && ppux <= 337){
		c = (r1 >> (15-ppusx)) & 1 | (r2 >> (14-ppusx)) & 2;
		if(ppuy < 240 && ppux <= 257){
			a = (a1 >> (15-ppusx)) & 1 | (a2 >> (14-ppusx)) & 2;
			pixel(ppux-2, ppuy, pal(c, a, 0), c == 0);
		}
		r1 <<= 1;
		r2 <<= 1;
		a1 <<= 1;
		a2 <<= 1;
	}
	if(ppux >= 256 && ppux <= 320){
		if(ppux == 256)
			incppuy();
		if(ppux == 257)
			ppuv = (ppuv & 0x7BE0) | (pput & 0x041F);
		return;
	}
	switch(ppux & 7){
	case 0:
		if(ppux != 0){
			if((ppuv & 0x1f) == 0x1f){
				ppuv &= ~0x1f;
				ppuv ^= 0x400;
			}else
				ppuv++;
		}
		break;
	case 1:
		t = ppuread(0x2000 | ppuv & 0x0FFF);
		if(ppux != 1){
			r1 |= nr1;
			r2 |= nr2;
			if(na & 1)
				a1 |= 0xff;
			if(na & 2)
				a2 |= 0xff;
		}
		break;
	case 3:
		na = ppuread(0x23C0 | ppuv & 0x0C00 | ((ppuv & 0x0380) >> 4) | ((ppuv & 0x001C) >> 2));
		if((ppuv & 0x0002) != 0) na >>= 2;
		if((ppuv & 0x0040) != 0) na >>= 4;
		break;
	case 5:
		nr1 = ppuread(((mem[PPUCTRL] & BGTABLE) << 8) | t << 4 | ppuv >> 12);
		break;
	case 7:
		nr2 = ppuread(((mem[PPUCTRL] & BGTABLE) << 8) | t << 4 | ppuv >> 12 | 8);
		break;
	}
}

static void
drawsprites(int show)
{
	uchar *p;
	int big, dx, dy, i, x, cc, pri;
	u8int r1, r2, c;
	static int n, m, nz, s0, t0;
	static struct { u8int x, a; u16int t; } s[8], *sp;
	static struct { u8int x, a, r1, r2; } t[8];

	big = (mem[PPUCTRL] & BIGSPRITE) != 0;
	if(ppux == 65){
		s0 = 0;
		for(p = oam, sp = s, n = 0; p < oam + sizeof(oam); p += 4){
			if((dy = p[0]) >= 0xEF)
				continue;
			dy = ppuy - dy;
			if(dy < 0 || dy >= (big ? 16 : 8))
				continue;
			if(p == oam)
				s0 = 1;
			sp->t = p[1];
			sp->a = p[2];
			sp->x = p[3];
			if((sp->a & (1<<7)) != 0)
				dy = (big ? 15 : 7) - dy;
			if(big){
				sp->t |= (sp->t & 1) << 8;
				if(dy >= 8){
					sp->t |= 1;
					dy -= 8;
				}else
					sp->t &= 0x1fe;
			}else
				sp->t |= (mem[PPUCTRL] & SPRTABLE) << 5;
			sp->t = sp->t << 4 | dy;
			sp++;
			if(++n == 8)
				break;
		}
	}
	if(ppux >= 2 && ppux <= 257 && m > 0){
		x = ppux - 2;
		dx = x - t[0].x;
		if(t0 && dx >= 0 && dx < 8 && ppux != 257){
			if((nz & 1) != 0 && iscolor(x, ppuy) && show)
				mem[PPUSTATUS] |= SPRITE0HIT;
			nz >>= 1;
		}
		cc = -1;
		pri = 0;
		for(i = m - 1; i >= 0; i--){
			dx = x - t[i].x;
			if(dx < 0 || dx > 7)
				continue;
			c = (t[i].r1 & 1) | (t[i].r2 & 1) << 1;
			if(c != 0){
				cc = pal(c, t[i].a & 3, 1);
				pri = (t[i].a & (1<<5)) == 0;
			}
			t[i].r1 >>= 1;
			t[i].r2 >>= 1;
		}
		if(cc != -1 && show && (pri || !iscolor(x, ppuy)))
			pixel(x, ppuy, cc, 0);
	}
	if(ppux == 257){
		for(i = 0; i < n; i++){
			r1 = ppuread(s[i].t);
			r2 = ppuread(s[i].t | 8);
			if((s[i].a & (1<<6)) == 0){
				r1 = ((r1 * 0x80200802ULL) & 0x0884422110ULL) * 0x0101010101ULL >> 32;
				r2 = ((r2 * 0x80200802ULL) & 0x0884422110ULL) * 0x0101010101ULL >> 32;
			}
			t[i].x = s[i].x;
			t[i].a = s[i].a;
			t[i].r1 = r1;
			t[i].r2 = r2;
		}
		m = n;
		nz = t[0].r1 | t[0].r2;
		t0 = s0;
	}
}

static void
flush(void)
{
	flushmouse(1);
	flushscreen();
	flushaudio(audioout);
}

void
ppustep(void)
{
	extern int nmi;
	int mask;

	if(ppuy < 240 || ppuy == 261){
		mask = mem[PPUMASK];
		if((mask & BGDISP) != 0)
			drawbg();
		if((((mask & BGDISP) == 0 && ppux <= 257 || ppux < 10 && (mask & BG8DISP) == 0) && ppux >= 2) && ppuy != 261)
			pixel(ppux - 2, ppuy, ppuread(0x3F00), 1);
		if((mask & SPRITEDISP) != 0 && ppuy != 261)
			drawsprites(ppux >= 10 || (mask & SPRITE8DISP) != 0);
		if(ppux == 240 && (mask & SPRITEDISP) != 0)
			mapper[map](SCAN, 0);
		if(ppuy == 261){
			if(ppux == 1)
				mem[PPUSTATUS] &= ~(PPUVBLANK|SPRITE0HIT);
			else if(ppux >= 280 && ppux <= 304 && (mask & BGDISP) != 0)
				ppuv = (pput & 0x7BE0) | (ppuv & 0x041F);
		}
	}else if(ppuy == 241){
		if(ppux == 1){
			mem[PPUSTATUS] |= PPUVBLANK;
			if((mem[PPUCTRL] & PPUNMI) != 0)
				nmi = 2;
			flush();
		}
	}
	ppux++;
	if(ppux > 340){
		ppux = 0;
		ppuy++;
		if(ppuy > 261){
			ppuy = 0;
			if(odd && (mem[PPUMASK] & (BGDISP | SPRITEDISP)) != 0)
				ppux++;
			odd ^= 1;
		}
	}
}
