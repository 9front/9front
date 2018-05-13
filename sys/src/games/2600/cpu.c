#include <u.h>
#include <libc.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

u16int pc, curpc;
u8int rA, rX, rY, rS, rP;
int nrdy;

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
#define zpX() memread(azpX(rX))
#define zpY() memread(azpX(rY))
#define abso() memread(fetch16())
#define absX() memread(aabsX(rX, 0))
#define absY() memread(aabsX(rY, 0))
#define indX() memread(aindX())
#define indY() memread(aindY(0))

static u16int
azpX(u8int a)
{
	u8int v;
	
	v = fetch8();
	memread(v);
	return v + a;
}

static u16int
aabsX(u8int a, int wr)
{
	u16int v, c;
	
	v = fetch16();
	c = (u8int)v + a & 0x100;
	v += a;
	if(c != 0 || wr)
		memread(v - c);
	return v;
}

static u16int
aindX(void)
{
	u8int r;
	u16int a;
	
	r = fetch8();
	memread(r);
	r += rX;
	a = memread(r++);
	a |= memread(r) << 8;
	return a;
}

static u16int
aindY(int wr)
{
	u8int r;
	u16int a, c;
	
	r = fetch8();
	a = memread(r++) + rY;
	c = a & 0x100;
	a += memread(r) << 8;
	if(c != 0 || wr)
		memread(a - c);
	return a;
}

static void
adc(u8int d)
{
	int r;
	
	if((rP & FLAGD) != 0){
		r = (rA & 0xf) + (d & 0xf) + (rP & FLAGC);
		if(r > 0x09)
			r += 0x06;
		if(r > 0x1f)
			r -= 0x10;
		r += (rA & 0xf0) + (d & 0xf0);
	}else
		r = rA + d + (rP & FLAGC);
	rP &= ~(FLAGN | FLAGZ | FLAGV | FLAGC);
	if((~(rA ^ d) & (rA ^ r)) & 0x80) rP |= FLAGV;
	if((rP & FLAGD) != 0 && r > 0x9f)
		r += 0x60;
	if(r > 0xFF) rP |= FLAGC;
	if(r & 0x80) rP |= FLAGN;
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
	memwrite(a, v);
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
	memwrite(a, v);
	rP |= v & 1;
	v >>= 1;
	if(v == 0) rP |= FLAGZ;
	if(v & 0x80) rP |= FLAGN;
	memwrite(a, v);
}

static void
branch(void)
{
	s8int t;
	u16int npc;
	
	t = fetch8();
	memread(pc);
	npc = pc + t;
	if((npc ^ pc) >> 8)
		memread(pc & 0xff00 | npc & 0xff);
	pc = npc;
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
	u8int v;

	v = memread(a);
	memwrite(a, v);
	memwrite(a, nz(v - 1));
}

static void
inc(u16int a)
{
	u8int v;

	v = memread(a);
	memwrite(a, v);
	v = nz(v + 1);
	memwrite(a, v);
}

static void
rol(u16int a)
{
	u8int v, b;
	
	v = memread(a);
	memwrite(a, v);
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
	memwrite(a, v);
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
	
	if((rP & FLAGD) != 0){
		d = ~d;
		r = (rA & 0xf) + (d & 0xf) + (rP & FLAGC);
		if(r < 0x10) r -= 0x06;
		if(r < 0) r += 0x10;
		r += (rA & 0xf0) + (d & 0xf0);
	}else
		r = rA + (u8int)~d + (rP & FLAGC);
	rP &= ~(FLAGZ | FLAGV | FLAGC | FLAGN);
	if(((rA ^ d) & (rA ^ r)) & 0x80) rP |= FLAGV;
	if(r > 0xFF) rP |= FLAGC;
	else if((rP & FLAGD) != 0)
		r -= 0x60;
	rA = r;
	if(rA == 0) rP |= FLAGZ;
	if(rA & 0x80) rP |= FLAGN;
}

static void
interrupt(int nmi, int brk)
{
	fetch8();
	push16(pc);
	push8(rP | 0x20 | (brk << 4));
	pc = memread(0xFFFA | (!nmi << 2));
	pc |= memread(0xFFFB | (!nmi << 2)) << 8;
	rP |= FLAGI;
}

