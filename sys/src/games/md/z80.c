#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

u8int s[16], ipage, intm, z80irq;
u16int ix[2];
u16int spc, scurpc, sp;
int halt;

enum {
	FLAGC = 0x01,
	FLAGN = 0x02,
	FLAGV = 0x04,
	FLAGH = 0x10,
	FLAGZ = 0x40,
	FLAGS = 0x80
};
enum { rB, rC, rD, rE, rH, rL, rHL, rA, rF = rHL };
#define BC() (s[rB] << 8 | s[rC])
#define DE() (s[rD] << 8 | s[rE])
#define HL() (s[rH] << 8 | s[rL])

static u8int
fetch8(void)
{
	return z80read(spc++);
}

static u16int
fetch16(void)
{
	u16int u;
	
	u = z80read(spc++);
	return u | z80read(spc++) << 8;
}

static void
push8(u8int u)
{
	z80write(--sp, u);
}

static void
push16(u16int u)
{
	z80write(--sp, u >> 8);
	z80write(--sp, u);
}

static u8int
pop8(void)
{
	return z80read(sp++);
}

static u16int
pop16(void)
{
	u16int v;
	
	v = z80read(sp++);
	return v | z80read(sp++) << 8;
}

static u16int
read16(u16int n)
{
	return z80read(n) | z80read(n+1) << 8;
}

static void
write16(u16int n, u16int v)
{
	z80write(n++, v);
	z80write(n, v >> 8);
}

static int
parity(u8int v)
{
	return (((v * 0x0101010101010101ULL) & 0x8040201008040201ULL) % 0x1FF) & 1;
}

static int
move(u8int dst, u8int src)
{
	if(dst == rHL){
		if(src == rHL){
			halt = 1;
			return 4;
		}
		z80write(HL(), s[src]);
		return 7;
	}
	if(src == rHL){
		s[dst] = z80read(HL());
		return 7;
	}
	s[dst] = s[src];
	return 4;
}

static int
alu(u8int op, u8int n)
{
	u8int v4;
	u8int u;
	u16int v;
	int t;

	switch(n){
	case 8: u = fetch8(); t = 3; break;
	case 10: u = ix[0]; t = 4; break;
	case 11: u = ix[1]; t = 4; break;
	case 12: u = ix[0] >> 8; t = 4; break;
	case 13: u = ix[1] >> 8; t = 4; break;
	case 14: u = z80read(ix[0] + fetch8()); t = 15; break;
	case 15: u = z80read(ix[1] + fetch8()); t = 15; break;
	case rHL:
		u = z80read(HL());
		t = 3;
		break;
	default:
		u = s[n];
		t = 0;
	}
	v4 = 0;
	switch(op){
	default:
		v4 = (s[rA] & 0x0f) + (u & 0x0f);
		v = s[rA] + u;
		break;
	case 1:
		v4 = (s[rA] & 0x0f) + (u & 0x0f) + (s[rF] & 1);
		v = s[rA] + u + (s[rF] & 1);
		break;
	case 2:
	case 7:
		v4 = (s[rA] & 0x0f) + (~u & 0x0f) + 1;
		v = s[rA] + (u ^ 0xff) + 1;
		break;
	case 3:
		v4 = (s[rA] & 0x0f) + (~u & 0x0f) + (~s[rF] & 1);
		v = s[rA] + (u ^ 0xff) + (~s[rF] & 1);
		break;
	case 4: v = s[rA] & u; break;
	case 5: v = s[rA] ^ u; break;
	case 6: v = s[rA] | u; break;
	}
	s[rF] = 0;
	if((u8int)v == 0)
		s[rF] |= FLAGZ;
	if((v & 0x80) != 0)
		s[rF] |= FLAGS;
	if(op < 2){
		if((v & 0x100) != 0)
			s[rF] |= FLAGC;
		if((v4 & 0x10) != 0)
			s[rF] |= FLAGH;
		if((~(s[rA] ^ u) & (s[rA] ^ v) & 0x80) != 0)
			s[rF] |= FLAGV;
	}else if(op < 4 || op == 7){
		s[rF] |= FLAGN;
		if((v & 0x100) == 0)
			s[rF] |= FLAGC;
		if((v4 & 0x10) == 0)
			s[rF] |= FLAGH;
		if(((s[rA] ^ u) & (s[rA] ^ v) & 0x80) != 0)
			s[rF] |= FLAGV;
	}else{
		if(!parity(v))
			s[rF] |= FLAGV;
		if(op == 4)
			s[rF] |= FLAGH;
	}
	if(op != 7)
		s[rA] = v;
	return 4+t;
}

