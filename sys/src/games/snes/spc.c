#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

u8int sA, sX, sY, sP, sS;
u16int spc, scurpc;
u8int spcmem[65536];
u8int spctimer[4];
static u8int ipl[64];

enum {
	SPCN = 1<<7,
	SPCV = 1<<6,
	SPCP = 1<<5,
	SPCB = 1<<4,
	SPCH = 1<<3,
	SPCI = 1<<2,
	SPCZ = 1<<1,
	SPCC = 1<<0,
};

static u8int
spcread(u16int p)
{
	u8int v;

	if(p >= 0xffc0 && (spcmem[0xf1] & 0x80) != 0)
		return ipl[p - 0xffc0];
	if((p & 0xfff0) == 0x00f0)
		switch(p){
		case 0xf3:
			return dspread(spcmem[0xf2]);
		case 0xf4:
		case 0xf5:
		case 0xf6:
		case 0xf7:
			return reg[0x2140 | p & 3];
		case 0xfa:
		case 0xfb:
		case 0xfc:
			return 0;
		case 0xfd:
		case 0xfe:
		case 0xff:
			v = spcmem[p];
			spcmem[p] = 0;
			return v;
		}
	return spcmem[p];
}

static void
spcwrite(u16int p, u8int v)
{
	if((p & 0xfff0) == 0x00f0)
		switch(p){
		case 0xf0:
			if(v != 0x0a)
				print("SPC test register set to value %#x != 0xa\n", v);
			return;
		case 0xf1:
			if((v & 0x10) != 0)
				reg[0x2140] = reg[0x2141] = 0;
			if((v & 0x20) != 0)
				reg[0x2142] = reg[0x2143] = 0;
			if((spcmem[0xf1] & 1) == 0 && (v & 1) != 0)
				spctimer[0] = spcmem[0xfd] = 0;
			if((spcmem[0xf1] & 2) == 0 && (v & 2) != 0)
				spctimer[1] = spcmem[0xfe] = 0;
			if((spcmem[0xf1] & 4) == 0 && (v & 4) != 0)
				spctimer[2] = spcmem[0xff] = 0;
			break;
		case 0xf3:
			dspwrite(spcmem[0xf2], v);
			return;
		case 0xfd:
		case 0xfe:
		case 0xff:
			return;
		}
	spcmem[p] = v;
}

void
spctimerstep(void)
{
	u8int m;
	
	m = spcmem[0xf1];
	if(spctimer[3] == 7){
		spctimer[3] = 0;
		if((m & 1) != 0 && ++spctimer[0] == spcmem[0xfa]){
			spctimer[0] = 0;
			spcmem[0xfd] = (spcmem[0xfd] + 1) & 0xf;
		}
		if((m & 2) != 0 && ++spctimer[1] == spcmem[0xfb]){
			spctimer[1] = 0;
			spcmem[0xfe] = (spcmem[0xfe] + 1) & 0xf;
		}
	}else
		spctimer[3]++;
	if((m & 4) != 0 && ++spctimer[2] == spcmem[0xfc]){
		spctimer[2] = 0;
		spcmem[0xff] = (spcmem[0xff] + 1) & 0xf;
	}
}

static u8int
fetch8(void)
{
	return spcread(spc++);
}

static u16int
fetch16(void)
{
	int a;
	
	a = fetch8();
	a |= fetch8() << 8;
	return a;
}

static u16int
mem16(u16int p)
{
	int a;

	a = spcread(p++);
	a |= spcread(p) << 8;
	return a;
}

static u16int
memd16(u16int p)
{
	int a;
	
	a = spcread(p);
	if((p & 0xff) == 0xff)
		p &= ~0xff;
	else
		p++;
	a |= spcread(p) << 8;
	return a;
}

static void
push8(u8int v)
{
	spcwrite(0x100 | sS--, v);
}

static void
push16(u16int v)
{
	spcwrite(0x100 | sS--, v>>8);
	spcwrite(0x100 | sS--, v);
}

static u8int
pop8(void)
{
	return spcread(0x100 | ++sS);
}

static u16int
pop16(void)
{
	u16int v;
	
	v = spcread(0x100 | ++sS);
	v |= spcread(0x100 | ++sS) << 8;
	return v;
}

