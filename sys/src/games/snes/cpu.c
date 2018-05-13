#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

u8int rP, emu, irq, nmi, dma, wai;
u16int rA, rX, rY, rS, rD, pc;
u32int rDB, rPB, curpc, hdma;
static u8int m8, x8;
int cyc;
static u32int lastpc;
#define io() cyc += 6

static void
ioirq(void)
{
	if(irq && (rP & FLAGI) == 0)
		memread(pc | rPB);
	else
		io();
}

static u8int
fetch8(void)
{
	return memread(pc++ | rPB);
}

static u16int
fetch16(void)
{
	u16int r;
	
	r = memread(pc++ | rPB);
	r |= memread(pc++ | rPB) << 8;
	return r;
}

static u16int
mem16(u32int a)
{
	u16int r;

	r = memread(a++);
	r |= memread(a) << 8;
	return r;
}

static u16int
mem816(u32int a, u16int v)
{
	if(m8)
		return memread(a) | v;
	return mem16(a);
}

static u16int
memx816(u32int a)
{
	if(x8)
		return memread(a);
	return mem16(a);
}


static void
memw816(u32int a, u16int v)
{
	memwrite(a, v);
	if(m8)
		return;
	memwrite(++a, v >> 8);
}

static void
memwx816(u32int a, u16int v)
{
	memwrite(a, v);
	if(x8)
		return;
	memwrite(++a, v >> 8);
}

static void
push8(u8int a)
{
	memwrite(rS, a);
	if(emu && (rS & 0xFF) == 0)
		rS |= 0xFF;
	else
		rS--;
}

static u8int
pop8(void)
{
	if(emu && (rS & 0xFF) == 0xFF)
		rS &= ~0xFF;
	else
		rS++;
	return memread(rS);
}

static void
push16(u16int a)
{
	push8(a >> 8);
	push8(a);
}

static u16int
pop16(void)
{
	u16int r;
	
	r = pop8();
	r |= pop8() << 8;
	return r;
}

static void
push816(u16int v, int m)
{
	if(!m)
		push8(v >> 8);
	push8(v);
}

static u16int
pop816(u16int a, int m)
{
	u16int r;

	r = pop8();
	if(m)
		return r | a;
	r |= pop8() << 8;
	return r;
}

static u16int
nz8(u16int v)
{
	rP &= ~(FLAGN | FLAGZ);
	if((v & 0xFF) == 0)
		rP |= FLAGZ;
	rP |= (v & 0x80);
	return v;
}

static u16int
nz16(u16int v)
{
	rP &= ~(FLAGN | FLAGZ);
	if(v == 0)
		rP |= FLAGZ;
	if((v & 0x8000) != 0)
		rP |= FLAGN;
	return v;
}

static u16int
nz(u16int v)
{
	if(m8)
		return nz8(v);
	return nz16(v);
}

static u16int
nzx(u16int v)
{
	if(x8)
		return nz8(v);
	return nz16(v);
}

static u16int
imm(int a)
{
	if(m8)
		return fetch8() | a;
	return fetch16();
}

static u16int
immx(int a)
{
	if(x8)
		return fetch8() | a;
	return fetch16();
}

static u32int
abso(int l, int x)
{
	u32int p;
	
	p = fetch16();
	if(l)
		p |= fetch8() << 16;
	else
		p |= rDB;
	switch(x){
	case 1: p += rX; break;
	case 2: p += rY; break;
	}
	return p;
}

static u32int
absi(int x)
{
	u16int p;
	u32int b, r;
	
	p = fetch16();
	if(x){
		p += rX;
		b = rPB;
		io();
	}else
		b = 0;
	r = memread(p++ | b);
	r |= memread(p | b) << 8;
	return r;
}

static u32int
dp(int x)
{
	u32int p;

	p = fetch8();
	switch(x){
	case 1: p += rX; io(); break;
	case 2: p += rY; io(); break;
	}
	if((rD & 0xFF) != 0)
		io();
	else if(emu)
		return rD & 0xFF00 | p & 0xFF;
	p = (p + rD) & 0xFFFF;
	return p;
}

static u32int
dpi(int l, int x, int y)
{
	u32int p, r, s;
	u32int b;
	
	p = dp(x);
	r = memread(p++);
	if(emu && (rD & 0xFF) == 0){
		if((p & 0xFF) == 0)
			p -= 0x100;
	}else
		p &= 0xFFFF;
	r |= memread(p++) << 8;
	if(l){
		if(emu && (rD & 0xFF) == 0){
			if((p & 0xFF) == 0)
				p -= 0x100;
		}else
			p &= 0xFFFF;
		b = memread(p) << 16;
	}else
		b = rDB;
	if(y){
		s = r + rY;
		if(x8 && ((r ^ s) & 0xFF00) != 0)
			io();
		r = s;
	}
	r += b;
	return r;
}

