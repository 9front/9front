#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

uchar pic[160*144*4*9];

static void
resolvetile(u8int tx, u8int ty, u8int toy, int window, u8int* tnl1, u8int *tnl2)
{
	u16int tni, tnli;
	u8int tn;
	
	tni = 0x9800 + 32 * ((u16int)ty) + ((u16int)tx);
	if(window){
		if(mem[LCDC] & WINDOWTILEMAP)
			tni += 0x400;
	}else
		if(mem[LCDC] & BGTILEMAP)
			tni += 0x400;
	tn = mem[tni];
	if(mem[LCDC] & BGTILEDATA)
		tnli = 0x8000 + 16 * (u16int)tn;
	else
		tnli = 0x9000 + 16 * (u16int)(schar)tn;
	*tnl1 = mem[tnli + 2 * ((u16int)toy)];
	*tnl2 = mem[tnli + 2 * ((u16int)toy) + 1];
}

static void
pixel(int x, int y, int val, int back)
{
	int Y;
	union { u8int c[4]; u32int l; } u;
	u32int *p, l;

	val = (3 - val) * 0x55;
	u.c[0] = val;
	u.c[1] = val;
	u.c[2] = val;
	u.c[3] = back ? 0 : 0xFF;
	l = u.l;
	if(scale == 3){
		p = ((u32int*)pic) + y * 3 * 3 * 160 + 3 * x;
		for(Y = 0; Y < 3; Y++){
			*p++ = l;
			*p++ = l;
			*p = l;
			p += 3 * 160 - 2;
		}
	}else if(scale == 2){
		p = ((u32int*)pic) + y * 2 * 2 * 160 + 2 * x;
		*p++ = l;
		*p = l;
		p += 2 * 160 - 1;
		*p++ = l;
		*p = l;
	}else{
		p = ((u32int*)pic) + y * 160 + x;
		*p = l;
	}
}

static void
pixelbelow(int x, int y, int val)
{
	if(pic[y*scale*scale*160*4 + x*scale*4 + 3] == 0)
		pixel(x, y, val, 0);
}

static void
drawbg(void)
{
	u8int Y, x, y, ty, toy, tx, tox, tnl1, tnl2, pal, val,h;
	
	Y = mem[LY];
	y = Y + mem[SCY];
	ty = y / 8;
	toy = y % 8;
	tx = mem[SCX] / 8;
	tox = mem[SCX] % 8;
	resolvetile(tx, ty, toy, 0, &tnl1, &tnl2);
	tnl1 <<= (tox+1) % 8;
	tnl2 <<= (tox+1) % 8;
	pal = mem[BGP];
	for(x = 0; x < 160; x++){
		tox++;
		if((tox % 8) == 0){
			tx++;
			resolvetile(tx%32, ty, toy, 0, &tnl1, &tnl2);
		}
		val = ((tnl1 & 0x80) >> 6) | ((tnl2 & 0x80) >> 5);
		h = val == 0;
		val = (pal >> val) & 3;
		pixel(x, Y, val, h);
		tnl1 <<= 1;
		tnl2 <<= 1;
	}
}

static void
drawsprites(void)
{
	u8int y, t, tnl1, tnl2, dx, ddx, val, pal;
	schar dy;
	u16int tnli;
	int i, x, big;
	struct { u8int y, x, t, f; } *s;
	
	y = mem[LY];
	big = mem[LCDC] & SPRITE16;
	s = (void*)(mem + 0xFE00);
	for(i = 0; i < 40; i++, s++){
		if(s->y == 0 || s->x == 0)
			continue;
		dy = y - s->y + 16;
		if(dy < 0 || dy >= (big ? 16 : 8))
			continue;
		pal = (s->f & (1<<4)) ? mem[OBP1] : mem[OBP0];
		if(s->f & (1<<6))
			dy = (big ? 15 : 7) - dy;
		t = s->t;
		if(big){
			if(dy >= 8){
				t |= 1;
				dy -= 8;
			}else
				t &= ~1;
		}
		tnli = 0x8000 + 2 * (u16int)dy + 16 * (u16int) t;
		tnl1 = mem[tnli];
		tnl2 = mem[tnli + 1];
		x = s->x - 9;
		for(dx = 0; dx < 8; dx++, x++){
			ddx = dx;
			if((s->f & (1<<5)) == 0)
				ddx = 7 - dx;
			val = ((tnl1 >> ddx) & 1) | (((tnl2 >> ddx) & 1) << 1);
			if(x < 0 || val == 0)
				continue;
			val = (pal >> (2 * val)) & 3;
			if(x >= 160)
				break;
			if(s->f & (1<<7))
				pixelbelow(x, y, val);
			else
				pixel(x, y, val, 0);
		}
	}
}

static void
drawwindow(void)
{
	u8int wx, wy, Y, y, ty, toy, tx, tox, tnl1, tnl2, x, val, pal;
	if(mem[WX] < 7)
		return;
	wx = mem[WX] - 7;
	wy = mem[WY];
	Y = mem[LY];
	if(Y < wy)
		return;
	y = Y - wy;
	ty = y / 8;
	toy = y % 8;
	tx = 0;
	tox = 0;
	resolvetile(tx, ty, toy, 1, &tnl1, &tnl2);
	pal = mem[BGP];
	for(x = wx; x < 160; x++){
		tox++;
		if((tox & 7) == 0){
			tx++;
			resolvetile(tx, ty, toy, 1, &tnl1, &tnl2);
		}
		val = ((tnl1 & 0x80) >> 6) | ((tnl2 & 0x80) >> 5);
		val = (pal >> val) & 3;
		pixel(x, Y, val, 0);
		tnl1 <<= 1;
		tnl2 <<= 1;
	}
}

void
ppustep(void)
{
	if(mem[LY] == 144){
		mem[STAT] &= ~3;
		mem[STAT] |= 1;
		interrupt(INTVBLANK);
	}
	if(mem[LY] == mem[LYC]){
		mem[STAT] |= 4;
		if(mem[STAT] & 64)
			interrupt(INTLCDC);
	}else
		mem[STAT] &= ~4;
	if(mem[LY] < 144)
		mem[STAT] &= ~3;
	if(mem[LY] < 144 && (mem[LCDC] & LCDOP)){
		if(mem[LCDC] & BGDISP)
			drawbg();
		if(mem[LCDC] & WINDOWDISP)
			drawwindow();
		if(mem[LCDC] & SPRITEDISP)
			drawsprites();
	}
	mem[LY]++;
	if(mem[LY] > 160){
		mem[LY] = 0;
		if((mem[LCDC] & LCDOP) == 0)
			memset(pic, 0, sizeof(pic));
		flush();
	}
}