static int
branch(int cc, int t)
{
	u16int v;
	
	v = (s8int)fetch8();
	if(!cc)
		return t + 7;
	spc += v;
	return t + 12;
}

static u8int
inc(u8int v)
{
	s[rF] &= FLAGC;
	++v;
	if(v == 0)
		s[rF] |= FLAGZ;
	if((v & 0x80) != 0)
		s[rF] |= FLAGS;
	if(v == 0x80)
		s[rF] |= FLAGV;
	if((v & 0xf) == 0)
		s[rF] |= FLAGH;
	return v;
}

static u8int
dec(u8int v)
{
	--v;
	s[rF] = s[rF] & FLAGC | FLAGN;
	if(v == 0)
		s[rF] |= FLAGZ;
	if((v & 0x80) != 0)
		s[rF] |= FLAGS;
	if(v == 0x7f)
		s[rF] |= FLAGV;
	if((v & 0xf) == 0xf)
		s[rF] |= FLAGH;
	return v;
}

static int
addhl(u16int u)
{
	u32int v;
	
	s[rF] &= ~(FLAGN|FLAGC|FLAGH);
	v = HL() + u;
	if((v & 0x10000) != 0)
		s[rF] |= FLAGC;
	if((HL() & 0xfff) + (u & 0xfff) >= 0x1000)
		s[rF] |= FLAGH;
	s[rL] = v;
	s[rH] = v >> 8;
	return 11;
}

static void
adchl(u16int u)
{
	u32int v, v4;
	
	v = HL() + u + (s[rF] & FLAGC);
	v4 = (HL() & 0xfff) + (u & 0xfff) + (s[rF] & FLAGC);
	s[rF] = 0;
	if((v & 0x10000) != 0)
		s[rF] |= FLAGC;
	if((v4 & 0x1000) != 0)
		s[rF] |= FLAGH;
	if((u16int)v == 0)
		s[rF] |= FLAGZ;
	if((v & 0x8000) != 0)
		s[rF] |= FLAGS;
	if((~(HL() ^ u) & (HL() ^ v) & 0x8000) != 0)
		s[rF] |= FLAGV;
	s[rL] = v;
	s[rH] = v >> 8;
}

static void
sbchl(u16int u)
{
	u32int v, v4;
	
	v = HL() + (u16int)~u + (~s[rF] & FLAGC);
	v4 = (HL() & 0xfff) + (~u & 0xfff) + (~s[rF] & FLAGC);
	s[rF] = FLAGN;
	if((v & 0x10000) == 0)
		s[rF] |= FLAGC;
	if((v4 & 0x1000) == 0)
		s[rF] |= FLAGH;
	if((u16int)v == 0)
		s[rF] |= FLAGZ;
	if((v & 0x8000) != 0)
		s[rF] |= FLAGS;
	if(((HL() ^ u) & (HL() ^ v) & 0x8000) != 0)
		s[rF] |= FLAGV;
	s[rL] = v;
	s[rH] = v >> 8;
}

static int
addindex(int n, u16int u)
{
	u32int v;
	
	s[rF] &= ~(FLAGN|FLAGC|FLAGH);
	v = ix[n] + u;
	if((v & 0x10000) != 0)
		s[rF] |= FLAGC;
	if((ix[n] & 0xfff) + (u & 0xfff) >= 0x1000)
		s[rF] |= FLAGH;
	ix[n] = v;
	return 15;
}

static int
jump(int cc)
{
	u16int v;
	
	v = fetch16();
	if(cc)
		spc = v;
	return 10;
}

static int
call(u16int a, int cc)
{
	if(!cc)
		return 10;
	push16(spc);
	spc = a;
	return 17;
}

static void
swap(u8int a)
{
	u8int v;
	
	v = s[a];
	s[a] = s[a + 8];
	s[a + 8] = v;
}