static u32int
sr(void)
{
	u8int d;
	
	d = fetch8();
	io();
	return (rS + d) & 0xFFFF;
}

static u32int
sry(void)
{
	u8int d;
	u32int a;
	
	d = fetch8();
	io();
	a = (mem16((rS + d) & 0xFFFF) | rDB) + rY;
	io();
	return a;
}

static void
rmw(u32int a, u16int, u16int w)
{
	io();
	memw816(a, w);
	nz(w);
}

static void
branch(int c)
{
	signed char t;
	u16int npc;
	
	t = fetch8();
	if(!c)
		return;
	npc = pc + t;
	io();
	if(emu && (npc ^ pc) >> 8)
		io();
	pc = npc;
}

static void
setrp(u8int v)
{
	if(emu)
		v |= 0x30;
	else if((v & 0x10) != 0){
		rX &= 0xff;
		rY &= 0xff;
	}
	rP = v;
}

static void
adc(u16int a)
{
	int r;

	if(m8){
		if((rP & FLAGD) != 0){
			r = (rA & 0xf) + (a & 0xf) + (rP & FLAGC);
			if(r > 0x09)
				r += 0x06;
			if(r > 0x1f)
				r -= 0x10;
			r += (rA & 0xf0) + (a & 0xf0);
		}else
			r = (rA & 0xff) + a + (rP & FLAGC);
		rP &= ~(FLAGC | FLAGN | FLAGV | FLAGZ);
		if((~(rA ^ a) & (rA ^ r)) & 0x80)
			rP |= FLAGV;
		if((rP & FLAGD) != 0 && r > 0x9f)
			r += 0x60;
		if(r > 0xFF)
			rP |= FLAGC;
		rP |= r & 0x80;
		r &= 0xFF;
		if(r == 0)
			rP |= FLAGZ;
		rA = rA & 0xFF00 | r;
	}else{
		if((rP & FLAGD) != 0){
			r  = (rA & 0x000f) + (a & 0x000f) + (rP & FLAGC);
			if(r > 0x0009) r += 0x0006;
			if(r > 0x001f) r -= 0x0010;
			r += (rA & 0x00f0) + (a & 0x00f0);
			if(r > 0x009f) r += 0x0060;
			if(r > 0x01ff) r -= 0x0100;
			r += (rA & 0x0f00) + (a & 0x0f00);
			if(r > 0x09ff) r += 0x0600;
			if(r > 0x1fff) r -= 0x1000;
			r += (rA & 0xf000) + (a & 0xf000);
		}else
			r = rA + a + (rP & FLAGC);
		rP &= ~(FLAGC | FLAGN | FLAGV | FLAGZ);
		if((~(rA ^ a) & (rA ^ r)) & 0x8000)
			rP |= FLAGV;
		if((rP & FLAGD) != 0 && r > 0x9fff)
			r += 0x6000;
		if(r > 0xFFFF)
			rP |= FLAGC;
		if((r & 0x8000) != 0)
			rP |= FLAGN;
		rA = r;
		if(rA == 0)
			rP |= FLAGZ;
	}
}

static void
asl(u32int a)
{
	u16int v;

	v = mem816(a, 0);
	rP &= ~FLAGC;
	rP |= v >> (m8 ? 7 : 15);
	rmw(a, v, v << 1);
}

static void
bit(u16int a)
{
	rP &= ~(FLAGN | FLAGZ | FLAGV);
	if((a & rA) == 0)
		rP |= FLAGZ;
	if(m8)
		rP |= a & 0xC0;
	else
		rP |= (a >> 8) & 0xC0;
}

static void
block(int incr)
{
	u32int sb;
	
	rDB = fetch8() << 16;
	sb = fetch8() << 16;
	memwrite(rDB | rY, memread(sb | rX));
	if(incr){
		rX++;
		rY++;
	}else{
		rX--;
		rY--;
	}
	if(x8){
		rX &= 0xff;
		rY &= 0xff;
	}
	if(rA-- != 0)
		pc -= 3;
	io();
	io();
}