#define imm() fetch8()
#define dp ((sP&SPCP)<<3)
#define azp() (fetch8()|dp)
#define azpX() ((u8int)(fetch8()+sX)|dp)
#define azpY() ((u8int)(fetch8()+sY)|dp)
#define zp() spcread(azp())
#define zpX() spcread(azpX())
#define zpY() spcread(azpY())
#define abs() spcread(fetch16())
#define absX() spcread(fetch16()+sX)
#define absY() spcread(fetch16()+sY)
#define indX() spcread(aindX())
#define indY() spcread(aindY())

static u16int
aindX(void)
{
	u8int r;
	u16int a;
	
	r = fetch8() + sX;
	a = spcread(r++ | dp);
	a |= spcread(r | dp) << 8;
	return a;
}

static u16int
aindY(void)
{
	u8int r;
	u16int a;
	
	r = fetch8();
	a = spcread(r++ | dp) + sY;
	a += spcread(r | dp) << 8;
	return a;
}

static u8int
nz(u8int v)
{
	sP &= ~(SPCN|SPCZ);
	sP |= v & 0x80;
	if(v == 0)
		sP |= SPCZ;
	return v;
}

static void
nz16(void)
{
	sP &= ~(SPCN|SPCZ);
	if(sA == 0 && sY == 0)
		sP |= SPCZ;
	if(sY & 0x80)
		sP |= SPCN;
}

static int
branch(int c, int n)
{
	static char s;
	u16int npc;
	
	if(!c){
		spc++;
		return n;
	}
	s = fetch8();
	npc = spc + s;
	if(((npc ^ spc) & 0xff00) != 0)
		n++;
	spc = npc;
	return ++n;	
}

static void
clrb(u16int a, int b)
{
	spcwrite(a, spcread(a) & ~(1<<b));
}

static void
cmp(u8int a, u8int b)
{
	sP &= ~(SPCZ|SPCN|SPCC);
	if(a >= b)
		sP |= SPCC;
	if(a == b)
		sP |= SPCZ;
	if((a - b) & 0x80)
		sP |= SPCN;
}

static u8int
adc(u8int a, u8int b)
{
	u16int r;
	u8int r8;
	
	r8 = r = a + b + (sP & SPCC);
	sP &= ~(SPCC|SPCZ|SPCH|SPCV|SPCN);
	if(r >= 0x100)
		sP |= SPCC;
	sP |= r8 & SPCN;
	if((a ^ b ^ r) & 0x10)
		sP |= SPCH;
	if((~(a ^ b) & (a ^ r)) & 0x80)
		sP |= SPCV;
	if(r8 == 0)
		sP |= SPCZ;
	return r8;
}

static void
inc(u16int a)
{
	spcwrite(a, nz(spcread(a) + 1));
}

static void
jsr(u16int a)
{
	push16(spc);
	spc = a;
}
static void
asl(u16int a)
{
	u8int v;
	
	v = spcread(a);
	sP &= ~SPCC;
	sP |= v >> 7 & 1;
	spcwrite(a, nz(v << 1));
}

static void
lsr(u16int a)
{
	u8int v;
	
	v = spcread(a);
	sP &= ~SPCC;
	sP |= v & 1;
	spcwrite(a, nz(v >> 1));
}

static void
rol(u16int a)
{
	u8int v, c;
	
	v = spcread(a);
	c = sP & SPCC;
	sP &= ~SPCC;
	sP |= v >> 7 & 1;
	v = v<<1 | c;
	spcwrite(a, nz(v));
}

static void
inc16(u16int a, int c)
{
	u16int v;

	v = memd16(a) + c;
	sP &= ~(SPCN|SPCZ);
	if(v == 0)
		sP |= SPCZ;
	if((v & 0x8000) != 0)
		sP |= SPCN;
	spcwrite(a, v);
	spcwrite(a+1, v>>8);
}

static void
ror(u16int a)
{
	u8int v, c;
	
	v = spcread(a);
	c = sP & SPCC;
	sP &= ~SPCC;
	sP |= v & 1;
	v = v>>1 | c<<7;
	spcwrite(a, nz(v));
}

static u8int
sbc(u8int a, u8int b)
{
	return adc(a, ~b);
}

