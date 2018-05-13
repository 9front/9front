#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

u8int r[8], ime;
u16int pc, curpc, sp;
int halt, trace;

enum {
	FLAGC = 0x10,
	FLAGH = 0x20,
	FLAGN = 0x40,
	FLAGZ = 0x80
};
enum { rB, rC, rD, rE, rH, rL, rHL, rA, rF = rHL };
#define BC() (r[rB] << 8 | r[rC])
#define DE() (r[rD] << 8 | r[rE])
#define HL() (r[rH] << 8 | r[rL])

Var cpuvars[] = { ARR(r), VAR(ime), VAR(pc), VAR(curpc), VAR(sp), VAR(halt), {nil, 0, 0} };

static u8int
fetch8(void)
{
	return memread(pc++);
}

static u16int
fetch16(void)
{
	u16int u;
	
	u = memread(pc++);
	return u | memread(pc++) << 8;
}

static void
push8(u8int u)
{
	memwrite(--sp, u);
}

static void
push16(u16int u)
{
	memwrite(--sp, u >> 8);
	memwrite(--sp, u);
}

static u8int
pop8(void)
{
	return memread(sp++);
}

static u16int
pop16(void)
{
	u16int v;
	
	v = memread(sp++);
	return v | memread(sp++) << 8;
}

static u16int
read16(u16int n)
{
	return memread(n) | memread(n+1) << 8;
}

static void
write16(u16int n, u16int v)
{
	memwrite(n++, v);
	memwrite(n, v >> 8);
}

static int
move(u8int dst, u8int src)
{
	if(dst == rHL){
		if(src == rHL){
			halt = 1;
			return 4;
		}
		memwrite(HL(), r[src]);
		return 8;
	}
	if(src == rHL){
		r[dst] = memread(HL());
		return 8;
	}
	r[dst] = r[src];
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
	case 8: u = fetch8(); t = 8; break;
	case rHL:
		u = memread(HL());
		t = 8;
		break;
	default:
		u = r[n];
		t = 4;
	}
	v4 = 0;
	switch(op){
	default:
		v4 = (r[rA] & 0x0f) + (u & 0x0f);
		v = r[rA] + u;
		break;
	case 1:
		v4 = (r[rA] & 0x0f) + (u & 0x0f) + (r[rF] >> 4 & 1);
		v = r[rA] + u + (r[rF] >> 4 & 1);
		break;
	case 2:
	case 7:
		v4 = (r[rA] & 0x0f) + (~u & 0x0f) + 1;
		v = r[rA] + (u ^ 0xff) + 1;
		break;
	case 3:
		v4 = (r[rA] & 0x0f) + (~u & 0x0f) + (~r[rF] >> 4 & 1);
		v = r[rA] + (u ^ 0xff) + (~r[rF] >> 4 & 1);
		break;
	case 4: v = r[rA] & u; break;
	case 5: v = r[rA] ^ u; break;
	case 6: v = r[rA] | u; break;
	}
	r[rF] = 0;
	if((u8int)v == 0)
		r[rF] |= FLAGZ;
	if(op < 2){
		if((v & 0x100) != 0)
			r[rF] |= FLAGC;
		if((v4 & 0x10) != 0)
			r[rF] |= FLAGH;
	}else if(op < 4 || op == 7){
		r[rF] |= FLAGN;
		if((v & 0x100) == 0)
			r[rF] |= FLAGC;
		if((v4 & 0x10) == 0)
			r[rF] |= FLAGH;
	}else
		if(op == 4)
			r[rF] |= FLAGH;
	if(op != 7)
		r[rA] = v;
	return t;
}

static int
branch(int cc, int t)
{
	u16int v;
	
	v = (s8int)fetch8();
	if(!cc)
		return t + 8;
	pc += v;
	return t + 12;
}