static int
bits(int i)
{
	u8int op, v, n, m, c;
	u16int a;
	int t;
	
	SET(a, v, t);
	if(i >= 0){
		a = ix[i] + fetch8();
		v = z80read(a);
		t = 23;
	}
	op = fetch8();
	n = op & 7;
	m = op >> 3 & 7;
	if(i < 0){
		a = HL();
		if(n == 6){
			v = z80read(a);
			t = 15;
		}else{
			v = s[n];
			t = 8;
		}
	}
	switch(op >> 6){
	case 0:
		c = s[rF] & FLAGC;
		switch(m){
		default: s[rF] = v >> 7; v = v << 1 | v >> 7; break;
		case 1:  s[rF] = v & 1; v = v >> 1 | v << 7; break;
		case 2:  s[rF] = v >> 7; v = v << 1 | c; break;
		case 3:  s[rF] = v & 1; v = v >> 1 | c << 7; break;
		case 4:  s[rF] = v >> 7; v = v << 1; break;
		case 5:  s[rF] = v & 1; v = v & 0x80 | v >> 1; break;
		case 6:  s[rF] = v >> 7; v = v << 1 | 1; break;
		case 7:  s[rF] = v & 1; v >>= 1; break;
		}
		if(v == 0)
			s[rF] |= FLAGZ;
		if((v & 0x80) != 0)
			s[rF] |= FLAGS;
		if(!parity(v))
			s[rF] |= FLAGV;
		break;
	case 1:
		s[rF] = s[rF] & ~(FLAGN|FLAGZ) | FLAGH;
		if((v & 1<<m) == 0)
			s[rF] |= FLAGZ;
		return t;
	case 2:
		v &= ~(1<<m);
		break;
	case 3:
		v |= (1<<m);
	}
	if(n == 6)
		z80write(a, v);
	else
		s[n] = v;
	return t;
}

static int
ed(void)
{
	u8int op, v, u, l;
	u16int a;
	
	op = fetch8();
	switch(op){
	case 0xa0: case 0xa1: case 0xa8: case 0xa9:
	case 0xb0: case 0xb1: case 0xb8: case 0xb9:
		switch(op & 3){
		default:
			z80write(DE(), z80read(HL()));
			s[rF] &= ~(FLAGN|FLAGH);
			l = 1;
			break;
		case 1:
			u = z80read(HL());
			v = s[rA] - u;
			s[rF] = s[rF] & ~(FLAGS|FLAGZ|FLAGH) | FLAGN;
			l = v != 0;
			if((v & 0x80) != 0)
				s[rF] |= FLAGS;
			if(v == 0)
				s[rF] |= FLAGZ;
			if((s[rA] & 0xf) < (u & 0xf))
				s[rF] |= FLAGH;
			break;
		}
		if((op & 8) != 0){
			if((op & 3) == 0 && s[rE]-- == 0)
				s[rD]--;
			if(s[rL]-- == 0)
				s[rH]--;
		}else{
			if((op & 3) == 0 && ++s[rE] == 0)
				s[rD]++;
			if(++s[rL] == 0)
				s[rH]++;
		}
		if(s[rC]-- == 0)
			s[rB]--;
		if((s[rC] | s[rB]) != 0){
			s[rF] |= FLAGV;
			if((op & 0x10) != 0 && l){
				spc -= 2;
				return 21;
			}
		}else
			s[rF] &= ~FLAGV;
		return 16;
	case 0x42: sbchl(BC()); return 15;
	case 0x52: sbchl(DE()); return 15;
	case 0x62: sbchl(HL()); return 15;
	case 0x72: sbchl(sp); return 15;
	case 0x43: write16(fetch16(), BC()); return 20;
	case 0x53: write16(fetch16(), DE()); return 20;
	case 0x63: write16(fetch16(), HL()); return 20;
	case 0x73: write16(fetch16(), sp); return 20;
	case 0x44:
		s[rA] = -s[rA];
		s[rF] = FLAGN;
		if(s[rA] == 0)
			s[rF] |= FLAGZ;
		else
			s[rF] |= FLAGC;
		if((s[rA] & 0x80) != 0)
			s[rF] |= FLAGS;
		if(s[rA] == 0x80)
			s[rF] |= FLAGV;
		if((s[rA] & 0xf) != 0)
			s[rF] |= FLAGH;
		return 8;
	case 0x46: intm &= 0xc0; return 8;
	case 0x56: intm = intm & 0xc0 | 1; return 8;
	case 0x47: ipage = s[rA]; return 9;
	case 0x57: s[rA] = ipage; return 9;
	case 0x67:
		v = z80read(HL());
		z80write(HL(), v >> 4 | s[rA] << 4);
		s[rA] = s[rA] & 0xf0 | v & 0x0f;
		if(0){
	case 0x6f:
			v = z80read(HL());
			z80write(HL(), v << 4 | s[rA] & 0xf);
			s[rA] = s[rA] & 0xf0 | v >> 4;
		}
		s[rF] &= FLAGC;
		if(s[rA] == 0)
			s[rF] |= FLAGZ;
		if((s[rA] & 0x80) != 0)
			s[rF] |= FLAGS;
		if(!parity(s[rA]))
			s[rF] |= FLAGV;
		return 18;
	case 0x4a: adchl(BC()); return 15;
	case 0x5a: adchl(DE()); return 15;
	case 0x6a: adchl(HL()); return 15;
	case 0x7a: adchl(sp); return 15;
	case 0x4b: a = fetch16(); s[rC] = z80read(a++); s[rB] = z80read(a); return 20;
	case 0x5b: a = fetch16(); s[rE] = z80read(a++); s[rD] = z80read(a); return 20;
	case 0x6b: a = fetch16(); s[rL] = z80read(a++); s[rH] = z80read(a); return 20;
	case 0x7b: sp = read16(fetch16()); return 20;
	case 0x4d: spc = pop16(); return 14;
	case 0x5e: intm = intm & 0xc0 | 2; return 8;
	case 0x4f: return 9;
	}
	sysfatal("undefined z80 opcode ed%.2x at pc=%#.4x", op, scurpc);
	return 0;
}

