#include <u.h>
#include <libc.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

int ppux=1, ppuy;
int col, pri;
u8int p0x, p1x, m0x, m1x, blx;
u16int coll;
u8int disp;
int p0difc;
int bwmod = 1<<3;

enum {
	SRCPF,
	SRCP0,
	SRCP1,
	SRCM0,
	SRCM1,
	SRCBL,
};

static void
pixeldraw(u8int v)
{
	u32int c;
	union { u32int l; u8int c[4]; } u;
	u32int *p;
	static u32int col[] = {
		0x000000, 0x404040, 0x6C6C6C, 0x909090, 0xB0B0B0, 0xC8C8C8, 0xDCDCDC, 0xECECEC, 
		0x444400, 0x646410, 0x848424, 0xA0A034, 0xB8B840, 0xD0D050, 0xE8E85C, 0xFCFC68, 
		0x702800, 0x844414, 0x985C28, 0xAC783C, 0xBC8C4C, 0xCCA05C, 0xDCB468, 0xECC878, 
		0x841800, 0x983418, 0xAC5030, 0xC06848, 0xD0805C, 0xE09470, 0xECA880, 0xFCBC94, 
		0x880000, 0x9C2020, 0xB03C3C, 0xC05858, 0xD07070, 0xE08888, 0xECA0A0, 0xFCB4B4, 
		0x78005C, 0x8C2074, 0xA03C88, 0xB0589C, 0xC070B0, 0xD084C0, 0xDC9CD0, 0xECB0E0, 
		0x480078, 0x602090, 0x783CA4, 0x8C58B8, 0xA070CC, 0xB484DC, 0xC49CEC, 0xD4B0FC, 
		0x140084, 0x302098, 0x4C3CAC, 0x6858C0, 0x7C70D0, 0x9488E0, 0xA8A0EC, 0xBCB4FC, 
		0x000088, 0x1C209C, 0x3840B0, 0x505CC0, 0x6874D0, 0x7C8CE0, 0x90A4EC, 0xA4B8FC, 
		0x00187C, 0x1C3890, 0x3854A8, 0x5070BC, 0x6888CC, 0x7C9CDC, 0x90B4EC, 0xA4C8FC, 
		0x002C5C, 0x1C4C78, 0x386890, 0x5084AC, 0x689CC0, 0x7CB4D4, 0x90CCE8, 0xA4E0FC, 
		0x003C2C, 0x1C5C48, 0x387C64, 0x509C80, 0x68B494, 0x7CD0AC, 0x90E4C0, 0xA4FCD4, 
		0x003C00, 0x205C20, 0x407C40, 0x5C9C5C, 0x74B474, 0x8CD08C, 0xA4E4A4, 0xB8FCB8, 
		0x143800, 0x345C1C, 0x507C38, 0x6C9850, 0x84B468, 0x9CCC7C, 0xB4E490, 0xC8FCA4, 
		0x2C3000, 0x4C501C, 0x687034, 0x848C4C, 0x9CA864, 0xB4C078, 0xCCD488, 0xE0EC9C, 
		0x442800, 0x644818, 0x846830, 0xA08444, 0xB89C58, 0xD0B46C, 0xE8CC7C, 0xFCE08C, 
	};
	
	c = col[v >> 1];
	u.c[0] = c;
	u.c[1] = c >> 8;
	u.c[2] = c >> 16;
	u.c[3] = 0xff;
	p = (u32int *)pic + ppuy * PICW * scale + ppux * 2 * scale;
	switch(scale){
	case 16: *p++ = u.l; *p++ = u.l;
	case 15: *p++ = u.l; *p++ = u.l;
	case 14: *p++ = u.l; *p++ = u.l;
	case 13: *p++ = u.l; *p++ = u.l;
	case 12: *p++ = u.l; *p++ = u.l;
	case 11: *p++ = u.l; *p++ = u.l;
	case 10: *p++ = u.l; *p++ = u.l;
	case 9: *p++ = u.l; *p++ = u.l;
	case 8: *p++ = u.l; *p++ = u.l;
	case 7: *p++ = u.l; *p++ = u.l;
	case 6: *p++ = u.l; *p++ = u.l;
	case 5: *p++ = u.l; *p++ = u.l;
	case 4: *p++ = u.l; *p++ = u.l;
	case 3: *p++ = u.l; *p++ = u.l;
	case 2: *p++ = u.l; *p++ = u.l;
	default: *p++ = u.l; *p = u.l;
	}
}

