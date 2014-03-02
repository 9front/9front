#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

u16int pc, curpc;
u8int rA, rX, rY, rS, rP;
u8int irq, nmi;

static u8int
fetch8(void)
{
	return memread(pc++);
}

static u16int
fetch16(void)
{
	u16int r;
	
	r = memread(pc++);
	r |= memread(pc++) << 8;
	return r;
}

static void
push8(u8int v)
{
	memwrite(0x100 | rS--, v);
}

static void
push16(u16int v)
{
	memwrite(0x100 | rS--, v >> 8);
	memwrite(0x100 | rS--, v);
}

static u8int
pop8(void)
{
	return memread(0x100 | ++rS);
}

static u16int
pop16(void)
{
	u16int v;
	
	v = memread(0x100 | ++rS);
	v |= memread(0x100 | ++rS) << 8;
	return v;
}

#define imm() fetch8()
#define zp() memread(fetch8())
#define zpX() memread((u8int)(fetch8()+rX))
#define zpY() memread((u8int)(fetch8()+rY))
#define abso() memread(fetch16())
#define absX() memread(a=fetch16()+rX)
#define absY() memread(a=fetch16()+rY)
#define indX() memread(aindX())
#define indY(c) memread(aindY(c))

static u16int
aindX(void)
{
	u8int r;
	u16int a;
	
	r = fetch8() + rX;
	a = memread(r++);
	a |= memread(r) << 8;
	return a;
}

static u16int
aindY(int *c)
{
	u8int r;
	u16int a;
	
	r = fetch8();
	a = memread(r++) + rY;
	*c = a > 0xFF;
	a += memread(r) << 8;
	return a;
}

static void
adc(u8int d)
{
	int r;
	
	r = rA + d + (rP & FLAGC);
	rP &= ~(FLAGN | FLAGZ | FLAGV | FLAGC);
	if(r > 0xFF) rP |= FLAGC;
	if(r & 0x80) rP |= FLAGN;
	if((~(rA ^ d) & (rA ^ r)) & 0x80) rP |= FLAGV;
	rA = r;
	if(rA == 0) rP |= FLAGZ;
}

static u8int
nz(u8int d)
{
	rP &= ~(FLAGN | FLAGZ);
	if(d & 0x80) rP |= FLAGN;
	if(d == 0) rP |= FLAGZ;
	return d;
}

static void
asl(u16int a)
{
	u8int v;

	rP &= ~(FLAGN | FLAGZ | FLAGC);
	v = memread(a);
	if(v & 0x80) rP |= FLAGC;
	v <<= 1;
	if(v == 0) rP |= FLAGZ;
	if(v & 0x80) rP |= FLAGN;
	memwrite(a, v);
}

static void
lsr(u16int a)
{
	u8int v;

	rP &= ~(FLAGN | FLAGZ | FLAGC);
	v = memread(a);
	rP |= v & 1;
	v >>= 1;
	if(v == 0) rP |= FLAGZ;
	if(v & 0x80) rP |= FLAGN;
	memwrite(a, v);
}

static int
branch(void)
{
	signed char t;
	u16int npc;
	
	t = fetch8();
	npc = pc + t;
	if((npc ^ pc) >> 8){
		pc = npc;
		return 4;
	}
	pc = npc;
	return 3;
}

static void
cmp(u8int a, u8int d)
{
	rP &= ~(FLAGN | FLAGZ | FLAGC);
	if(a == d) rP |= FLAGZ;
	if(a >= d) rP |= FLAGC;
	if((a - d) & 0x80) rP |= FLAGN;
}

static void
dec(u16int a)
{
	memwrite(a, nz(memread(a) - 1));
}

static void
inc(u16int a)
{
	u8int v;

	v = memread(a);
	memwrite(a, v);
	v = nz(v + 1);
	if(!(map == 1 && a >= 0x8000))
		memwrite(a, v);
}

static void
rol(u16int a)
{
	u8int v, b;
	
	v = memread(a);
	b = rP & FLAGC;
	rP &= ~(FLAGC | FLAGN | FLAGZ);
	if(v & 0x80) rP |= FLAGC;
	v = (v << 1) | b;
	if(v & 0x80) rP |= FLAGN;
	if(v == 0) rP |= FLAGZ;
	memwrite(a, v);
}