static int
index(int n)
{
	u8int op;
	u16int v;
	
	op = fetch8();
	switch(op){
	case 0x40: case 0x41: case 0x42: case 0x43: case 0x47:
	case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4f:
	case 0x50: case 0x51: case 0x52: case 0x53: case 0x57:
	case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5f:
	case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7f:
		s[op >> 3 & 7] = s[op & 7];
		return 8;
	case 0x60: case 0x61: case 0x62: case 0x63: case 0x67:
		ix[n] = ix[n] & 0xff | s[op & 7] << 8;
		return 8;
	case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6f:
		ix[n] = ix[n] & 0xff00 | s[op & 7];
		return 8;
	case 0x65: ix[n] = ix[n] << 8 | ix[n] & 0xff; return 8;
	case 0x6c: ix[n] = ix[n] >> 8 | ix[n] & 0xff00; return 8;
	case 0x64: case 0x6d: return 8;
	case 0x70: case 0x71: case 0x72: case 0x73:
	case 0x74: case 0x75: case 0x77:
		z80write(ix[n] + fetch8(), s[op & 7]);
		return 19;
	case 0x44: case 0x4c: case 0x54: case 0x5c: case 0x7c:
		s[op >> 3 & 7] = ix[n] >> 8;
		return 8;
	case 0x45: case 0x4d: case 0x55: case 0x5d: case 0x7d:
		s[op >> 3 & 7] = ix[n];
		return 8;
	case 0x46: case 0x4e: case 0x56: case 0x5e: case 0x66: case 0x6e: case 0x7e:
		s[op >> 3 & 7] = z80read(ix[n] + fetch8());
		return 19;
	case 0x84: case 0x8c: case 0x94: case 0x9c:
	case 0xa4: case 0xac: case 0xb4: case 0xbc:
		return alu(op >> 3 & 7, 12 + n);
	case 0x85: case 0x8d: case 0x95: case 0x9d:
	case 0xa5: case 0xad: case 0xb5: case 0xbd:
		return alu(op >> 3 & 7, 10 + n);
	case 0x86: case 0x8e: case 0x96: case 0x9e:
	case 0xa6: case 0xae: case 0xb6: case 0xbe:
		return alu(op >> 3 & 7, 14 + n);	
	
	case 0x21: ix[n] = fetch16(); return 14;
	case 0xe1: ix[n] = pop16(); return 14;
	case 0x22: write16(fetch16(), ix[n]); return 20;
	case 0x23: ix[n]++; return 10;
	case 0xe3: v = ix[n]; ix[n] = read16(sp); write16(sp, v); return 23;
	case 0x24: inc(ix[n] >> 8); ix[n] += 0x100; return 8;
	case 0x34: v = ix[n] + fetch8(); z80write(v, inc(z80read(v))); return 23;
	case 0x25: dec(ix[n] >> 8); ix[n] -= 0x100; return 8;
	case 0x35: v = ix[n] + fetch8(); z80write(v, dec(z80read(v))); return 23;
	case 0xe5: push16(ix[n]); return 15;
	case 0x26: ix[n] = ix[n] & 0xff | fetch8() << 8; return 11;
	case 0x36: v = ix[n] + fetch8(); z80write(v, fetch8()); return 19;
	case 0x09: return addindex(n, BC());
	case 0x19: return addindex(n, DE());
	case 0x29: return addindex(n, ix[n]);
	case 0x39: return addindex(n, sp);
	case 0xe9: spc = ix[n]; return 8;
	case 0xf9: sp = ix[n]; return 10;
	case 0x2a: ix[n] = read16(fetch16()); return 20;
	case 0x2b: ix[n]--; return 10;
	case 0xcb: return bits(n);
	case 0x2c: inc(ix[n]++); return 8;
	case 0x2d: dec(ix[n]--); return 8;
	case 0x2e: ix[n] = ix[n] & 0xff00 | fetch8(); return 11;
	}
	sysfatal("undefined z80 opcode %.2x%.2x at pc=%#.4x", n ? 0xfd : 0xdd, op, scurpc);
	return 0;
}