static void
cmp(u16int a, u16int b, int m)
{
	if(m){
		a &= 0xff;
		b &= 0xff;
	}
	rP &= ~(FLAGN | FLAGZ | FLAGC);
	if(a == b)
		rP |= FLAGZ;
	if(a >= b)
		rP |= FLAGC;
	if((a - b) & (m ? 0x80 : 0x8000))
		rP |= FLAGN;
}

static void
dec(u32int a)
{
	u16int v;
	
	v = mem816(a, 0);
	rmw(a, v, v-1);
}

static void
inc(u32int a)
{
	u16int v;

	v = mem816(a, 0);
	rmw(a, v, v+1);
}

static void
lsr(u32int a)
{
	u16int v;
	
	v = mem816(a, 0);
	rP &= ~FLAGC;
	rP |= v & 1;
	rmw(a, v, v>>1);
}

static void
rol(u32int a)
{
	u16int v, w;
	
	v = rP & FLAGC;
	w = mem816(a, 0);
	rP &= ~FLAGC;
	rP |= w >> (m8 ? 7 : 15);
	rmw(a, w, w<<1 | v);
}

static void
ror(u32int a)
{
	u16int v, w;
	
	v = (rP & FLAGC) << (m8 ? 7 : 15);
	w = mem816(a, 0);
	rP &= ~FLAGC;
	rP |= w & 1;
	rmw(a, w, w>>1 | v);
}

static void
sbc(u16int a)
{
	int r;

	if(m8){
		a ^= 0xff;
		if((rP & FLAGD) != 0){
			r = (rA & 0xf) + (a & 0xf) + (rP & FLAGC);
			if(r < 0x10) r -= 0x06;
			if(r < 0) r += 0x10;
			r += (rA & 0xf0) + (a & 0xf0);
		}else
			r = (rA & 0xff) + a + (rP & FLAGC);
		rP &= ~(FLAGC | FLAGN | FLAGV | FLAGZ);
		if((~(rA ^ a) & (rA ^ r)) & 0x80)
			rP |= FLAGV;
		if(r > 0xff)
			rP |= FLAGC;
		else if((rP & FLAGD) != 0)
			r -= 0x60;
		rP |= r & 0x80;
		r &= 0xFF;
		if(r == 0)
			rP |= FLAGZ;
		rA = rA & 0xFF00 | r;
	}else{
		a ^= 0xffff;
		if((rP & FLAGD) != 0){
			r  = (rA & 0x000f) + (a & 0x000f) + (rP & FLAGC);
			if(r < 0x0010) r -= 0x0006;
			if(r < 0x0000) r += 0x0010;
			r += (rA & 0x00f0) + (a & 0x00f0);
			if(r < 0x0100) r -= 0x0060;
			if(r < 0x0000) r += 0x0100;
			r += (rA & 0x0f00) + (a & 0x0f00);
			if(r < 0x1000) r -= 0x0600;
			if(r < 0x0000) r += 0x1000;
			r += (rA & 0xf000) + (a & 0xf000);
		}else
			r = rA + a + (rP & FLAGC);
		rP &= ~(FLAGC | FLAGN | FLAGV | FLAGZ);
		if((~(rA ^ a) & (rA ^ r)) & 0x8000)
			rP |= FLAGV;	
		if(r > 0xFFFF)
			rP |= FLAGC;
		else if((rP & FLAGD) != 0)
			r -= 0x6000;
		if((r & 0x8000) != 0)
			rP |= FLAGN;
		rA = r;
		if(rA == 0)
			rP |= FLAGZ;
	}
}

static void
setra(u16int a)
{
	if(m8)
		rA = rA & 0xff00 | nz8(a & 0xff);
	else
		rA = nz16(a);
}

static void
setx(u16int a, u16int *b)
{
	if(x8)
		*b = nz8(a & 0xff);
	else
		*b = nz16(a);
}

static void
tsb(u32int a, int set)
{
	u16int v;

	v = mem816(a, 0);
	rP &= ~FLAGZ;
	if(m8){
		if((rA & v & 0xFF) == 0)
			rP |= FLAGZ;
	}else
		if((rA & v) == 0)
			rP |= FLAGZ;
	io();
	if(set)
		memw816(a, v | rA);
	else
		memw816(a, v & ~rA);
}

enum { COP = 0, BRK = 1, NMI = 3, IRQ = 5 };

static void
interrupt(int src)
{
	if(src > BRK)
		memread(pc | rPB);
	if(!emu)
		push8(rPB >> 16);
	push16(pc);
	if(emu && src != BRK)
		push8(rP & ~(1<<5));
	else
		push8(rP);
	if(emu && src == BRK)
		src = IRQ;
	pc = mem16(0xffe4 + src * 2 + emu * 0x10);
	rP |= FLAGI;
	rP &= ~FLAGD;
	rPB = 0;
	if(emu)
		rDB = 0;
	wai = 0;
}