static void
setb(u16int a, int b)
{
	spcwrite(a, spcread(a) | (1<<b));
}

static void
setnbit(u16int a, int c)
{
	u8int v, b;
	
	b = a >> 13;
	v = spcread(a & 0x1fff) & ~(1<<b);
	if(c)
		v |= (1<<b);
	spcwrite(a & 0x1fff, v);
}

static void
tset(u16int a, int set)
{
	u8int v;
	
	v = spcread(a);
	nz(sA - v);
	if(set)
		v |= sA;
	else
		v &= ~sA;
	spcwrite(a, v);
}

static void
div(void)
{
	u32int v, x;
	int i;
	
	sP &= ~(SPCH|SPCV);
	if((sX & 0xf) <= (sY & 0xf))
		sP |= SPCH;
	v = sA | sY << 8;
	x = sX << 9;
	for(i = 0; i < 9; i++){
		v = (v << 1 | v >> 16) & 0x1ffff;
		if(v >= x)
			v ^= 1;
		if((v & 1) != 0)
			v = (v - x) & 0x1ffff;
	}
	nz(sA = v);
	sY = v >> 9;
	if((v & 0x100) != 0)
		sP |= SPCV;
}

void
spcreset(void)
{
	spcmem[0xf0] = 0x0a;
	spcmem[0xf1] = 0xb0;
	spc = spcread(0xfffe) | spcread(0xffff) << 8;
}