void
step(void)
{
	u8int op;
	u16int a, v;

	if(nrdy){
		io();
		return;
	}
	curpc = pc;
	op = fetch8();
	if(trace)
		print("%.4x %.2x | %.2x %.2x %.2x | %.2x %.2x | %3d %3d\n", curpc, op, rA, rX, rY, rS, rP, ppux-3, ppuy);
	switch(op){
	case 0x00: fetch8(); interrupt(0, 1); return;
	case 0x01: nz(rA |= indX()); return;
	case 0x05: nz(rA |= zp()); return;
	case 0x06: asl(fetch8()); return;
	case 0x08: memread(pc); push8(rP | 0x30); return;
	case 0x09: nz(rA |= imm()); return;
	case 0x0A:
		rP &= ~(FLAGN | FLAGZ | FLAGC);
		if(rA & 0x80) rP |= FLAGC;
		rA <<= 1;
		if(rA == 0) rP |= FLAGZ;
		if(rA & 0x80) rP |= FLAGN;
		memread(pc);
		return;
	case 0x0D: nz(rA |= abso()); return;
	case 0x0E: asl(fetch16()); return;
	case 0x10: if((rP & FLAGN) == 0) branch(); else fetch8(); return;
	case 0x11: nz(rA |= indY()); return;
	case 0x15: nz(rA |= zpX()); return;
	case 0x16: asl(azpX(rX)); return;
	case 0x18: rP &= ~FLAGC; memread(pc); return;
	case 0x19: nz(rA |= absY()); return;
	case 0x1D: nz(rA |= absX()); return;
	case 0x1E: asl(aabsX(rX, 1)); return;
	case 0x20: v = fetch8(); memread(rS|0x100); push16(pc); pc = fetch8() << 8 | v; return;
	case 0x21: nz(rA &= indX()); return;
	case 0x24:
		a = memread(fetch8());
		rP &= ~(FLAGN | FLAGZ | FLAGV);
		rP |= a & 0xC0;
		if((a & rA) == 0) rP |= FLAGZ;
		return;
	case 0x25: nz(rA &= zp()); return;
	case 0x26: rol(fetch8()); return;
	case 0x28: memread(pc); memread(0x100|rS); rP = pop8() & 0xcf; return;
	case 0x29: nz(rA &= imm()); return;
	case 0x2A:
		a = rP & FLAGC;
		rP &= ~(FLAGC | FLAGZ | FLAGN);
		if(rA & 0x80) rP |= FLAGC;
		rA = (rA << 1) | a;
		if(rA & 0x80) rP |= FLAGN;
		if(rA == 0) rP |= FLAGZ;
		memread(pc);
		return;
	case 0x2C:
		a = memread(fetch16());
		rP &= ~(FLAGN | FLAGZ | FLAGV);
		rP |= a & 0xC0;
		if((a & rA) == 0) rP |= FLAGZ;
		return;
	case 0x2D: nz(rA &= abso()); return;
	case 0x2E: rol(fetch16()); return;
	case 0x30: if((rP & FLAGN) != 0) branch(); else fetch8(); return;
	case 0x31: nz(rA &= indY()); return;
	case 0x35: nz(rA &= zpX()); return;
	case 0x36: rol(azpX(rX)); return;
	case 0x38: rP |= FLAGC; memread(pc); return;
	case 0x39: nz(rA &= absY()); return;
	case 0x3E: rol(aabsX(rX, 1)); return;
	case 0x3D: nz(rA &= absX()); return;
	case 0x40: fetch8(); memread(rS|0x100); rP = pop8() & 0xcf; pc = pop16(); return;
	case 0x41: nz(rA ^= indX()); return;
	case 0x45: nz(rA ^= zp()); return;
	case 0x46: lsr(fetch8()); return;
	case 0x48: memread(pc); push8(rA); return;
	case 0x49: nz(rA ^= imm()); return;
	case 0x4A:
		rP &= ~(FLAGN | FLAGZ | FLAGC);
		rP |= rA & 1;
		rA >>= 1;
		if(rA == 0) rP |= FLAGZ;
		if(rA & 0x80) rP |= FLAGN;
		memread(pc);
		return;
	case 0x4C: pc = fetch16(); return;
	case 0x4D: nz(rA ^= abso()); return;
	case 0x4E: lsr(fetch16()); return;
	case 0x51: nz(rA ^= indY()); return;
	case 0x56: lsr(azpX(rX)); return;
	case 0x58: rP &= ~FLAGI; memread(pc); return;
	case 0x50: if((rP & FLAGV) == 0) branch(); else fetch8(); return;
	case 0x55: nz(rA ^= zpX()); return;
	case 0x59: nz(rA ^= absY()); return;
	case 0x5D: nz(rA ^= absX()); return;
	case 0x5E: lsr(aabsX(rX, 1)); return;
	case 0x60: fetch8(); memread(rS | 0x100); pc = pop16(); fetch8(); return;
	case 0x61: adc(indX()); return;
	case 0x65: adc(zp()); return;
	case 0x66: ror(fetch8()); return;
	case 0x68: memread(pc); memread(0x100|rS); nz(rA = pop8()); return;
	case 0x69: adc(imm()); return;
	case 0x6A:
		a = rP & FLAGC;
		rP &= ~(FLAGC | FLAGN | FLAGZ);
		rP |= rA & 1;
		rA = (rA >> 1) | (a << 7);
		if(rA & 0x80) rP |= FLAGN;
		if(rA == 0) rP |= FLAGZ;
		memread(pc);
		return;
	case 0x6C: v = fetch16(); pc = memread(v) | (memread((v & 0xFF00) | (u8int)(v+1)) << 8); return;
	case 0x6D: adc(abso()); return;
	case 0x6E: ror(fetch16()); return;
	case 0x70: if((rP & FLAGV) != 0) branch(); else fetch8(); return;
	case 0x71: adc(indY()); return;
	case 0x75: adc(zpX()); return;
	case 0x76: ror(azpX(rX)); return;
	case 0x78: rP |= FLAGI; memread(pc); return;
	case 0x79: adc(absY()); return;
	case 0x7D: adc(absX()); return;
	case 0x7E: ror(aabsX(rX, 1)); return;
	case 0x81: memwrite(aindX(), rA); return;
	case 0x84: memwrite(fetch8(), rY); return;
	case 0x85: memwrite(fetch8(), rA); return;
	case 0x86: memwrite(fetch8(), rX); return;
	case 0x88: nz(--rY); memread(pc); return;
	case 0x8A: nz(rA = rX); memread(pc); return;
	case 0x8C: memwrite(fetch16(), rY); return;
	case 0x8D: memwrite(fetch16(), rA); return;
	case 0x8E: memwrite(fetch16(), rX); return;
	case 0x90: if((rP & FLAGC) == 0) branch(); else fetch8(); return;
	case 0x91: memwrite(aindY(1), rA); return;
	case 0x94: memwrite(azpX(rX), rY); return;
	case 0x95: memwrite(azpX(rX), rA); return;
	case 0x96: memwrite(azpX(rY), rX); return;
	case 0x98: nz(rA = rY); memread(pc); return;
	case 0x99: memwrite(aabsX(rY, 1), rA); return;
	case 0x9A: rS = rX; memread(pc); return;
	case 0x9D: memwrite(aabsX(rX, 1), rA); return;
	case 0xA0: nz(rY = imm()); return;
	case 0xA1: nz(rA = indX()); return;
	case 0xA2: nz(rX = imm()); return;
	case 0xA4: nz(rY = zp()); return;
	case 0xA5: nz(rA = zp()); return;
	case 0xA6: nz(rX = zp()); return;
	case 0xA8: nz(rY = rA); memread(pc); return;
	case 0xA9: nz(rA = imm()); return;
	case 0xAA: nz(rX = rA); memread(pc); return;
	case 0xAC: nz(rY = abso()); return;
	case 0xAE: nz(rX = abso()); return;
	case 0xAD: nz(rA = abso()); return;
	case 0xB0: if((rP & FLAGC) != 0) branch(); else fetch8(); return;
	case 0xB1: nz(rA = indY()); return;
	case 0xB4: nz(rY = zpX()); return;
	case 0xB5: nz(rA = zpX()); return;
	case 0xB6: nz(rX = zpY()); return;
	case 0xB8: rP &= ~FLAGV; memread(pc); return;
	case 0xB9: nz(rA = absY()); return;
	case 0xBA: nz(rX = rS); memread(pc); return;
	case 0xBC: nz(rY = absX()); return;
	case 0xBD: nz(rA = absX()); return;
	case 0xBE: nz(rX = absY()); return;
	case 0xC1: cmp(rA, indX()); return;
	case 0xC5: cmp(rA, zp()); return;
	case 0xC9: cmp(rA, imm()); return;
	case 0xCD: cmp(rA, abso()); return;
	case 0xD0: if((rP & FLAGZ) == 0) branch(); else fetch8(); return;
	case 0xD1: cmp(rA, indY()); return;
	case 0xD5: cmp(rA, zpX()); return;
	case 0xD8: rP &= ~FLAGD; memread(pc); return;
	case 0xD9: cmp(rA, absY()); return;
	case 0xDD: cmp(rA, absX()); return;
	case 0xC0: cmp(rY, imm()); return;
	case 0xC4: cmp(rY, zp()); return;
	case 0xC6: dec(fetch8()); return;
	case 0xC8: nz(++rY); memread(pc); return;
	case 0xCA: nz(--rX); memread(pc); return;
	case 0xCC: cmp(rY, abso()); return;
	case 0xCE: dec(fetch16()); return;
	case 0xD6: dec(azpX(rX)); return;
	case 0xDE: dec(aabsX(rX, 1)); return;
	case 0xE0: cmp(rX, imm()); return;
	case 0xE1: sbc(indX()); return;
	case 0xE4: cmp(rX, zp()); return;
	case 0xE5: sbc(zp()); return;
	case 0xE6: inc(fetch8()); return;
	case 0xE8: nz(++rX); memread(pc); return;
	case 0xE9: sbc(imm()); return;
	case 0xEA: memread(pc); return;
	case 0xEC: cmp(rX, abso()); return;
	case 0xED: sbc(abso()); return;
	case 0xEE: inc(fetch16()); return;
	case 0xF0: if((rP & FLAGZ) != 0) branch(); else fetch8(); return;
	case 0xF1: sbc(indY()); return;
	case 0xF5: sbc(zpX()); return;
	case 0xF6: inc(azpX(rX)); return;
	case 0xF8: rP |= FLAGD; memread(pc); return;
	case 0xF9: sbc(absY()); return;
	case 0xFD: sbc(absX()); return;
	case 0xFE: inc(aabsX(rX, 1)); return;
	default:
		fprint(2, "undefined %#x (pc %#x)\n", op, curpc);
		return;
	}
}