int
z80step(void)
{
	u8int op;
	u16int v, w;

	if((z80bus & RESET) != 0){
		scurpc = spc = 0;
		intm = 0;
		ipage = 0;
		return 1;
	}
	if((z80bus & BUSACK) != 0){
		if((z80bus & BUSREQ) == 0)
			z80bus &= ~BUSACK;
		return 1;
	}
	if((z80bus & BUSREQ) != 0){
		z80bus |= BUSACK;
		return 1;
	}
	if(z80irq != 0 && (intm & 0x80) != 0){
		push16(spc);
		intm &= 0x3f;
		switch(intm & 3){
		case 1:
			spc = 0x38;
			return 2;
		default:
			sysfatal("z80 interrupt in mode %d", intm & 3);
		}
	}
	scurpc = spc;
	if(0)
		print("%x AF %.2x%.2x BC %.2x%.2x DE %.2x%.2x HL %.2x%.2x IX %.4x IY %.4x\n", scurpc, s[rA], s[rF], s[rB], s[rC], s[rD], s[rE], s[rH], s[rL], ix[0], ix[1]);
	op = fetch8();
	switch(op >> 6){
	case 1: return move(op >> 3 & 7, op & 7);
	case 2: return alu(op >> 3 & 7, op & 7);
	}
	switch(op){
	case 0x00: return 4;
	case 0x10: return branch(--s[rB] != 0, 1);
	case 0x20: return branch((s[rF] & FLAGZ) == 0, 0);
	case 0x30: return branch((s[rF] & FLAGC) == 0, 0);
	case 0x01: s[rC] = fetch8(); s[rB] = fetch8(); return 10;
	case 0x11: s[rE] = fetch8(); s[rD] = fetch8(); return 10;
	case 0x21: s[rL] = fetch8(); s[rH] = fetch8(); return 10;
	case 0x31: sp = fetch16(); return 10;
	case 0x02: z80write(BC(), s[rA]); return 7;
	case 0x12: z80write(DE(), s[rA]); return 7;
	case 0x22: v = fetch16(); z80write(v++, s[rL]); z80write(v, s[rH]); return 16;
	case 0x32: z80write(fetch16(), s[rA]); return 13;
	case 0x03: if(++s[rC] == 0) s[rB]++; return 6;
	case 0x13: if(++s[rE] == 0) s[rD]++; return 6;
	case 0x23: if(++s[rL] == 0) s[rH]++; return 6;
	case 0x33: sp++; return 6;
	case 0x04: inc(s[rB]++); return 4;
	case 0x14: inc(s[rD]++); return 4;
	case 0x24: inc(s[rH]++); return 4;
	case 0x34: z80write(HL(), inc(z80read(HL()))); return 11;
	case 0x05: dec(s[rB]--); return 4;
	case 0x15: dec(s[rD]--); return 4;
	case 0x25: dec(s[rH]--); return 4;
	case 0x35: z80write(HL(), dec(z80read(HL()))); return 11;
	case 0x06: s[rB] = fetch8(); return 7;
	case 0x16: s[rD] = fetch8(); return 7;
	case 0x26: s[rH] = fetch8(); return 7;
	case 0x36: z80write(HL(), fetch8()); return 10;
	case 0x07:
		s[rF] = s[rF] & ~(FLAGC|FLAGN|FLAGH) | s[rA] >> 7;
		s[rA] = s[rA] << 1 | s[rA] >> 7;
		return 4;
	case 0x17:
		v = s[rF] & FLAGC;
		s[rF] = s[rF] & ~(FLAGC|FLAGN|FLAGH) | s[rA] >> 7;
		s[rA] = s[rA] << 1 | v;
		return 4;
	case 0x27:
		if(s[rA] > 0x99 || (s[rF] & FLAGC) != 0){
			s[rF] |= FLAGC;
			v = 0x60;
		}else{
			s[rF] &= ~FLAGC;
			v = 0;
		}
		if((s[rA] & 0xf) > 9 || (s[rF] & FLAGH) != 0)
			v |= 6;
		w = s[rA];
		if((s[rF] & FLAGN) != 0)
			s[rA] -= v;
		else
			s[rA] += v;
		s[rF] &= ~(FLAGV|FLAGS|FLAGZ|FLAGH);
		if(((s[rA] ^ w) & 0x10) != 0)
			s[rF] |= FLAGH;
		if(!parity(s[rA]))
			s[rF] |= FLAGV;
		if(s[rA] == 0)
			s[rF] |= FLAGZ;
		if((s[rA] & 0x80) != 0)
			s[rF] |= FLAGS;
		return 4;
	case 0x37: s[rF] = s[rF] & ~(FLAGN | FLAGH) | FLAGC; return 4;
	case 0x08:
		swap(rA);
		swap(rF);
		return 4;
	case 0x18: return branch(1, 0);
	case 0x28: return branch((s[rF] & FLAGZ) != 0, 0);
	case 0x38: return branch((s[rF] & FLAGC) != 0, 0);
	case 0x09: return addhl(BC());
	case 0x19: return addhl(DE());
	case 0x29: return addhl(HL());
	case 0x39: return addhl(sp);
	case 0x0a: s[rA] = z80read(BC()); return 7;
	case 0x1a: s[rA] = z80read(DE()); return 7;
	case 0x2a: v = fetch16(); s[rL] = z80read(v++); s[rH] = z80read(v); return 16;
	case 0x3a: s[rA] = z80read(fetch16()); return 13;
	case 0x0b: if(s[rC]-- == 0) s[rB]--; return 6;
	case 0x1b: if(s[rE]-- == 0) s[rD]--; return 6;
	case 0x2b: if(s[rL]-- == 0) s[rH]--; return 6;
	case 0x3b: sp--; return 6;
	case 0x0c: inc(s[rC]++); return 4;
	case 0x1c: inc(s[rE]++); return 4;
	case 0x2c: inc(s[rL]++); return 4;
	case 0x3c: inc(s[rA]++); return 4;
	case 0x0d: dec(s[rC]--); return 4;
	case 0x1d: dec(s[rE]--); return 4;
	case 0x2d: dec(s[rL]--); return 4;
	case 0x3d: dec(s[rA]--); return 4;
	case 0x0e: s[rC] = fetch8(); return 7;
	case 0x1e: s[rE] = fetch8(); return 7;
	case 0x2e: s[rL] = fetch8(); return 7;
	case 0x3e: s[rA] = fetch8(); return 7;
	case 0x0f:
		s[rF] = s[rF] & ~(FLAGC|FLAGN|FLAGH) | s[rA] & 1;
		s[rA] = s[rA] >> 1 | s[rA] << 7;
		return 4;
	case 0x1f:
		v = s[rF] << 7;
		s[rF] = s[rF] & ~(FLAGC|FLAGN|FLAGH) | s[rA] & 1;
		s[rA] = s[rA] >> 1 | v;
		return 4;
	case 0x2f:
		s[rF] |= FLAGN|FLAGH;
		s[rA] ^= 0xff;
		return 4;
	case 0x3f:
		s[rF] = s[rF] & ~(FLAGN|FLAGH) ^ FLAGC | s[rF] << 4 & FLAGH;
		return 4;
	case 0xc0: if((s[rF] & FLAGZ) == 0) {spc = pop16(); return 11;} return 5;
	case 0xd0: if((s[rF] & FLAGC) == 0) {spc = pop16(); return 11;} return 5;
	case 0xe0: if((s[rF] & FLAGV) == 0) {spc = pop16(); return 11;} return 5;
	case 0xf0: if((s[rF] & FLAGS) == 0) {spc = pop16(); return 11;} return 5;
	case 0xc1: s[rC] = pop8(); s[rB] = pop8(); return 10;
	case 0xd1: s[rE] = pop8(); s[rD] = pop8(); return 10;
	case 0xe1: s[rL] = pop8(); s[rH] = pop8(); return 10;
	case 0xf1: s[rF] = pop8(); s[rA] = pop8(); return 10;
	case 0xc2: return jump((s[rF] & FLAGZ) == 0);
	case 0xd2: return jump((s[rF] & FLAGC) == 0);
	case 0xe2: return jump((s[rF] & FLAGV) == 0);
	case 0xf2: return jump((s[rF] & FLAGS) == 0);
	case 0xc3: return jump(1);
	case 0xd3: z80out(fetch8(), s[rA]); return 11;
	case 0xe3:
		v = HL();
		s[rL] = z80read(sp);
		s[rH] = z80read(sp + 1);
		write16(sp, v);
		return 19;
	case 0xf3: intm &= 0x3f; return 4;
	case 0xc4: return call(fetch16(), (s[rF] & FLAGZ) == 0);
	case 0xd4: return call(fetch16(), (s[rF] & FLAGC) == 0);
	case 0xe4: return call(fetch16(), (s[rF] & FLAGV) == 0);
	case 0xf4: return call(fetch16(), (s[rF] & FLAGS) == 0);
	case 0xc5: push8(s[rB]); push8(s[rC]); return 11;
	case 0xd5: push8(s[rD]); push8(s[rE]); return 11;
	case 0xe5: push8(s[rH]); push8(s[rL]); return 11;
	case 0xf5: push8(s[rA]); push8(s[rF]); return 11;
	case 0xc6: return alu(0, 8);
	case 0xd6: return alu(2, 8);
	case 0xe6: return alu(4, 8);
	case 0xf6: return alu(6, 8);
	case 0xc7: return call(0x00, 1);
	case 0xd7: return call(0x10, 1);
	case 0xe7: return call(0x20, 1);
	case 0xf7: return call(0x30, 1);
	case 0xc8: if((s[rF] & FLAGZ) != 0) {spc = pop16(); return 11;} return 5;
	case 0xd8: if((s[rF] & FLAGC) != 0) {spc = pop16(); return 11;} return 5;
	case 0xe8: if((s[rF] & FLAGV) != 0) {spc = pop16(); return 11;} return 5;
	case 0xf8: if((s[rF] & FLAGS) != 0) {spc = pop16(); return 11;} return 5;
	case 0xc9: spc = pop16(); return 10;
	case 0xd9:
		swap(rB);
		swap(rC);
		swap(rD);
		swap(rE);
		swap(rH);
		swap(rL);
		return 4;
	case 0xe9: spc = HL(); return 4;
	case 0xf9: sp = HL(); return 6;
	case 0xca: return jump((s[rF] & FLAGZ) != 0);
	case 0xda: return jump((s[rF] & FLAGC) != 0);
	case 0xea: return jump((s[rF] & FLAGV) != 0);
	case 0xfa: return jump((s[rF] & FLAGS) != 0);
	case 0xcb: return bits(-1);
	case 0xdb: s[rA] = z80in(fetch8()); return 11;
	case 0xeb:
		v = DE();
		s[rD] = s[rH];
		s[rE] = s[rL];
		s[rH] = v >> 8;
		s[rL] = v;
		return 4;
	case 0xfb: intm |= 0xc0; return 4;
	case 0xcc: return call(fetch16(), (s[rF] & FLAGZ) != 0);
	case 0xdc: return call(fetch16(), (s[rF] & FLAGC) != 0);
	case 0xec: return call(fetch16(), (s[rF] & FLAGV) != 0);
	case 0xfc: return call(fetch16(), (s[rF] & FLAGS) != 0);
	case 0xcd: return call(fetch16(), 1);
	case 0xdd: return index(0);
	case 0xed: return ed();
	case 0xfd: return index(1);
	case 0xce: return alu(1, 8);
	case 0xde: return alu(3, 8);
	case 0xee: return alu(5, 8);
	case 0xfe: return alu(7, 8);
	case 0xcf: return call(0x08, 1);
	case 0xdf: return call(0x18, 1);
	case 0xef: return call(0x28, 1);
	case 0xff: return call(0x38, 1);
	}
	sysfatal("undefined z80 opcode %#.2x at pc=%#.4x", op, scurpc);
	return 0;
}