static u8int
inc(u8int v)
{
	r[rF] &= FLAGC;
	++v;
	if(v == 0)
		r[rF] |= FLAGZ;
	if((v & 0xf) == 0)
		r[rF] |= FLAGH;
	return v;
}

static u8int
dec(u8int v)
{
	--v;
	r[rF] = r[rF] & FLAGC | FLAGN;
	if(v == 0)
		r[rF] |= FLAGZ;
	if((v & 0xf) == 0xf)
		r[rF] |= FLAGH;
	return v;
}

static int
addhl(u16int u)
{
	u32int v;
	
	r[rF] &= ~(FLAGN|FLAGC|FLAGH);
	v = HL() + u;
	if((v & 0x10000) != 0)
		r[rF] |= FLAGC;
	if((HL() & 0xfff) + (u & 0xfff) >= 0x1000)
		r[rF] |= FLAGH;
	r[rL] = v;
	r[rH] = v >> 8;
	return 8;
}

static void
adchl(u16int u)
{
	u32int v, v4;
	
	v = HL() + u + (r[rF] & FLAGC);
	v4 = (HL() & 0xfff) + (u & 0xfff) + (r[rF] & FLAGC);
	r[rF] = 0;
	if((v & 0x10000) != 0)
		r[rF] |= FLAGC;
	if((v4 & 0x1000) != 0)
		r[rF] |= FLAGH;
	if((u16int)v == 0)
		r[rF] |= FLAGZ;
	r[rL] = v;
	r[rH] = v >> 8;
}

static void
sbchl(u16int u)
{
	u32int v, v4;
	
	v = HL() + (u16int)~u + (~r[rF] & FLAGC);
	v4 = (HL() & 0xfff) + (~u & 0xfff) + (~r[rF] & FLAGC);
	r[rF] = FLAGN;
	if((v & 0x10000) == 0)
		r[rF] |= FLAGC;
	if((v4 & 0x1000) == 0)
		r[rF] |= FLAGH;
	if((u16int)v == 0)
		r[rF] |= FLAGZ;
	r[rL] = v;
	r[rH] = v >> 8;
}

static int
jump(int cc)
{
	u16int v;
	
	v = fetch16();
	if(!cc)
		return 12;
	pc = v;
	return 16;
}

static int
call(u16int a, int cc)
{
	if(!cc)
		return 12;
	push16(pc);
	pc = a;
	return cc < 0 ? 16 : 24;
}

static int
bits(void)
{
	u8int op, v, n, m, c;
	u16int a;
	int t;
	
	op = fetch8();
	n = op & 7;
	m = op >> 3 & 7;
	a = HL();
	if(n == 6){
		v = memread(a);
		t = 16;
	}else{
		v = r[n];
		t = 8;
	}
	switch(op >> 6){
	case 0:
		c = r[rF] >> 4 & 1;
		switch(m){
		default: r[rF] = v >> 3 & 0x10; v = v << 1 | v >> 7; break;
		case 1:  r[rF] = v << 4 & 0x10; v = v >> 1 | v << 7; break;
		case 2:  r[rF] = v >> 3 & 0x10; v = v << 1 | c; break;
		case 3:  r[rF] = v << 4 & 0x10; v = v >> 1 | c << 7; break;
		case 4:  r[rF] = v >> 3 & 0x10; v = v << 1; break;
		case 5:  r[rF] = v << 4 & 0x10; v = v & 0x80 | v >> 1; break;
		case 6:  r[rF] = 0; v = v << 4 | v >> 4; break;
		case 7:  r[rF] = v << 4 & 0x10; v >>= 1; break;
		}
		if(v == 0)
			r[rF] |= FLAGZ;
		break;
	case 1:
		r[rF] = r[rF] & ~(FLAGN|FLAGZ) | FLAGH;
		if((v & 1<<m) == 0)
			r[rF] |= FLAGZ;
		if(n == 6)
			t = 12;
		return t;
	case 2:
		v &= ~(1<<m);
		break;
	case 3:
		v |= (1<<m);
	}
	if(n == 6)
		memwrite(a, v);
	else
		r[n] = v;
	return t;
}