static void
ror(u16int a)
{
	u8int v, b;
	
	v = memread(a);
	b = rP & FLAGC;
	rP &= ~(FLAGC | FLAGN | FLAGZ);
	rP |= v & 1;
	v = (v >> 1) | (b << 7);
	if(v & 0x80) rP |= FLAGN;
	if(v == 0) rP |= FLAGZ;
	memwrite(a, v);
}

static void
sbc(u8int d)
{
	int r;
	
	r = rA + (u8int)~d + (rP & FLAGC);
	rP &= ~(FLAGZ | FLAGV | FLAGC | FLAGN);
	if(r > 0xFF) rP |= FLAGC;
	if(((rA ^ d) & (rA ^ r)) & 0x80) rP |= FLAGV;
	rA = r;
	if(rA == 0) rP |= FLAGZ;
	if(rA & 0x80) rP |= FLAGN;
}

static void
interrupt(int nmi, int brk)
{
	push16(pc);
	push8(rP | 0x20 | (brk << 4));
	pc = memread(0xFFFA | (!nmi << 2));
	pc |= memread(0xFFFB | (!nmi << 2)) << 8;
	rP |= FLAGI;
}

int trace;

int
step(void)
{
	u8int op;
	u16int a, v;
	int c;
	
	if(nmi)
		if(--nmi == 0){
			interrupt(1, 0);
			nmi = 0;
			return 7;
		}
	if(irq && (rP & 4) == 0){
		interrupt(0, 0);
		return 7;
	}
	curpc = pc;
	op = fetch8();
	if(trace)
		print("%x %x %x %x %x %x %x\n", curpc, op, rA, rX, rY, rS, rP);
	switch(op){
	case 0x00: pc++; interrupt(0, 1); return 7;
	case 0x01: nz(rA |= indX()); return 6;
	case 0x05: nz(rA |= zp()); return 3;
	case 0x06: asl(fetch8()); return 5;
	case 0x08: push8(rP | 0x30); return 3;
	case 0x09: nz(rA |= imm()); return 2;
	case 0x0A:
		rP &= ~(FLAGN | FLAGZ | FLAGC);
		if(rA & 0x80) rP |= FLAGC;
		rA <<= 1;
		if(rA == 0) rP |= FLAGZ;
		if(rA & 0x80) rP |= FLAGN;
		return 2;
	case 0x0D: nz(rA |= abso()); return 4;
	case 0x0E: asl(fetch16()); return 6;
	case 0x10: if((rP & FLAGN) == 0) return branch(); pc++; return 2;
	case 0x11: nz(rA |= indY(&c)); return 5+c;
	case 0x15: nz(rA |= zpX()); return 4;
	case 0x16: asl((u8int)(fetch8() + rX)); return 6;
	case 0x18: rP &= ~FLAGC; return 2;
	case 0x19: nz(rA |= absY()); return 4 + ((u8int)a < rY);
	case 0x1D: nz(rA |= absX()); return 4 + ((u8int)a < rX);
	case 0x1E: asl(fetch16() + rX); return 7;
	case 0x20: push16(pc+1); pc = fetch16(); return 6;
	case 0x21: nz(rA &= indX()); return 6;
	case 0x24:
		a = memread(fetch8());
		rP &= ~(FLAGN | FLAGZ | FLAGV);
		rP |= a & 0xC0;
		if((a & rA) == 0) rP |= FLAGZ;
		return 3;
	case 0x25: nz(rA &= zp()); return 3;
	case 0x26: rol(fetch8()); return 5;
	case 0x28: rP = pop8() & 0xcf; return 4;
	case 0x29: nz(rA &= imm()); return 2;
	case 0x2A:
		a = rP & FLAGC;
		rP &= ~(FLAGC | FLAGZ | FLAGN);
		if(rA & 0x80) rP |= FLAGC;
		rA = (rA << 1) | a;
		if(rA & 0x80) rP |= FLAGN;
		if(rA == 0) rP |= FLAGZ;
		return 2;
	case 0x2C:
		a = memread(fetch16());
		rP &= ~(FLAGN | FLAGZ | FLAGV);
		rP |= a & 0xC0;
		if((a & rA) == 0) rP |= FLAGZ;
		return 4;
	case 0x2D: nz(rA &= abso()); return 4;
	case 0x2E: rol(fetch16()); return 6;
	case 0x30: if((rP & FLAGN) != 0) return branch(); pc++; return 3;
	case 0x31: nz(rA &= indY(&c)); return 5+c;
	case 0x35: nz(rA &= zpX()); return 4;
	case 0x36: rol((u8int)(fetch8() + rX)); return 6;
	case 0x38: rP |= FLAGC; return 2;
	case 0x39: nz(rA &= absY()); return 4 + ((u8int)a < rY);
	case 0x3E: rol(fetch16() + rX); return 7;
	case 0x3D: nz(rA &= absX()); return 4 + ((u8int)a < rX);
	case 0x40: rP = pop8() & 0xcf; pc = pop16(); return 6;
	case 0x41: nz(rA ^= indX()); return 6;
	case 0x45: nz(rA ^= zp()); return 3;
	case 0x46: lsr(fetch8()); return 5;
	case 0x48: push8(rA); return 3;
	case 0x49: nz(rA ^= imm()); return 2;
	case 0x4A:
		rP &= ~(FLAGN | FLAGZ | FLAGC);
		rP |= rA & 1;
		rA >>= 1;
		if(rA == 0) rP |= FLAGZ;
		if(rA & 0x80) rP |= FLAGN;
		return 2;
	case 0x4C: pc = fetch16(); return 3;
	case 0x4D: nz(rA ^= abso()); return 4;
	case 0x4E: lsr(fetch16()); return 6;
	case 0x51: nz(rA ^= indY(&c)); return 5+c;
	case 0x56: lsr((u8int)(fetch8() + rX)); return 6;
	case 0x58: rP &= ~FLAGI; return 2;
	case 0x50: if((rP & FLAGV) == 0) return branch(); pc++; return 3;
	case 0x55: nz(rA ^= zpX()); return 4;
	case 0x59: nz(rA ^= absY()); return 4 + ((u8int)a < rX);
	case 0x5D: nz(rA ^= absX()); return 4 + ((u8int)a < rX);
	case 0x5E: lsr(fetch16() + rX); return 7;
	case 0x60: pc = pop16() + 1; return 6;
	case 0x61: adc(indX()); return 6;
	case 0x65: adc(zp()); return 3;
	case 0x66: ror(fetch8()); return 5;
	case 0x68: nz(rA = pop8()); return 4;
	case 0x69: adc(imm()); return 2;
	case 0x6A:
		a = rP & FLAGC;
		rP &= ~(FLAGC | FLAGN | FLAGZ);
		rP |= rA & 1;
		rA = (rA >> 1) | (a << 7);
		if(rA & 0x80) rP |= FLAGN;
		if(rA == 0) rP |= FLAGZ;
		return 2;
	case 0x6C: v = fetch16(); pc = memread(v) | (memread((v & 0xFF00) | (u8int)(v+1)) << 8); return 5;
	case 0x6D: adc(abso()); return 4;
	case 0x6E: ror(fetch16()); return 6;
	case 0x70: if((rP & FLAGV) != 0) return branch(); pc++; return 3;
	case 0x71: adc(indY(&c)); return 5+c;
	case 0x75: adc(zpX()); return 4;
	case 0x76: ror((u8int)(fetch8() + rX)); return 6;
	case 0x78: rP |= FLAGI; return 2;
	case 0x79: adc(absY()); return 4 + ((u8int)a < rY);
	case 0x7D: adc(absX()); return 4 + ((u8int)a < rX);
	case 0x7E: ror(fetch16() + rX); return 7;
	case 0x81: memwrite(aindX(), rA); return 6;
	case 0x84: memwrite(fetch8(), rY); return 3;
	case 0x85: memwrite(fetch8(), rA); return 3;
	case 0x86: memwrite(fetch8(), rX); return 3;
	case 0x88: nz(--rY); return 2;
	case 0x8A: nz(rA = rX); return 2;
	case 0x8C: memwrite(fetch16(), rY); return 4;
	case 0x8D: memwrite(fetch16(), rA); return 4;
	case 0x8E: memwrite(fetch16(), rX); return 4;
	case 0x90: if((rP & FLAGC) == 0) return branch(); pc++; return 3;
	case 0x91: memwrite(aindY(&c), rA); return 6;
	case 0x94: memwrite((u8int)(fetch8() + rX), rY); return 4;
	case 0x95: memwrite((u8int)(fetch8() + rX), rA); return 4;
	case 0x96: memwrite((u8int)(fetch8() + rY), rX); return 4;
	case 0x98: nz(rA = rY); return 2;
	case 0x99: memwrite(fetch16() + rY, rA); return 5;
	case 0x9A: rS = rX; return 2;
	case 0x9D: memwrite(fetch16() + rX, rA); return 5;
	case 0xA0: nz(rY = imm()); return 2;
	case 0xA1: nz(rA = indX()); return 6;
	case 0xA2: nz(rX = imm()); return 2;
	case 0xA4: nz(rY = zp()); return 3;
	case 0xA5: nz(rA = zp()); return 3;
	case 0xA6: nz(rX = zp()); return 3;
	case 0xA8: nz(rY = rA); return 2;
	case 0xA9: nz(rA = imm()); return 2;
	case 0xAA: nz(rX = rA); return 2;
	case 0xAC: nz(rY = abso()); return 4;
	case 0xAE: nz(rX = abso()); return 4;
	case 0xAD: nz(rA = abso()); return 4;
	case 0xB0: if((rP & FLAGC) != 0) return branch(); pc++; return 3;
	case 0xB1: nz(rA = indY(&c)); return 5+c;
	case 0xB4: nz(rY = zpX()); return 4;
	case 0xB5: nz(rA = zpX()); return 4;
	case 0xB6: nz(rX = zpY()); return 4;
	case 0xB8: rP &= ~FLAGV; return 2;
	case 0xB9: nz(rA = absY()); return 4 + ((u8int)a < rY);
	case 0xBA: nz(rX = rS); return 2;
	case 0xBC: nz(rY = absX()); return 4 + ((u8int)a < rX);
	case 0xBD: nz(rA = absX()); return 4 + ((u8int)a < rX);
	case 0xBE: nz(rX = absY()); return 4 + ((u8int)a < rY);
	case 0xC1: cmp(rA, indX()); return 6;
	case 0xC5: cmp(rA, zp()); return 3;
	case 0xC9: cmp(rA, imm()); return 2;
	case 0xCD: cmp(rA, abso()); return 4;
	case 0xD0: if((rP & FLAGZ) == 0) return branch(); pc++; return 3;
	case 0xD1: cmp(rA, indY(&c)); return 5 + c;
	case 0xD5: cmp(rA, zpX()); return 4;
	case 0xD8: rP &= ~FLAGD; return 2;
	case 0xD9: cmp(rA, absY()); return 4 + ((u8int)a < rY);
	case 0xDD: cmp(rA, absX()); return 4 + ((u8int)a < rX);
	case 0xC0: cmp(rY, imm()); return 2;
	case 0xC4: cmp(rY, zp()); return 3;
	case 0xC6: dec(fetch8()); return 5;
	case 0xC8: nz(++rY); return 2;
	case 0xCA: nz(--rX); return 2;
	case 0xCC: cmp(rY, abso()); return 4;
	case 0xCE: dec(fetch16()); return 6;
	case 0xD6: dec((u8int)(fetch8() + rX)); return 6;
	case 0xDE: dec(fetch16() + rX); return 7;
	case 0xE0: cmp(rX, imm()); return 2;
	case 0xE1: sbc(indX()); return 6;
	case 0xE4: cmp(rX, zp()); return 3;
	case 0xE5: sbc(zp()); return 3;
	case 0xE6: inc(fetch8()); return 5;
	case 0xE8: nz(++rX); return 2;
	case 0xE9: sbc(imm()); return 2;
	case 0xEA: return 2;
	case 0xEC: cmp(rX, abso()); return 4;
	case 0xED: sbc(abso()); return 4;
	case 0xEE: inc(fetch16()); return 6;
	case 0xF0: if((rP & FLAGZ) != 0) return branch(); pc++; return 3;
	case 0xF1: sbc(indY(&c)); return 5+c;
	case 0xF5: sbc(zpX()); return 4;
	case 0xF6: inc((u8int)(fetch8() + rX)); return 6;
	case 0xF8: rP |= FLAGD; return 2;
	case 0xF9: sbc(absY()); return 4 + ((u8int)a < rY);
	case 0xFD: sbc(absX()); return 4 + ((u8int)a < rX);
	case 0xFE: inc(fetch16() + rX); return 7;
	default:
		print("undefined %#x (pc %#x)\n", op, curpc);
		return 2;
	}
}