void
cpureset(void)
{
	pc = mem16(0xfffc);
	rD = 0;
	rDB = 0;
	rPB = 0;
	rS = 0x100;
	rP = 0x35;
}

int
cpustep(void)
{
	u8int op;
	int a;
	static int cnt;

	cyc = 0;
	if((hdma & 0xffff) != 0){
		curpc = -1;
		return hdmastep();
	}
	if(dma){
		curpc = -1;
		return dmastep();
	}
	if(nmi)
		if(--nmi == 0){
			interrupt(NMI);
			return cyc;
		}
	if(irq && (rP & FLAGI) == 0){
		interrupt(IRQ);
		return cyc;
	}
	curpc = pc|rPB;
	if(wai){
		io();
		if(irq){
			wai = 0;
			io();
		}else
			return cyc;
	}
	m8 = (rP & FLAGM) != 0;
	x8 = (rP & FLAGX) != 0;
	op = fetch8();
	if(op == 0)
		print("BRK PC=%.6x from PC=%.6x\n", curpc, lastpc);
	lastpc = curpc;
	if(trace)
		print("%.6x %.2x A=%.4x X=%.4x Y=%.4x P=%.2x %.2x\n", curpc, op, rA, rX, rY, rP, rS);
	switch(op){
	case 0x00: fetch8(); interrupt(BRK); break;
	case 0x01: nz(rA |= mem816(dpi(0, 1, 0), 0)); break;
	case 0x02: fetch8(); interrupt(COP); break;
	case 0x03: nz(rA |= mem816(sr(), 0)); break;
	case 0x04: tsb(dp(0), 1); break;
	case 0x05: nz(rA |= mem816(dp(0), 0)); break;
	case 0x06: asl(dp(0)); break;
	case 0x07: nz(rA |= mem816(dpi(1, 0, 0), 0)); break;
	case 0x08: io(); push8(rP); break;
	case 0x09: nz(rA |= imm(0)); break;
	case 0x0A:
		rP &= ~FLAGC;
		if(m8){
			rP |= (rA >> 7) & 1;
			rA = (rA & 0xFF00) | ((rA << 1) & 0xFF);
		}else{
			rP |= (rA >> 15) & 1;
			rA <<= 1;
		}
		nz(rA);
		ioirq();
		break;
	case 0x0B: io(); push16(rD); break;
	case 0x0C: tsb(abso(0, 0), 1); break;
	case 0x0D: nz(rA |= mem816(abso(0, 0), 0)); break;
	case 0x0E: asl(abso(0, 0)); break;
	case 0x0F: nz(rA |= mem816(abso(1, 0), 0)); break;
	case 0x10: branch((rP & FLAGN) == 0); break;
	case 0x11: nz(rA |= mem816(dpi(0, 0, 1), 0)); break;
	case 0x12: nz(rA |= mem816(dpi(0, 0, 0), 0)); break;
	case 0x13: nz(rA |= mem816(sry(), 0)); break;
	case 0x14: tsb(dp(0), 0); break;
	case 0x15: nz(rA |= mem816(dp(1), 0)); break;
	case 0x16: asl(dp(1)); break;
	case 0x17: nz(rA |= mem816(dpi(1, 0, 1), 0)); break;
	case 0x18: rP &= ~FLAGC; ioirq(); break;
	case 0x19: nz(rA |= mem816(abso(0, 2), 0)); break;
	case 0x1A:
		if(m8 && (rA & 0xFF) == 0xFF)
			rA &= ~0xFF;
		else
			rA++;
		nz(rA);
		ioirq();
		break;
	case 0x1B: rS = rA; if(emu) rS = rS & 0xff | 0x100; ioirq(); break;
	case 0x1C: tsb(abso(0, 0), 0); break;
	case 0x1D: nz(rA |= mem816(abso(0, 1), 0)); break;
	case 0x1E: asl(abso(0, 1)); break;
	case 0x1F: nz(rA |= mem816(abso(1, 1), 0)); break;
	case 0x20: a = fetch16(); io(); push16(pc-1); pc = a; break;
	case 0x21: nz(rA &= mem816(dpi(0, 1, 0), 0xFF00)); break;
	case 0x22: a = fetch16(); push8(rPB>>16); io(); rPB = fetch8()<<16; push16(pc-1); pc = a; break;
	case 0x23: nz(rA &= mem816(sr(), 0xFF00)); break;
	case 0x24: bit(mem816(dp(0), 0)); break;
	case 0x25: nz(rA &= mem816(dp(0), 0xFF00)); break;
	case 0x26: rol(dp(0)); break;
	case 0x27: nz(rA &= mem816(dpi(1, 0, 0), 0xFF00)); break;
	case 0x28: io(); io(); setrp(pop8()); break;
	case 0x29: nz(rA &= imm(0xFF00)); break;
	case 0x2A:
		a = rP & FLAGC;
		rP &= ~FLAGC;
		if(m8){
			rP |= (rA >> 7) & 1;
			rA = (rA & 0xFF00) | ((rA << 1) & 0xFF) | a;
		}else{
			rP |= (rA >> 15) & 1;
			rA = (rA << 1) | a;
		}
		nz(rA);
		ioirq();
		break;
	case 0x2B: io(); io(); nz16(rD = pop16()); break;
	case 0x2C: bit(mem816(abso(0, 0), 0)); break;
	case 0x2D: nz(rA &= mem816(abso(0, 0), 0xFF00)); break;
	case 0x2E: rol(abso(0, 0)); break;
	case 0x2F: nz(rA &= mem816(abso(1, 0), 0xFF00)); break;
	case 0x30: branch((rP & FLAGN) != 0); break;
	case 0x31: nz(rA &= mem816(dpi(0, 0, 1), 0xFF00)); break;
	case 0x32: nz(rA &= mem816(dpi(0, 0, 0), 0xFF00)); break;
	case 0x33: nz(rA &= mem816(sry(), 0xFF00)); break;
	case 0x34: bit(mem816(dp(1), 0)); break;
	case 0x35: nz(rA &= mem816(dp(1), 0xFF00)); break;
	case 0x36: rol(dp(1)); break;
	case 0x37: nz(rA &= mem816(dpi(1, 0, 1), 0xFF00)); break;
	case 0x38: rP |= FLAGC; ioirq(); break;
	case 0x39: nz(rA &= mem816(abso(0, 2), 0xFF00)); break;
	case 0x3A:
		if(m8 && (rA & 0xFF) == 0)
			rA |= 0xFF;
		else
			rA--;
		nz(rA);
		ioirq();
		break;
	case 0x3B: nz16(rA = rS); ioirq(); break;
	case 0x3C: bit(mem816(abso(0, 1), 0)); break;
	case 0x3D: nz(rA &= mem816(abso(0, 1), 0xFF00)); break;
	case 0x3E: rol(abso(0, 1)); break;
	case 0x3F: nz(rA &= mem816(abso(1, 1), 0xFF00)); break;
	case 0x40:
		io();
		io();
		setrp(pop8());
		pc = pop16();
		if(!emu)
			rPB = pop8() << 16;
		break;
	case 0x41: nz(rA ^= mem816(dpi(0, 1, 0), 0)); break;
	case 0x42: fetch8(); break;
	case 0x43: nz(rA ^= mem816(sr(), 0)); break;
	case 0x44: block(0); break;
	case 0x45: nz(rA ^= mem816(dp(0), 0)); break;
	case 0x46: lsr(dp(0)); break;
	case 0x47: nz(rA ^= mem816(dpi(1, 0, 0), 0)); break;
	case 0x48: io(); push816(rA, m8); break;
	case 0x49: nz(rA ^= imm(0)); break;
	case 0x4A:
		rP &= ~FLAGC;
		rP |= rA & 1;
		if(m8)
			rA = rA & 0xFF00 | (rA >> 1) & 0x7F;
		else
			rA >>= 1;
		nz(rA);
		ioirq();
		break;
	case 0x4B: io(); push8(rPB >> 16); break;
	case 0x4C: pc = fetch16(); break;
	case 0x4D: nz(rA ^= mem816(abso(0, 0), 0)); break;
	case 0x4E: lsr(abso(0, 0)); break;
	case 0x4F: nz(rA ^= mem816(abso(1, 0), 0)); break;
	case 0x50: branch((rP & FLAGV) == 0); break;
	case 0x51: nz(rA ^= mem816(dpi(0, 0, 1), 0)); break;
	case 0x52: nz(rA ^= mem816(dpi(0, 0, 0), 0)); break;
	case 0x53: nz(rA ^= mem816(sry(), 0)); break;
	case 0x54: block(1); break;
	case 0x55: nz(rA ^= mem816(dp(1), 0)); break;
	case 0x56: lsr(dp(1)); break;
	case 0x57: nz(rA ^= mem816(dpi(1, 0, 1), 0)); break;
	case 0x58: rP &= ~FLAGI; ioirq(); break;
	case 0x59: nz(rA ^= mem816(abso(0, 2), 0)); break;
	case 0x5A: io(); push816(rY, x8); break;
	case 0x5B: nz16(rD = rA); ioirq(); break;
	case 0x5C: a = fetch16(); rPB = fetch8() << 16; pc = a; break;
	case 0x5D: nz(rA ^= mem816(abso(0, 1), 0)); break;
	case 0x5E: lsr(abso(0, 1)); break;
	case 0x5F: nz(rA ^= mem816(abso(1, 1), 0)); break;
	case 0x60: io(); io(); pc = pop16() + 1; io(); break;
	case 0x61: adc(mem816(dpi(0, 1, 0), 0)); break;
	case 0x62: a = fetch16(); io(); push16(a + pc); break;
	case 0x63: adc(mem816(sr(), 0)); break;
	case 0x64: memw816(dp(0), 0); break;
	case 0x65: adc(mem816(dp(0), 0)); break;
	case 0x66: ror(dp(0)); break;
	case 0x67: adc(mem816(dpi(1, 0, 0), 0)); break;
	case 0x68: nz(rA = pop816(rA & 0xFF00, m8)); break;
	case 0x69: adc(imm(0)); break;
	case 0x6A:
		a = rP & FLAGC;
		rP &= ~FLAGC;
		rP |= rA & 1;
		if(m8)
			rA = rA & 0xFF00 | (rA >> 1) & 0x7F | a << 7;
		else
			rA = rA >> 1 | a << 15;
		nz(rA);
		ioirq();
		break;
	case 0x6B: io(); io(); pc = pop16() + 1; rPB = pop8() << 16; break;
	case 0x6C: pc = absi(0); break;
	case 0x6D: adc(mem816(abso(0, 0), 0)); break;
	case 0x6E: ror(abso(0, 0)); break;
	case 0x6F: adc(mem816(abso(1, 0), 0)); break;
	case 0x70: branch((rP & FLAGV) != 0); break;
	case 0x71: adc(mem816(dpi(0, 0, 1), 0)); break;
	case 0x72: adc(mem816(dpi(0, 0, 0), 0)); break;
	case 0x73: adc(mem816(sry(), 0)); break;
	case 0x74: memw816(dp(1), 0); break;
	case 0x75: adc(mem816(dp(1), 0)); break;
	case 0x76: ror(dp(1)); break;
	case 0x77: adc(mem816(dpi(1, 0, 1), 0)); break;
	case 0x78: rP |= FLAGI; io(); break;
	case 0x79: adc(mem816(abso(0, 2), 0)); break;
	case 0x7A: io(); io(); nzx(rY = pop816(0, x8)); break;
	case 0x7B: nz16(rA = rD); ioirq(); break;
	case 0x7C: pc = absi(1); break;
	case 0x7D: adc(mem816(abso(0, 1), 0)); break;
	case 0x7E: ror(abso(0, 1)); break;
	case 0x7F: adc(mem816(abso(1, 1), 0)); break;
	case 0x80: branch(1); break;
	case 0x81: memw816(dpi(0, 1, 0), rA); break;
	case 0x82: a = fetch16(); io(); pc += a; break;
	case 0x83: memw816(sr(), rA); break;
	case 0x84: memwx816(dp(0), rY); break;
	case 0x85: memw816(dp(0), rA); break;
	case 0x86: memwx816(dp(0), rX); break;
	case 0x87: memw816(dpi(1, 0, 0), rA); break;
	case 0x88: 
		rY--;
		if(x8)
			rY &= 0xff;
		nzx(rY);
		ioirq();
		break;
	case 0x89:
		rP &= ~FLAGZ;
		if((imm(0) & rA) == 0)
			rP |= FLAGZ;
		break;
	case 0x8A: setra(rX); ioirq(); break;
	case 0x8B: io(); push8(rDB >> 16); break;
	case 0x8C: memwx816(abso(0, 0), rY); break;
	case 0x8D: memw816(abso(0, 0), rA); break;
	case 0x8E: memwx816(abso(0, 0), rX); break;
	case 0x8F: memw816(abso(1, 0), rA); break;
	case 0x90: branch((rP & FLAGC) == 0); break;
	case 0x91: memw816(dpi(0, 0, 1), rA); break;
	case 0x92: memw816(dpi(0, 0, 0), rA); break;
	case 0x93: memw816(sry(), rA); break;
	case 0x94: memwx816(dp(1), rY); break;
	case 0x95: memw816(dp(1), rA); break;
	case 0x96: memwx816(dp(2), rX); break;
	case 0x97: memw816(dpi(1, 0, 1), rA); break;
	case 0x98: setra(rY); ioirq(); break;
	case 0x99: memw816(abso(0, 2), rA); break;
	case 0x9A: rS = rX; if(emu) rS = rS & 0xff | 0x100; ioirq(); break;
	case 0x9B: setx(rX, &rY); ioirq(); break;
	case 0x9C: memw816(abso(0, 0), 0); break;
	case 0x9D: memw816(abso(0, 1), rA); break;
	case 0x9E: memw816(abso(0, 1), 0); break;
	case 0x9F: memw816(abso(1, 1), rA); break;
	case 0xA0: nzx(rY = immx(0)); break;
	case 0xA1: nz(rA = mem816(dpi(0, 1, 0), rA & 0xFF00)); break;
	case 0xA2: nzx(rX = immx(0)); break;
	case 0xA3: nz(rA = mem816(sr(), rA & 0xFF00)); break;
	case 0xA4: nzx(rY = memx816(dp(0))); break;
	case 0xA5: nz(rA = mem816(dp(0), rA & 0xFF00)); break;
	case 0xA6: nzx(rX = memx816(dp(0))); break;
	case 0xA7: nz(rA = mem816(dpi(1, 0, 0), rA & 0xFF00)); break;
	case 0xA8: setx(rA, &rY); ioirq(); break;
	case 0xA9: nz(rA = imm(rA & 0xFF00)); break;
	case 0xAA: setx(rA, &rX); ioirq(); break;
	case 0xAB: io(); io(); rDB = nz8(pop8()) << 16; break;
	case 0xAC: nzx(rY = memx816(abso(0, 0))); break;
	case 0xAD: nz(rA = mem816(abso(0, 0), rA & 0xFF00)); break;
	case 0xAE: nzx(rX = memx816(abso(0, 0))); break;
	case 0xAF: nz(rA = mem816(abso(1, 0), rA & 0xFF00)); break;
	case 0xB0: branch((rP & FLAGC) != 0); break;
	case 0xB1: nz(rA = mem816(dpi(0, 0, 1), rA & 0xFF00)); break;
	case 0xB2: nz(rA = mem816(dpi(0, 0, 0), rA & 0xFF00)); break;
	case 0xB3: nz(rA = mem816(sry(), rA & 0xFF00)); break;
	case 0xB4: nzx(rY = memx816(dp(1))); break;
	case 0xB5: nz(rA = mem816(dp(1), rA & 0xFF00)); break;
	case 0xB6: nzx(rX = memx816(dp(2))); break;
	case 0xB7: nz(rA = mem816(dpi(1, 0, 1), rA & 0xFF00)); break;
	case 0xB8: rP &= ~FLAGV; ioirq(); break;
	case 0xB9: nz(rA = mem816(abso(0, 2), rA & 0xFF00)); break;
	case 0xBA: setx(rS, &rX); ioirq(); break;
	case 0xBB: setx(rY, &rX); ioirq(); break;
	case 0xBC: nzx(rY = memx816(abso(0, 1))); break;
	case 0xBD: nz(rA = mem816(abso(0, 1), rA & 0xFF00)); break;
	case 0xBE: nzx(rX = memx816(abso(0, 2))); break;
	case 0xBF: nz(rA = mem816(abso(1, 1), rA & 0xFF00)); break;
	case 0xC0: cmp(rY, immx(0), x8); break;
	case 0xC1: cmp(rA, mem816(dpi(0, 1, 0), 0), m8); break;
	case 0xC2: setrp(rP & ~fetch8()); io(); break;
	case 0xC3: cmp(rA, mem816(sr(), 0), m8); break;
	case 0xC4: cmp(rY, memx816(dp(0)), x8); break;
	case 0xC5: cmp(rA, mem816(dp(0), 0), m8); break;
	case 0xC6: dec(dp(0)); break;
	case 0xC7: cmp(rA, mem816(dpi(1, 0, 0), 0), m8); break;
	case 0xC8: 
		rY++;
		if(x8)
			rY &= 0xff;
		nzx(rY);
		ioirq();
		break;
	case 0xC9: cmp(rA, imm(0), m8); break;
	case 0xCA:
		rX--;
		if(x8)
			rX &= 0xff;
		nzx(rX);
		ioirq();
		break;
	case 0xCB: wai = 1; break;
	case 0xCC: cmp(rY, memx816(abso(0, 0)), x8); break;
	case 0xCD: cmp(rA, mem816(abso(0, 0), 0), m8); break;
	case 0xCE: dec(abso(0, 0)); break;
	case 0xCF: cmp(rA, mem816(abso(1, 0), 0), m8); break;
	case 0xD0: branch((rP & FLAGZ) == 0); break;
	case 0xD1: cmp(rA, mem816(dpi(0, 0, 1), 0), m8); break;
	case 0xD2: cmp(rA, mem816(dpi(0, 0, 0), 0), m8); break;
	case 0xD3: cmp(rA, mem816(sry(), 0), m8); break;
	case 0xD4: io(); push16(dpi(0, 0, 0)); break;
	case 0xD5: cmp(rA, mem816(dp(1), 0), m8); break;
	case 0xD6: dec(dp(1)); break;
	case 0xD7: cmp(rA, mem816(dpi(1, 0, 1), 0), m8); break;
	case 0xD8: rP &= ~FLAGD; ioirq(); break;
	case 0xD9: cmp(rA, mem816(abso(0, 2), 0), m8); break;
	case 0xDA: io(); push816(rX, x8); break;
	case 0xDB: print("STP\n"); break;
	case 0xDC: a = fetch16(); pc = memread(a) | memread((u16int)(a+1))<<8; rPB = memread((u16int)(a+2)) << 16; break;
	case 0xDD: cmp(rA, mem816(abso(0, 1), 0), m8); break;
	case 0xDE: dec(abso(0, 1)); break;
	case 0xDF: cmp(rA, mem816(abso(1, 1), 0), m8); break;
	case 0xE0: cmp(rX, immx(0), x8); break;
	case 0xE1: sbc(mem816(dpi(0, 1, 0), 0)); break;
	case 0xE2: setrp(rP | fetch8()); io(); break;
	case 0xE3: sbc(mem816(sr(), 0)); break;
	case 0xE4: cmp(rX, memx816(dp(0)), x8); break;
	case 0xE5: sbc(mem816(dp(0), 0)); break;
	case 0xE6: inc(dp(0)); break;
	case 0xE7: sbc(mem816(dpi(1, 0, 0), 0)); break;
	case 0xE8:
		rX++;
		if(x8)
			rX &= 0xff;
		nzx(rX);
		ioirq();
		break;
	case 0xE9: sbc(imm(0)); break;
	case 0xEA: ioirq(); break;
	case 0xEB: nz8(rA = (rA >> 8) | (rA << 8)); io(); io(); break;
	case 0xEC: cmp(rX, memx816(abso(0, 0)), x8); break;
	case 0xED: sbc(mem816(abso(0, 0), 0)); break;
	case 0xEE: inc(abso(0, 0)); break;
	case 0xEF: sbc(mem816(abso(1, 0), 0)); break;
	case 0xF0: branch((rP & FLAGZ) != 0); break;
	case 0xF1: sbc(mem816(dpi(0, 0, 1), 0)); break;
	case 0xF2: sbc(mem816(dpi(0, 0, 0), 0)); break;
	case 0xF3: sbc(mem816(sry(), 0)); break;
	case 0xF4: push16(fetch16()); break;
	case 0xF5: sbc(mem816(dp(1), 0)); break;
	case 0xF6: inc(dp(1)); break;
	case 0xF7: sbc(mem816(dpi(1, 0, 1), 0)); break;
	case 0xF8: rP |= FLAGD; ioirq(); break;
	case 0xF9: sbc(mem816(abso(0, 2), 0)); break;
	case 0xFA: nzx(rX = pop816(0, x8)); break;
	case 0xFB:
		a = emu;
		emu = rP & 1;
		if(emu){
			rX &= 0xff;
			rY &= 0xff;
			rS = rS & 0xff | 0x100;
			rP |= 0x30;
		}
		rP &= ~1;
		rP |= a;
		ioirq();
		break;
	case 0xFC: push16(pc+1); pc = absi(1); break;
	case 0xFD: sbc(mem816(abso(0, 1), 0)); break;
	case 0xFE: inc(abso(0, 1)); break;
	case 0xFF: sbc(mem816(abso(1, 1), 0)); break;
	default:
		print("undefined %#x (pc %#.6x)\n", op, curpc);
		io();
	}
	return cyc;
}