void
reset(void)
{
	r[rA] = 0x01;
	r[rF] = 0xb0;
	r[rC] = 0x13;
	r[rE] = 0xd8;
	r[rL] = 0x4d;
	r[rH] = 0x01;
	if((mode & COL) == COL)
		r[rA] = 0x11;
	sp = 0xfffe;
	pc = 0x100;
}

int
step(void)
{
	u8int op, v4;
	u16int v, w;
	s8int s;

	if(halt)
		if((reg[IF] & reg[IE]) != 0)
			halt = 0;
		else
			return 4;
	if((reg[IF] & reg[IE]) != 0 && ime != 0){
		push16(pc);
		ime = 0;
		v4 = reg[IF] & reg[IE];
		v4 &= -v4;
		reg[IF] &= ~v4;
		for(pc = 0x40; v4 != 1; pc += 8)
			v4 >>= 1;
		return 12;
	}
	curpc = pc;
	op = fetch8();
	if(trace)
		print("%.4x %.2x AF %.2x%.2x BC %.2x%.2x DE %.2x%.2x HL %.2x%.2x SP %.4x\n", curpc, op, r[rA], r[rF], r[rB], r[rC], r[rD], r[rE], r[rH], r[rL], sp);
	switch(op >> 6){
	case 1: return move(op >> 3 & 7, op & 7);
	case 2: return alu(op >> 3 & 7, op & 7);
	}
	switch(op){
	case 0x00: return 4;
	case 0x10:
		if((mode & CGB) != 0 && (reg[KEY1] & 1) != 0){
			reg[DIV] += divclock - clock >> 7 - ((mode & TURBO) != 0);
			divclock = clock;
			mode ^= TURBO;
			timertac(reg[TAC], 1);
			reg[KEY1] ^= 0x81;
			return 4;
		}
		print("STOP ignored (pc=%.4ux)\n", curpc);
		return 4;
	case 0x20: return branch((r[rF] & FLAGZ) == 0, 0);
	case 0x30: return branch((r[rF] & FLAGC) == 0, 0);
	case 0x01: r[rC] = fetch8(); r[rB] = fetch8(); return 12;
	case 0x11: r[rE] = fetch8(); r[rD] = fetch8(); return 12;
	case 0x21: r[rL] = fetch8(); r[rH] = fetch8(); return 12;
	case 0x31: sp = fetch16(); return 12;
	case 0x02: memwrite(BC(), r[rA]); return 8;
	case 0x12: memwrite(DE(), r[rA]); return 8;
	case 0x22: memwrite(HL(), r[rA]); if(++r[rL] == 0) r[rH]++; return 8;
	case 0x32: memwrite(HL(), r[rA]); if(r[rL]-- == 0) r[rH]--; return 8;
	case 0x03: if(++r[rC] == 0) r[rB]++; return 8;
	case 0x13: if(++r[rE] == 0) r[rD]++; return 8;
	case 0x23: if(++r[rL] == 0) r[rH]++; return 8;
	case 0x33: sp++; return 8;
	case 0x04: inc(r[rB]++); return 4;
	case 0x14: inc(r[rD]++); return 4;
	case 0x24: inc(r[rH]++); return 4;
	case 0x34: memwrite(HL(), inc(memread(HL()))); return 12;
	case 0x05: dec(r[rB]--); return 4;
	case 0x15: dec(r[rD]--); return 4;
	case 0x25: dec(r[rH]--); return 4;
	case 0x35: memwrite(HL(), dec(memread(HL()))); return 12;
	case 0x06: r[rB] = fetch8(); return 8;
	case 0x16: r[rD] = fetch8(); return 8;
	case 0x26: r[rH] = fetch8(); return 8;
	case 0x36: memwrite(HL(), fetch8()); return 12;
	case 0x07:
		r[rF] = r[rA] >> 3 & 0x10;
		r[rA] = r[rA] << 1 | r[rA] >> 7;
		return 4;
	case 0x17:
		v = r[rF] >> 4 & 1;
		r[rF] = r[rA] >> 3 & 0x10;
		r[rA] = r[rA] << 1 | v;	
		return 4;
	case 0x27:
		if(r[rA] > 0x99 && (r[rF] & FLAGN) == 0 || (r[rF] & FLAGC) != 0){
			r[rF] |= FLAGC;
			v = 0x60;
		}else{
			r[rF] &= ~FLAGC;
			v = 0;
		}
		if((r[rA] & 0xf) > 9 && (r[rF] & FLAGN) == 0 || (r[rF] & FLAGH) != 0)
			v |= 6;
		if((r[rF] & FLAGN) != 0)
			r[rA] -= v;
		else
			r[rA] += v;
		r[rF] &= ~(FLAGZ|FLAGH);
		if(r[rA] == 0)
			r[rF] |= FLAGZ;
		return 4;
	case 0x37: r[rF] = r[rF] & ~(FLAGN | FLAGH) | FLAGC; return 4;
	case 0x08: write16(fetch16(), sp); return 20;
	case 0x18: return branch(1, 0);
	case 0x28: return branch((r[rF] & FLAGZ) != 0, 0);
	case 0x38: return branch((r[rF] & FLAGC) != 0, 0);
	case 0x09: return addhl(BC());
	case 0x19: return addhl(DE());
	case 0x29: return addhl(HL());
	case 0x39: return addhl(sp);
	case 0x0a: r[rA] = memread(BC()); return 8;
	case 0x1a: r[rA] = memread(DE()); return 8;
	case 0x2a: r[rA] = memread(HL()); if(++r[rL] == 0) r[rH]++; return 8;
	case 0x3a: r[rA] = memread(HL()); if(r[rL]-- == 0) r[rH]--; return 8;
	case 0x0b: if(r[rC]-- == 0) r[rB]--; return 8;
	case 0x1b: if(r[rE]-- == 0) r[rD]--; return 8;
	case 0x2b: if(r[rL]-- == 0) r[rH]--; return 8;
	case 0x3b: sp--; return 8;
	case 0x0c: inc(r[rC]++); return 4;
	case 0x1c: inc(r[rE]++); return 4;
	case 0x2c: inc(r[rL]++); return 4;
	case 0x3c: inc(r[rA]++); return 4;
	case 0x0d: dec(r[rC]--); return 4;
	case 0x1d: dec(r[rE]--); return 4;
	case 0x2d: dec(r[rL]--); return 4;
	case 0x3d: dec(r[rA]--); return 4;
	case 0x0e: r[rC] = fetch8(); return 8;
	case 0x1e: r[rE] = fetch8(); return 8;
	case 0x2e: r[rL] = fetch8(); return 8;
	case 0x3e: r[rA] = fetch8(); return 8;
	case 0x0f:
		r[rF] = r[rA] << 4 & 0x10;
		r[rA] = r[rA] >> 1 | r[rA] << 7;
		return 4;
	case 0x1f:
		v = r[rF] << 3 & 0x80;
		r[rF] = r[rA] << 4 & 0x10;
		r[rA] = r[rA] >> 1 | v;
		return 4;
	case 0x2f:
		r[rF] |= FLAGN|FLAGH;
		r[rA] ^= 0xff;
		return 4;
	case 0x3f:
		r[rF] = r[rF] & ~(FLAGN|FLAGH) ^ FLAGC;
		return 4;
	case 0xc0: if((r[rF] & FLAGZ) == 0) {pc = pop16(); return 20;} return 8;
	case 0xd0: if((r[rF] & FLAGC) == 0) {pc = pop16(); return 20;} return 8;
	case 0xe0: memwrite(0xff00 | fetch8(), r[rA]); return 12;
	case 0xf0: r[rA] = memread(0xff00 | fetch8()); return 12;
	case 0xc1: r[rC] = pop8(); r[rB] = pop8(); return 12;
	case 0xd1: r[rE] = pop8(); r[rD] = pop8(); return 12;
	case 0xe1: r[rL] = pop8(); r[rH] = pop8(); return 12;
	case 0xf1: r[rF] = pop8() & 0xf0; r[rA] = pop8(); return 12;
	case 0xc2: return jump((r[rF] & FLAGZ) == 0);
	case 0xd2: return jump((r[rF] & FLAGC) == 0);
	case 0xe2: memwrite(0xff00 | r[rC], r[rA]); return 8;
	case 0xf2: r[rA] = memread(0xff00 | r[rC]); return 8;
	case 0xc3: return jump(1);
	case 0xf3: ime = 0; return 4;
	case 0xc4: return call(fetch16(), (r[rF] & FLAGZ) == 0);
	case 0xd4: return call(fetch16(), (r[rF] & FLAGC) == 0);
	case 0xc5: push8(r[rB]); push8(r[rC]); return 16;
	case 0xd5: push8(r[rD]); push8(r[rE]); return 16;
	case 0xe5: push8(r[rH]); push8(r[rL]); return 16;
	case 0xf5: push8(r[rA]); push8(r[rF]); return 16;
	case 0xc6: return alu(0, 8);
	case 0xd6: return alu(2, 8);
	case 0xe6: return alu(4, 8);
	case 0xf6: return alu(6, 8);
	case 0xc7: return call(0x00, -1);
	case 0xd7: return call(0x10, -1);
	case 0xe7: return call(0x20, -1);
	case 0xf7: return call(0x30, -1);
	case 0xc8: if((r[rF] & FLAGZ) != 0) {pc = pop16(); return 20;} return 8;
	case 0xd8: if((r[rF] & FLAGC) != 0) {pc = pop16(); return 20;} return 8;
	case 0xe8: case 0xf8:
		s = fetch8();
		v = sp + s;
		v4 = (sp & 0xf) + (s & 0xf);
		w = (sp & 0xff) + (s & 0xff);
		r[rF] = 0;
		if(v4 >= 0x10)
			r[rF] |= FLAGH;
		if(w >= 0x100)
			r[rF] |= FLAGC;	
		if(op == 0xe8){
			sp = v;
			return 16;
		}else{
			r[rL] = v;
			r[rH] = v >> 8;
			return 12;
		}
	case 0xc9: pc = pop16(); return 16;
	case 0xd9: pc = pop16(); ime = 1; return 16;
	case 0xe9: pc = HL(); return 4;
	case 0xf9: sp = HL(); return 8;
	case 0xca: return jump((r[rF] & FLAGZ) != 0);
	case 0xda: return jump((r[rF] & FLAGC) != 0);
	case 0xea: memwrite(fetch16(), r[rA]); return 16;
	case 0xfa: r[rA] = memread(fetch16()); return 16;
	case 0xcb: return bits();
	case 0xfb: ime = 1; return 4;
	case 0xcc: return call(fetch16(), (r[rF] & FLAGZ) != 0);
	case 0xdc: return call(fetch16(), (r[rF] & FLAGC) != 0);
	case 0xcd: return call(fetch16(), 1);
	case 0xce: return alu(1, 8);
	case 0xde: return alu(3, 8);
	case 0xee: return alu(5, 8);
	case 0xfe: return alu(7, 8);
	case 0xcf: return call(0x08, -1);
	case 0xdf: return call(0x18, -1);
	case 0xef: return call(0x28, -1);
	case 0xff: return call(0x38, -1);
	}
	sysfatal("undefined opcode %#.2x at pc=%#.4x", op, curpc);
	return 0;
}