int
spcstep(void)
{
	static int ctr;
	u8int op, a;
	u16int b, c;

	scurpc = spc;
	op = fetch8();
	if(trace)
		print("SPC %.4x %.2x A=%.2x X=%.2x Y=%.2x P=%.2x S=%.2x\n", spc-1, op, sA, sX, sY, sP, sS);
	switch(op){
	case 0x00: return 2;
	case 0x01: jsr(mem16(0xffde)); return 8;
	case 0x02: setb(azp(), 0); return 4;
	case 0x03: return branch((zp() & 0x01) != 0, 5);
	case 0x04: nz(sA |= zp()); return 3;
	case 0x05: nz(sA |= abs()); return 4;
	case 0x06: nz(sA |= spcread(sX|dp)); return 3;
	case 0x07: nz(sA |= indX()); return 6;
	case 0x08: nz(sA |= imm()); return 2;
	case 0x09: b = zp(); c = azp(); spcwrite(c, nz(b | spcread(c))); return 6;
	case 0x0A: b = fetch16(); sP |= (spcread(b & 0x1fff) >> (b >> 13)) & 1;  return 4;
	case 0x0B: asl(azp()); return 5;
	case 0x0C: asl(fetch16()); return 5;
	case 0x0D: push8(sP); return 4;
	case 0x0E: tset(fetch16(), 1); return 6;
	case 0x10: return branch((sP & SPCN) == 0, 2);
	case 0x11: jsr(mem16(0xffdc)); return 8;
	case 0x12: clrb(azp(), 0); return 4;
	case 0x13: return branch((zp() & 0x01) == 0, 5);
	case 0x14: nz(sA |= zpX()); return 4;
	case 0x15: nz(sA |= absX()); return 5;
	case 0x16: nz(sA |= absY()); return 5;
	case 0x17: nz(sA |= indY()); return 6;
	case 0x18: a = imm(); b = azp(); spcwrite(b, nz(spcread(b) | a)); return 5;
	case 0x19: spcwrite(sX|dp, nz(spcread(sX|dp) | spcread(sY|dp))); return 5;
	case 0x1A: inc16(azp(), -1); return 6;
	case 0x1B: asl(azpX()); return 5;
	case 0x1C: sP &= ~SPCC; sP |= sA >> 7; nz(sA <<= 1); return 2;
	case 0x1D: nz(--sX); return 2;
	case 0x1E: cmp(sX, abs()); return 4;
	case 0x1F: spc = mem16(fetch16() + sX); return 6;
	case 0x20: sP &= ~SPCP; return 2;
	case 0x21: jsr(mem16(0xffda)); return 8;
	case 0x22: setb(azp(), 1); return 4;
	case 0x23: return branch((zp() & 0x02) != 0, 5);
	case 0x24: nz(sA &= zp()); return 3;
	case 0x25: nz(sA &= abs()); return 4;
	case 0x26: nz(sA &= spcread(sX|dp)); return 3;
	case 0x27: nz(sA &= indX()); return 6;
	case 0x28: nz(sA &= imm()); return 2;
	case 0x29: b = zp(); c = azp(); spcwrite(c, nz(b & spcread(c))); return 6;
	case 0x2A: b = fetch16(); sP |= (~spcread(b & 0x1fff) >> (b >> 13)) & 1;  return 4;
	case 0x2B: rol(azp()); return 4;
	case 0x2C: rol(fetch16()); return 5;
	case 0x2D: push8(sA); return 4;
	case 0x2E: return branch(sA != zp(), 5);
	case 0x2F: return branch(1, 2);
	case 0x30: return branch((sP & SPCN) != 0, 2);
	case 0x31: jsr(mem16(0xffd8)); return 8;
	case 0x32: clrb(azp(), 1); return 4;
	case 0x33: return branch((zp() & 0x02) == 0, 5);
	case 0x34: nz(sA &= zpX()); return 4;
	case 0x35: nz(sA &= absX()); return 5;
	case 0x36: nz(sA &= absY()); return 5;
	case 0x37: nz(sA &= indY()); return 6;
	case 0x38: a = imm(); b = azp(); spcwrite(b, nz(spcread(b) & a)); return 5;
	case 0x39: spcwrite(sX|dp, nz(spcread(sX|dp) & spcread(sY|dp))); return 5;
	case 0x3A: inc16(azp(), 1); return 6;
	case 0x3B: rol(azpX()); return 5;
	case 0x3C:
		a = sP & SPCC;
		sP &= ~SPCC;
		sP |= sA >> 7 & 1;
		sA = sA << 1 | a;
		nz(sA);
		return 2;
	case 0x3D: nz(++sX); return 2;
	case 0x3E: cmp(sX, zp()); return 3;
	case 0x3F: jsr(fetch16()); return 8;
	case 0x40: sP |= SPCP; return 2;
	case 0x41: jsr(mem16(0xffd6)); return 8;
	case 0x42: setb(azp(), 2); return 4;
	case 0x43: return branch((zp() & 0x04) != 0, 5);
	case 0x44: nz(sA ^= zp()); return 3;
	case 0x45: nz(sA ^= abs()); return 4;
	case 0x46: nz(sA ^= spcread(sX|dp)); return 3;
	case 0x47: nz(sA ^= indX()); return 6;
	case 0x48: nz(sA ^= imm()); return 2;
	case 0x49: b = zp(); c = azp(); spcwrite(c, nz(b ^ spcread(c))); return 6;
	case 0x4A: b = fetch16(); sP &= 0xfe | (spcread(b & 0x1fff) >> (b >> 13)) & 1;  return 4;
	case 0x4B: lsr(azp()); return 4;
	case 0x4C: lsr(fetch16()); return 5;
	case 0x4D: push8(sX); return 4;
	case 0x4E: tset(fetch16(), 0); return 5;
	case 0x4F: jsr(0xff00 | fetch8()); return 6;
	case 0x50: return branch((sP & SPCV) == 0, 2);
	case 0x51: jsr(mem16(0xffd4)); return 8;
	case 0x52: clrb(azp(), 2); return 4;
	case 0x53: return branch((zp() & 0x04) == 0, 5);
	case 0x54: nz(sA ^= zpX()); return 4;
	case 0x55: nz(sA ^= absX()); return 5;
	case 0x56: nz(sA ^= absY()); return 5;
	case 0x57: nz(sA ^= indY()); return 6;
	case 0x58: a = imm(); b = azp(); spcwrite(b, nz(spcread(b) ^ a)); return 5;
	case 0x59: spcwrite(sX|dp, nz(spcread(sX|dp) ^ spcread(sY|dp))); return 5;
	case 0x5A: 
		b = sA | sY << 8;
		c = memd16(azp());
		sP &= ~(SPCN|SPCZ|SPCC);
		if(b >= c)
			sP |= SPCC;
		if(b == c)
			sP |= SPCZ;
		if(((b - c) & 0x8000) != 0)
			sP |= SPCN;
		return 4;
	case 0x5B: lsr(azpX()); return 4;
	case 0x5C: sP &= ~SPCC; sP |= sA & 1; nz(sA >>= 1); return 2;
	case 0x5D: nz(sX = sA); return 2;
	case 0x5E: cmp(sY, abs()); return 4;
	case 0x5F: spc = fetch16(); return 3;
	case 0x60: sP &= ~SPCC; return 2;
	case 0x61: jsr(mem16(0xffd2)); return 8;
	case 0x62: setb(azp(), 3); return 4;
	case 0x63: return branch((zp() & 0x08) != 0, 5);
	case 0x64: cmp(sA, zp()); return 3;
	case 0x65: cmp(sA, abs()); return 4;
	case 0x66: cmp(sA, spcread(sX|dp)); return 3;
	case 0x67: cmp(sA, indX()); return 6;
	case 0x68: cmp(sA, imm()); return 2;
	case 0x69: a = zp(); cmp(zp(), a); return 6;
	case 0x6A: b = fetch16(); sP &= ~((spcread(b & 0x1fff) >> (b >> 13)) & 1);  return 4;
	case 0x6B: ror(azp()); return 4;
	case 0x6C: ror(fetch16()); return 5;
	case 0x6D: push8(sY); return 4;
	case 0x6E: b = azp(); a = spcread(b)-1; spcwrite(b, a); return branch(a != 0, 5);
	case 0x6F: spc = pop16(); return 5;
	case 0x70: return branch((sP & SPCV) != 0, 2);
	case 0x72: clrb(azp(), 3); return 4;
	case 0x71: jsr(mem16(0xffd0)); return 8;
	case 0x73: return branch((zp() & 0x08) == 0, 5);
	case 0x74: cmp(sA, zpX()); return 4;
	case 0x75: cmp(sA, absX()); return 5;
	case 0x76: cmp(sA, absY()); return 5;
	case 0x77: cmp(sA, indY()); return 6;
	case 0x78: a = imm(); cmp(zp(), a); return 5;
	case 0x79: cmp(spcread(sX|dp), spcread(sY|dp)); return 5;
	case 0x7A:
		b = memd16(azp());
		sP &= ~SPCC;
		sA = adc(sA, b);
		sY = adc(sY, b >> 8);
		if(sA != 0)
			sP &= ~SPCZ;
		return 5;
	case 0x7B: ror(azpX()); return 5;
	case 0x7C:
		a = sP & SPCC;
		sP &= ~SPCC;
		sP |= sA & 1;
		sA = sA >> 1 | a << 7;
		nz(sA);
		return 2;
	case 0x7D: nz(sA = sX); return 2;
	case 0x7E: cmp(sY, zp()); return 3;
	case 0x7F: sP = pop8(); spc = pop16(); return 6;
	case 0x80: sP |= SPCC; return 2;
	case 0x81: jsr(mem16(0xffce)); return 8;
	case 0x82: setb(azp(), 4); return 4;
	case 0x83: return branch((zp() & 0x10) != 0, 5);
	case 0x84: sA = adc(sA, zp()); return 3;
	case 0x85: sA = adc(sA, abs()); return 4;
	case 0x86: sA = adc(sA, spcread(sX|dp)); return 3;
	case 0x87: sA = adc(sA, indX()); return 6;
	case 0x88: sA = adc(sA, imm()); return 2;
	case 0x89: b = zp(); c = azp(); spcwrite(c, adc(b, spcread(c))); return 6;
	case 0x8A: b = fetch16(); sP ^= (spcread(b & 0x1fff) >> (b >> 13)) & 1;  return 4;
	case 0x8B: b = azp(); spcwrite(b, nz(spcread(b)-1)); return 4;
	case 0x8C: b = fetch16(); spcwrite(b, nz(spcread(b)-1)); return 4;
	case 0x8D: nz(sY = imm()); return 2;
	case 0x8E: sP = pop8(); return 2;
	case 0x8F: a = fetch8(); spcwrite(azp(), a); return 5;
	case 0x90: return branch((sP & SPCC) == 0, 2);
	case 0x91: jsr(mem16(0xffcc)); return 8;
	case 0x92: clrb(azp(), 4); return 4;
	case 0x93: return branch((zp() & 0x10) == 0, 5);
	case 0x94: sA = adc(sA, zpX()); return 4;
	case 0x95: sA = adc(sA, absX()); return 5;
	case 0x96: sA = adc(sA, absY()); return 5;
	case 0x97: sA = adc(sA, indY()); return 6;
	case 0x98: a = imm(); b = azp(); spcwrite(b, adc(spcread(b), a)); return 5;
	case 0x99: spcwrite(sX|dp, adc(spcread(sX|dp), spcread(sY|dp))); return 5;
	case 0x9A:
		b = memd16(azp());
		sP |= SPCC;
		sA = sbc(sA, b);
		sY = sbc(sY, b >> 8);
		if(sA != 0)
			sP &= ~SPCZ;
		return 5;
	case 0x9B: b = azpX(); spcwrite(b, nz(spcread(b)-1)); return 4;
	case 0x9C: nz(--sA); return 2;
	case 0x9D: nz(sX = sS); return 2;
	case 0x9E: div(); return 12;
	case 0x9F: nz(sA = sA >> 4 | sA << 4); return 5;
	case 0xA0: sP |= SPCI; return 2;
	case 0xA1: jsr(mem16(0xffca)); return 8;
	case 0xA2: setb(azp(), 5); return 4;
	case 0xA3: return branch((zp() & 0x20) != 0, 5);
	case 0xA4: sA = sbc(sA, zp()); return 3;
	case 0xA5: sA = sbc(sA, abs()); return 4;
	case 0xA6: sA = sbc(sA, spcread(sX|dp)); return 3;
	case 0xA7: sA = sbc(sA, indX()); return 6;
	case 0xA8: sA = sbc(sA, imm()); return 2;
	case 0xA9: b = zp(); c = azp(); spcwrite(c, sbc(spcread(c), b)); return 6;
	case 0xAA: b = fetch16(); sP &= ~1; sP |= (spcread(b & 0x1fff) >> (b >> 13)) & 1;  return 4;
	case 0xAB: inc(azp()); return 4;
	case 0xAC: inc(fetch16()); return 5;
	case 0xAD: cmp(sY, imm()); return 2;
	case 0xAE: sA = pop8(); return 2;
	case 0xAF: spcwrite(sX++|dp, sA); return 4;
	case 0xB0: return branch((sP & SPCC) != 0, 2);
	case 0xB1: jsr(mem16(0xffc8)); return 8;
	case 0xB2: clrb(azp(), 5); return 4;
	case 0xB3: return branch((zp() & 0x20) == 0, 5);
	case 0xB4: sA = sbc(sA, zpX()); return 4;
	case 0xB5: sA = sbc(sA, absX()); return 5;
	case 0xB6: sA = sbc(sA, absY()); return 5;
	case 0xB7: sA = sbc(sA, indY()); return 6;
	case 0xB8: a = imm(); b = azp(); spcwrite(b, sbc(spcread(b), a)); return 5;
	case 0xB9: spcwrite(sX|dp, sbc(spcread(sX|dp), spcread(sY|dp))); return 5;
	case 0xBA: a = fetch8(); sA = spcread(a++|dp); sY = spcread(a|dp); nz16(); return 5;
	case 0xBB: inc(azpX()); return 4;
	case 0xBC: nz(++sA); return 2;
	case 0xBD: sS = sX; return 2;
	case 0xBF: nz(sA = spcread(sX++|dp)); return 3;
	case 0xC0: sP &= ~SPCI; return 2;
	case 0xC1: jsr(mem16(0xffc6)); return 8;
	case 0xC2: setb(azp(), 6); return 4;
	case 0xC3: return branch((zp() & 0x40) != 0, 5);
	case 0xC4: spcwrite(azp(), sA); return 4;
	case 0xC5: spcwrite(fetch16(), sA); return 5;
	case 0xC6: spcwrite(sX|dp, sA); return 4;
	case 0xC7: spcwrite(aindX(), sA); return 7;
	case 0xC8: cmp(sX, imm()); return 2;
	case 0xC9: spcwrite(fetch16(), sX); return 5;
	case 0xCA: b = fetch16(); setnbit(b, sP & SPCC); return 6;
	case 0xCB: spcwrite(azp(), sY); return 4;
	case 0xCC: spcwrite(fetch16(), sY); return 5;
	case 0xCD: nz(sX = imm()); return 2;
	case 0xCE: sX = pop8(); return 2;
	case 0xCF: b = sY * sA; nz(sY = b >> 8); sA = b; return 9;
	case 0xD0: return branch((sP & SPCZ) == 0, 2);
	case 0xD1: jsr(mem16(0xffc4)); return 8;
	case 0xD2: clrb(azp(), 6); return 4;
	case 0xD3: return branch((zp() & 0x40) == 0, 5);
	case 0xD4: spcwrite(azpX(), sA); return 4;
	case 0xD5: spcwrite(fetch16() + sX, sA); return 6;
	case 0xD6: spcwrite(fetch16() + sY, sA); return 6;
	case 0xD7: spcwrite(aindY(), sA); return 7;
	case 0xD8: spcwrite(azp(), sX); return 4;
	case 0xD9: spcwrite(azpY(), sX); return 5;
	case 0xDA: a = fetch8(); spcwrite(a++|dp, sA); spcwrite(a|dp, sY); return 5;
	case 0xDB: spcwrite(azpX(), sY); return 5;
	case 0xDC: nz(--sY); return 2;
	case 0xDD: nz(sA = sY); return 2;
	case 0xDE: return branch(sA != zpX(), 6);
	case 0xE0: sP &= ~(SPCV|SPCH); return 2;
	case 0xE1: jsr(mem16(0xffc2)); return 8;
	case 0xE2: setb(azp(), 7); return 4;
	case 0xE3: return branch((zp() & 0x80) != 0, 5);
	case 0xE4: nz(sA = zp()); return 3;
	case 0xE5: nz(sA = abs()); return 4;
	case 0xE6: nz(sA = spcread(sX|dp)); return 3;
	case 0xE7: nz(sA = indX()); return 6;
	case 0xE8: nz(sA = imm()); return 2;
	case 0xE9: nz(sX = abs()); return 4;
	case 0xEA: b = fetch16(); spcwrite(b & 0x1fff, spcread(b & 0x1fff) ^ (1<<(b>>13))); return 6;
	case 0xEB: nz(sY = zp()); return 3;
	case 0xEC: nz(sY = abs()); return 4;
	case 0xED: sP ^= SPCC; return 3;
	case 0xEE: sY = pop8(); return 4;
	case 0xF0: return branch((sP & SPCZ) != 0, 2);
	case 0xF1: jsr(mem16(0xffc0)); return 8;
	case 0xF2: clrb(azp(), 7); return 4;
	case 0xF3: return branch((zp() & 0x80) == 0, 5);
	case 0xF4: nz(sA = zpX()); return 4;
	case 0xF5: nz(sA = absX()); return 5;
	case 0xF6: nz(sA = absY()); return 5;
	case 0xF7: nz(sA = indY()); return 6;
	case 0xF8: nz(sX = zp()); return 3;
	case 0xF9: nz(sX = zpY()); return 4;
	case 0xFA: a = zp(); spcwrite(azp(), a); return 5;
	case 0xFB: nz(sY = zpX()); return 4;
	case 0xFC: nz(++sY); return 2;
	case 0xFD: nz(sY = sA); return 2;
	case 0xFE: return branch(--sY, 4);
	default:
		print("undefined spc op %.2x at %.4x\n", op, spc-1);
		return 2;
	}
}

static u8int ipl[64] = {
	0xcd, 0xef, 0xbd, 0xe8, 0x00, 0xc6, 0x1d, 0xd0, 0xfc, 0x8f, 0xaa, 0xf4, 0x8f, 0xbb, 0xf5, 0x78,
	0xcc, 0xf4, 0xd0, 0xfb, 0x2f, 0x19, 0xeb, 0xf4, 0xd0, 0xfc, 0x7e, 0xf4, 0xd0, 0x0b, 0xe4, 0xf5,
	0xcb, 0xf4, 0xd7, 0x00, 0xfc, 0xd0, 0xf3, 0xab, 0x01, 0x10, 0xef, 0x7e, 0xf4, 0x10, 0xeb, 0xba,
	0xf6, 0xda, 0x00, 0xba, 0xf4, 0xc4, 0xf4, 0xdd, 0x5d, 0xd0, 0xdb, 0x1f, 0x00, 0x00, 0xc0, 0xff,           
};