static void
pixel(u8int v, int p, int s)
{
	if(p > pri){
		col = v;
		pri = p;
	}
	disp |= 1<<s;
}

static void
playfield(void)
{
	int x, p;
	u8int c;
	
	x = ppux / 4;
	if(x >= 20)
		if((reg[CTRLPF] & 1) != 0)
			x = 39 - x;
		else
			x = x - 20;
	if(x < 4){
		if((reg[PF0] & 0x10<<x) == 0)
			return;
	}else if(x < 12){
		if((reg[PF1] & 0x800>>x) == 0)
			return;
	}else
		if((reg[PF2] & 1<<x-12) == 0)
			return;
	if((reg[CTRLPF] & 6) == 2)
		if(ppux < 80){
			c = reg[COLUP0];
			p = 3;
		}else{
			c = reg[COLUP1];
			p = 2;
		}
	else{
		c = reg[COLUPF];
		p = (reg[CTRLPF] & 4) + 1;
	}
	pixel(c, p, SRCPF);
}

static void
player(int n)
{
	u8int c;
	int x;

	c = reg[GRP0 + n];
	x = ppux - (n ? p1x : p0x);
	if(x < 0)
		return;
	switch(reg[NUSIZ0 + n] & 7){
	default: if(x >= 8) return; break;
	case 1: if(x >= 8 && (x < 16 || x >= 24)) return; break;
	case 2: if(x >= 8 && (x < 32 || x >= 40)) return; break;
	case 3: if(x >= 40 || ((x & 15) >= 8)) return; break;
	case 4: if(x >= 8 && (x < 64 || x >= 72)) return; break;
	case 5: if(x >= 16) return; x >>= 1; break;
	case 6: if(x >= 72 || ((x & 31) >= 8)) return; break;
	case 7: if(x >= 32) return; x >>= 2; break;
	}
	x &= 7;
	if((reg[REFP0 + n] & 8) == 0)
		x ^= 7;
	if((c & 1<<x) == 0)
		return;
	c = reg[COLUP0 + n];
	pixel(c, 3 - n, SRCP0 + n);
}

static void
missile(int n)
{
	int x;

	x = ppux - (n ? m1x : m0x);
	if((reg[RESMP0 + n] & 2) != 0){
		if(n)
			m1x = p1x;
		else
			m0x = p0x;
		return;
	}
	if(x < 0 || x >= 1<<(reg[NUSIZ0] >> 4 & 3) || (reg[ENAM0 + n] & 2) == 0)
		return;
	pixel(reg[COLUP0 + n], 3 - n, SRCM0 + n);
}

static void
ball(void)
{
	int x;

	x = ppux - blx;
	if(x < 0 || x >= 1<<(reg[CTRLPF] >> 4 & 3) || (reg[ENABL] & 2) == 0)
		return;
	pixel(reg[COLUPF], (reg[CTRLPF] & 4) + 1, SRCBL);
}

void
tiastep(void)
{
	static u16int colltab[64] = {
		0x0000, 0x0000, 0x0000, 0x0020, 0x0000, 0x0080, 0x8000, 0x80a0,
		0x0000, 0x0200, 0x0001, 0x0221, 0x0002, 0x0282, 0x8003, 0x82a3,
		0x0000, 0x0800, 0x0008, 0x0828, 0x0004, 0x0884, 0x800c, 0x88ac,
		0x4000, 0x4a00, 0x4009, 0x4a29, 0x4006, 0x4a86, 0xc00f, 0xcaaf,
		0x0000, 0x2000, 0x0010, 0x2030, 0x0040, 0x20c0, 0x8050, 0xa0f0,
		0x0100, 0x2300, 0x0111, 0x2331, 0x0142, 0x23c2, 0x8153, 0xa3f3,
		0x0400, 0x2c00, 0x0418, 0x2c38, 0x0444, 0x2cc4, 0x845c, 0xacfc,
		0x4500, 0x6f00, 0x4519, 0x6f39, 0x4546, 0x6fc6, 0xc55f, 0xefff,
	};

	if(ppuy < PICH && ppux < 160){
		col = reg[COLUBK];
		pri = 0;
		disp = 0;
		playfield();
		player(0);
		player(1);
		missile(0);
		missile(1);
		ball();
		coll |= colltab[disp];
		pixeldraw(col);
	}
	if(ppux == 160)
		nrdy = 0;
	if(++ppux == 228){
		ppuy++;
		ppux = 0;
	}
}
