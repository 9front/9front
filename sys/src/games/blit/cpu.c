#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

enum {
	FLAGS = 1<<13,
	FLAGX = 16,
	FLAGN = 8,
	FLAGZ = 4,
	FLAGV = 2,
	FLAGC = 1,
};

u32int r[16], pc, curpc;
u32int asp, irq, stop;
extern u32int irql[8];
u32int irqla[8];
u16int rS;
static u32int op;
int trace, tim;
#define ra (r+8)

static void
undef(void)
{
	sysfatal("undefined opcode %#o at pc=%#.6x", op, curpc);
}

static u16int
fetch16(void)
{
	u16int v;
	
	v = memread(pc);
	pc += 2;
	return v;
}

static u32int
fetch32(void)
{
	u32int v;
	
	v = fetch16() << 16;
	return v | fetch16();
}

static void
push16(u16int u)
{
	ra[7] -= 2;
	memwrite(ra[7], u, -1);
}

static u16int
pop16(void)
{
	u16int v;
	
	v = memread(ra[7]);
	ra[7] += 2;
	return v;
}

static void
push32(u32int u)
{
	ra[7] -= 4;
	memwrite(ra[7], u >> 16, -1);
	memwrite(ra[7] + 2, u, -1);
}

static u32int
pop32(void)
{
	u32int v;
	v = memread(ra[7]) << 16;
	v |= memread(ra[7] + 2);
	ra[7] += 4;
	return v;
}

static vlong
amode(int m, int n, int s)
{
	u16int w;
	u32int v;
	
	m &= 7;
	n &= 7;
	s &= 3;
	if(n == 7 && s == 0)
		s++;
	switch(m){
	case 0:
		return ~n;
	case 1:
		return ~(n+8);
	case 2:
		tim += s == 2 ? 8 : 4;
		return ra[n];
	case 3:
		v = ra[n];
		ra[n] += 1<<s;
		tim += s == 2 ? 8 : 4;
		return v;
	case 4:
		tim += s == 2 ? 10 : 6;
		return ra[n] -= 1<<s;
	case 5:
		tim += s == 2 ? 12 : 8;
		return (u32int)(ra[n] + (s16int)fetch16());
	case 6:
		tim += s == 2 ? 14 : 10;
		w = fetch16();
		v = r[w >> 12];
		if((w & 1<<11) == 0)
			v = (s16int)v;
		return (u32int)(ra[n] + v + (s8int)w);
	case 7:
		switch(n){
		case 0:
			tim += s == 2 ? 12 : 8;
			return (u32int)(s16int)fetch16();
		case 1:
			tim += s == 2 ? 16 : 12;
			return fetch32();
		case 2:
			tim += s == 2 ? 12 : 8;
			v = fetch16();
			return (u32int)(pc + (s16int)v - 2);
		case 3:
			tim += s == 2 ? 14 : 10;
			w = fetch16();
			v = r[w >> 12];
			if((w & 1<<11) == 0)
				v = (s16int)v;
			return (u32int)(pc + v + (s8int)w - 2);
		case 4:
			tim += s == 2 ? 8 : 4;
			v = pc;
			pc += 1<<s;
			if(s == 0)
				v = pc++;
			return v;
		default:
			undef();
		}
	default:
		undef();
	}
	return 0;
}

static u32int
rmode(vlong a, int s)
{
	u32int v;

	if(a >= 0){
		switch(s & 3){
		case 0:
			v = memread(a);
			if((a & 1) == 0)
				v >>= 8;
			return (s8int) v;
		default:
			return (s16int) memread(a);
		case 2:
			v = memread(a) << 16;
			return v | memread(a + 2);
		}
	}
	v = r[~a];
	switch(s & 3){
	case 0: return (s8int) v;
	case 1: return (s16int) v;
	default: return v;
	}
}

static void
wmode(vlong a, int s, u32int v)
{
	int n;

	if(a >= 0){
		switch(s & 3){
		case 0:
			memwrite(a, (u8int)v | (u8int)v << 8, (a & 1) != 0 ? 0xff : 0xff00);
			return;
		default:
			memwrite(a, v, -1);
			return;
		case 2:
			memwrite(a, v >> 16, -1);
			memwrite(a + 2, v, -1);
			return;
		}
	}
	n = ~a;
	if(n < 8){
		switch(s){
		case 0: r[n] = r[n] & 0xffffff00 | v & 0xff; break;
		case 1: r[n] = r[n] & 0xffff0000 | v & 0xffff; break;
		default: r[n] = v;
		}
	}else{
		if(s == 1)
			v = (s16int) v;
		r[n] = v;
	}
}

static void
nz(u32int v, int s)
{
	switch(s){
	case 0: v = (s8int) v; break;
	case 1: v = (s16int) v; break;
	}
	rS &= ~(FLAGC|FLAGN|FLAGV|FLAGZ);
	if(v == 0)
		rS |= FLAGZ;
	if((s32int)v < 0)
		rS |= FLAGN;
}

static u32int
add(u32int u, u32int w, int c, int s)
{
	u64int v;

	rS &= ~(FLAGN|FLAGV|FLAGC);
	switch(s){
	case 0:
		v = (u8int)w + (u8int)u + c;
		if(v >= 0x100)
			rS |= FLAGC;
		if((v & 0x80) != 0)
			rS |= FLAGN;
		if((~(w ^ u) & (v ^ u) & 0x80) != 0)
			rS |= FLAGV;
		if((u8int)v != 0)
			rS &= ~FLAGZ;
		break;
	case 1:
		v = (u16int)w + (u16int)u + c;
		if(v >= 0x10000)
			rS |= FLAGC;
		if((v & 0x8000) != 0)
			rS |= FLAGN;
		if((~(w ^ u) & (v ^ u) & 0x8000) != 0)
			rS |= FLAGV;
		if((u16int)v != 0)
			rS &= ~FLAGZ;
		break;
	default:
		v = (u64int)w + u + c;
		if((v >> 32) != 0)
			rS |= FLAGC;
		if((v & 0x80000000) != 0)
			rS |= FLAGN;
		if((~(w ^ u) & (v ^ u) & 0x80000000) != 0)
			rS |= FLAGV;
		if((u32int)v != 0)
			rS &= ~FLAGZ;
		break;
	}
	return v;
} 

static u32int
sub(u32int u, u32int w, int c, int s)
{
	u64int v;

	rS &= ~(FLAGN|FLAGV|FLAGC);
	switch(s){
	case 0:
		v = (u8int)u - (u8int)w - c;
		if(v >= 0x100)
			rS |= FLAGC;
		if((v & 0x80) != 0)
			rS |= FLAGN;
		if(((w ^ u) & (v ^ u) & 0x80) != 0)
			rS |= FLAGV;
		if((u8int)v != 0)
			rS &= ~FLAGZ;
		break;
	case 1:
		v = (u16int)u - (u16int)w - c;
		if(v >= 0x10000)
			rS |= FLAGC;
		if((v & 0x8000) != 0)
			rS |= FLAGN;
		if(((w ^ u) & (v ^ u) & 0x8000) != 0)
			rS |= FLAGV;
		if((u16int)v != 0)
			rS &= ~FLAGZ;
		break;
	default:
		v = (u64int)u - w - c;
		if((v >> 32) != 0)
			rS |= FLAGC;
		if((v & 0x80000000) != 0)
			rS |= FLAGN;
		if(((w ^ u) & (v ^ u) & (1<<31)) != 0)
			rS |= FLAGV;
		if((u32int)v != 0)
			rS &= ~FLAGZ;
		break;
	}
	return v;
}

static int
cond(int n)
{
	switch(n){
	case 0: return 1; break;
	default: return 0; break;
	case 2: return (rS & (FLAGC|FLAGZ)) == 0; break;
	case 3: return (rS & (FLAGC|FLAGZ)) != 0; break;
	case 4: return (rS & FLAGC) == 0; break;
	case 5: return (rS & FLAGC) != 0; break;
	case 6: return (rS & FLAGZ) == 0; break;
	case 7: return (rS & FLAGZ) != 0; break;
	case 8: return (rS & FLAGV) == 0; break;
	case 9: return (rS & FLAGV) != 0; break;
	case 10: return (rS & FLAGN) == 0; break;
	case 11: return (rS & FLAGN) != 0; break;
	case 12: return ((rS ^ (rS << 2)) & FLAGN) == 0; break;
	case 13: return ((rS ^ (rS << 2)) & FLAGN) != 0; break;
	case 14: return ((rS ^ (rS << 2)) & FLAGN) == 0 && (rS & FLAGZ) == 0; break;
	case 15: return ((rS ^ (rS << 2)) & FLAGN) != 0 || (rS & FLAGZ) != 0; break;
	}
}

static u32int
rot(u32int v, int m, int n, int s)
{
	int l, ll, x, vf;
	u32int msb;
	
	/* abandon all hope ye who enter here */
	msb = 1 << (8 << s) - 1;
	v &= (msb << 1) - 1;
	if(m == 0)
		x = (v & msb) != 0;
	else
		x = 0;
	if((m & 6) == 4)
		ll = l = (rS & FLAGX) != 0;
	else
		ll = l = 0;
	vf = 0;
	while(n--){
		if((m & 1) == 0){
			l = v & 1;
			v >>= 1;
		}else{
			l = (v & msb) != 0;
			v <<= 1;
		}
		if((m & 6) != 6)
			rS = rS & ~FLAGX | l << 4;
		if(m >= 6)
			x = l;
		else if(m >= 4){
			x = ll;
			ll = l;
		}
		if((m & 1) == 0){
			if(x != 0)
				v |= msb;
		}else
			v |= x;
		vf |= l ^ (v & msb) != 0;
		tim += 2;
	}
	nz(v, s);
	rS |= l;
	if(m == 1 && vf)
		rS |= FLAGV;
	tim += s == 2 ? 8 : 6;
	return v;
}

static u8int
addbcd(u8int a, u8int b)
{
	int r;
	u8int bc, dc, s;
	
	r = a + b + (rS >> 4 & 1);
	bc = ((a ^ b ^ r) & 0x110) >> 1;
	dc = ((r + 0x66 ^ r) & 0x110) >> 1;
	s = r + (bc | dc) - ((bc | dc) >> 2);
	rS &= ~(FLAGC|FLAGX|FLAGN|FLAGV);
	if(((bc | r & ~s) & 0x80) != 0)
		rS |= FLAGC|FLAGX;
	if(s != 0)
		rS &= ~FLAGZ;
	if((s & 0x80) != 0)
		rS |= FLAGN;
	if((~r & s & 0x80) != 0)
		rS |= FLAGV;
	return s;
}

static u8int
subbcd(u8int a, u8int b)
{
	int r;
	u8int bc, s;
	
	r = a - b - (rS >> 4 & 1);
	bc = ((a ^ b ^ r) & 0x110) >> 1;
	s = r - (bc - (bc >> 2));
	rS &= ~(FLAGC|FLAGX|FLAGN|FLAGV);
	if(((bc | (~r & s)) & 0x80) != 0)
		rS |= FLAGC|FLAGX;
	if(s != 0)
		rS &= ~FLAGZ;
	if((s & 0x80) != 0)
		rS |= FLAGN;
	if((r & ~s & 0x80) != 0)
		rS |= FLAGV;
	return s;
}

static void
dtime(u16int op, u8int s)
{
	if((op & 0x100) == 0){
		if(s == 2)
			if((op & 0x30) == 0 || (op & 0x3f) == 0x3c)
				tim += 8;
			else
				tim += 6;
		else
			tim += 4;
	}else
		tim += s == 2 ? 12 : 8;
}

static void
stime(int a, u8int s)
{
	if(a)
		tim += s == 2 ? 6 : 4;
	else
		tim += s == 2 ? 12 : 8;
}

static void
trap(int n, u32int pcv)
{
	int l, v;
	u32int sr, t;
	
	sr = rS;
	if(n < 0){
		for(l = 7; l > ((rS >> 8) & 7); l--)
			if((irql[l] & irq) != 0)
				break;
		v = intack(l);
		rS = rS & ~0x700 | l << 8;
		tim += 44;
	}else{
		switch(n){
		case 2: case 3: tim += 50; break;
		case 5: tim += 38; break;
		case 6: tim += 40; break;
		default: tim += 34; break;
		}
		v = n;
	}
	if((rS & FLAGS) == 0){
		t = asp;
		asp = ra[7];
		ra[7] = t;
	}
	rS |= FLAGS;
	push32(pcv);
	push16(sr);
	pc = memread(v * 4) << 16;
	pc |= memread(v * 4 + 2);
	stop = 0;
}

void
cpureset(void)
{
	u32int v;
	int i;

	ra[7] = memread(0) << 16 | memread(2);
	pc = memread(4) << 16 | memread(6);
	rS = 0x2700;
	for(i = 7, v = 0; i >= 0; i--){
		irqla[i] = v;
		v |= irql[i];
	}
}

static void
cputrace(void)
{
	int i;
	static char buf[1024];
	static u32int oldr[16];
	char *p, *e;
	
	p = buf;
	e = buf + sizeof(buf);
	p = seprint(p, e, "%.6ux %.6uo %.4x %.8ux %.8ux |", curpc, op, rS, ra[7], asp);
	for(i = 0; i < 16; i++)
		if(oldr[i] != r[i])
			p = seprint(p, e, " %c%d=%.8ux", i >= 8  ? 'A' : 'D', i & 7, r[i]);
	print("%s\n", buf);
	memcpy(oldr, r, sizeof(r));
}

static void
ccr_sr_op(u16int op)
{
	int s;
	u32int v, w;
	
	s = op >> 6 & 3;
	if(s == 1 && (rS & FLAGS) == 0){
		trap(8, curpc);
		return;
	}
	v = rS;
	w = fetch16();
	switch(op >> 9 & 7){
	case 0: v |= w; break;
	case 1: v &= w; break;
	case 5: v ^= w; break;
	default: undef();
	}
	if(s != 1)
		v = v & 0x1f | rS & 0xff00;
	rS = v;
	if(s == 1 && (rS & FLAGS) == 0){
		v = ra[7];
		ra[7] = asp;
		asp = v;
	}
	tim += 20;
}

static void
op_movep(u16int op)
{
	int s, n;
	vlong a;
	u32int v;

	a = (u32int)(ra[op & 7] + (s16int)fetch16());
	s = op >> 6 & 3;
	n = op >> 9 & 7;
	switch(s){
	case 0:
		v = (u8int)rmode(a, 0) << 8;
		v |= (u8int)rmode(a + 2, 0);
		r[n] = r[n] & 0xffff0000 | v;
		tim += 16;
		break;
	case 1:
		v = (u8int)rmode(a, 0) << 24;
		v |= (u8int)rmode(a + 2, 0) << 16;
		v |= (u8int)rmode(a + 4, 0) << 8;
		v |= (u8int)rmode(a + 6, 0);
		tim += 24;
		r[n] = v;
		break;
	case 2:
		wmode(a, 0, r[n] >> 8);
		wmode(a + 2, 0, r[n]);
		tim += 16;
		break;
	case 3:
		wmode(a, 0, r[n] >> 24);
		wmode(a + 2, 0, r[n] >> 16);
		wmode(a + 4, 0, r[n] >> 8);
		wmode(a + 6, 0, r[n]);
		tim += 24;
		break;
	}
}

static void
bitop(u16int op)
{
	int s, n;
	u32int v, w;
	vlong a;
	
	s = op >> 6 & 3;
	n = op >> 9 & 7;
	if((op & 0x100) != 0)
		w = r[n];
	else
		w = fetch16();
	if((op & 0x38) != 0){
		n = 0;
		w = 1<<(w & 7);
	}else{
		n = 2;
		w = 1<<(w & 31);
	}
	a = amode(op >> 3, op, n);
	v = rmode(a, n);
	rS &= ~FLAGZ;
	if((v & w) == 0)
		rS |= FLAGZ;
	switch(s){
	case 1: v ^= w; break;
	case 2: v &= ~w; if(n == 2) tim += 2; break;
	case 3: v |= w; break;
	}
	if(s != 0){
		wmode(a, n, v);
		tim += (op & 0x100) != 0 ? 8 : 12;
	}else{
		tim += (op & 0x100) != 0 ? 4 : 8;
		if(n == 2)
			tim += 2;
	}
}

static void
immop(u16int op)
{
	int s, n;
	u32int v, w;
	vlong a;
	
	s = op >> 6 & 3;
	n = op >> 9 & 7;
	switch(s){
	case 0: w = (s8int)fetch16(); break;
	default: w = fetch16(); break;
	case 2: w = fetch32(); break;
	}
	a = amode(op >> 3, op, s);
	v = rmode(a, s);
	switch(n){
	case 0: nz(v |= w, s); break;
	case 1: nz(v &= w, s); break;
	case 2: rS |= FLAGZ; v = sub(v, w, 0, s); rS = rS & ~FLAGX | rS << 4 & FLAGX; break;
	case 3: rS |= FLAGZ; v = add(v, w, 0, s); rS = rS & ~FLAGX | rS << 4 & FLAGX; break;
	case 5: nz(v ^= w, s); break;
	case 6: rS |= FLAGZ; sub(v, w, 0, s); break;
	default: undef();
	}
	if(n == 6){
		if(a < 0)
			tim += s == 2 ? 14 : 8;
		else
			tim += s == 2 ? 12 : 8;
	}else{
		if(a < 0)
			tim += s == 2 ? 16 : 8;
		else
			tim += s == 2 ? 20 : 12;
	}
	if(n != 6)
		wmode(a, s, v);
}

void
op_move(u16int op, int s)
{
	u32int v;

	v = rmode(amode(op >> 3, op, s), s);
	wmode(amode(op >> 6, op >> 9, s), s, v);
	if((op & 0x1c0) != 0x40)
		nz(v, s);
	tim += 4;
	if((op & 0700) == 0400)
		tim -= 2;
}

static void
op_lea(u16int op)
{
	int n;
	
	n = op >> 9 & 7;
	ra[n] = amode(op >> 3, op, 1);
	if((op & 070) == 060 || (op & 077) == 073) tim += 2;
}

static void
op_chk(u16int op)
{
	int s, n;
	u32int v;
	vlong a;
	
	s = op >> 6 & 3;
	n = op >> 9 & 7;
	a = amode(op >> 3, op, s);
	v = rmode(a, s);
	if((s32int)r[n] < 0 || (s32int)r[n] > (s32int)v)
		trap(6, curpc);
	else
		tim += 10;
}

static void
op_movem(u16int op)
{
	int s, n, m;
	u32int w;
	vlong a;
	
	s = (op >> 6 & 1) + 1;
	w = fetch16();
	if((op & 0x38) == 0x18){
		n = op & 7;
		a = ra[n];
		for(m = 0; m < 16; m++){
			if((w & 1) != 0){
				r[m] = rmode(a, s);
				a += 1<<s;
				tim += 2<<s;
			}
			w >>= 1;
		}
		ra[n] = a;
		tim += 12;
	}else if((op & 0x38) == 0x20){
		n = op & 7;
		a = ra[n];
		for(m = 0; m < 16; m++){
			if((w & 1) != 0){
				a -= 1<<s;
				wmode(a, s, r[15 - m]);
				tim += 2<<s;
			}
			w >>= 1;
		}
		ra[n] = a;
		tim += 8;
	}else{
		a = amode(op >> 3, op, s);
		for(m = 0; m < 16; m++){
			if((w & 1) != 0){
				if((op & 0x400) != 0)
					r[m] = rmode(a, s);
				else
					wmode(a, s, r[m]);
				a += 1<<s;
				tim += 2<<s;
			}
			w >>= 1;
		}
		tim += (op & 0x400) != 0 ? 8 : 4;
		if(s == 2) tim -= 4;
	}
}

static void
op_move_from_sr(u16int op)
{
	vlong a;
	
	a = amode(op >> 3, op, 1);
	wmode(a, 1, rS);
	tim += a < 0 ? 6 : 8;
}

static void
op_negx(u16int op)
{
	int s;
	u32int v;
	vlong a;
	
	s = op >> 6 & 3;
	a = amode(op >> 3, op, s);
	v = rmode(a, s);
	wmode(a, s, sub(0, v, rS>>4 & 1, s));
	rS = rS & ~FLAGX | rS << 4 & FLAGX;
	stime(a < 0, s);
}

static void
op_clr(u16int op)
{
	int s;
	vlong a;
	
	s = op >> 6 & 3;
	a = amode(op >> 3, op, s);
	wmode(a, s, 0);
	nz(0, 0);
	stime(a < 0, s);
}

static void
op_move_to_ccr(u16int op)
{
	rS = rS & 0xff00 | rmode(amode(op >> 3, op, 1), 1) & 0x1f;
	tim += 12;
}

static void
op_neg(u16int op)
{
	int s;
	u32int v;
	vlong a;
	
	s = op >> 6 & 3;
	a = amode(op >> 3, op, s);
	v = rmode(a, s);
	rS |= FLAGZ;
	wmode(a, s, sub(0, v, 0, s));
	rS = rS & ~FLAGX | rS << 4 & FLAGX;
	stime(a < 0, s);
}

static void
op_move_to_sr(u16int op)
{
	u32int v;
	
	if((rS & FLAGS) != 0){
		rS = rmode(amode(op >> 3, op, 1), 1);
		if((rS & FLAGS) == 0){
			v = asp;
			asp = ra[7];
			ra[7] = v;
		}
		tim += 12;
	}else
		trap(8, curpc);
}

static void
op_not(u16int op)
{
	int s;
	u32int v;
	vlong a;
	
	s = op >> 6 & 3;
	a = amode(op >> 3, op, s);
	v = ~rmode(a, s);
	nz(v, s);
	wmode(a, s, v);
	stime(a < 0, s);
}

static void
op_nbcd(u16int op)
{
	u32int v;
	vlong a;
	
	a = amode(op >> 3, op, 0);
	v = rmode(a, 0);
	wmode(a, 0, subbcd(0, v));
	if(a < 0)
		tim += 6;
	else
		tim += 8;
}

static void
op_pea(u16int op)
{
	ra[7] -= 4;
	wmode(ra[7], 2, amode(op >> 3, op, 1));
	tim += 8;
	if((op & 070) == 060 || (op & 077) == 073) tim += 2;
}

static void
op_swap(u16int op)
{
	int n;
	
	n = op & 7;
	nz(r[n] = r[n] >> 16 | r[n] << 16, 2);
	tim += 4;
}

static void
op_ext_b(u16int op)
{
	int n;
	
	n = op & 7;
	nz(r[n] = r[n] & 0xffff0000 | (u16int)(s8int)r[n], 1);
	tim += 4;
}

static void
op_ext_w(u16int op)
{
	int n;
	
	n = op & 7;
	nz(r[n] = (s16int)r[n], 2);
	tim += 4;
}

static void
op_tas(u16int op)
{
	u32int v;
	vlong a;
	
	a = amode(op >> 3, op, 0);
	v = rmode(a, 0);
	nz(v, 0);
	wmode(a, 0, v | 0x80);
	tim += a < 0 ? 4 : 14;
}

static void
op_tst(u16int op)
{
	int s;
	vlong a;
	
	s = op >> 6 & 3;
	a = amode(op >> 3, op, s);
	nz(rmode(a, s), s);
	tim += 4;
}

static void
op_trap(u16int op)
{
	trap(0x20 | op & 0xf, pc);
}

static void
op_link(u16int op)
{
	int n;
	
	n = op & 7;
	push32(ra[n]);
	ra[n] = ra[7];
	ra[7] += (s16int)fetch16();
	tim += 16;
}

static void
op_unlk(u16int op)
{
	int n;
	
	n = op & 7;
	ra[7] = ra[n];
	ra[n] = pop32();
	tim += 12;
}

static void
op_move_usp(u16int op)
{
	int n;
	
	n = op & 7;
	if((rS & FLAGS) != 0){
		if((op & 8) != 0)
			ra[n] = asp;
		else
			asp = ra[n];
		tim += 4;
	}else
		trap(8, curpc);
}

static void
jtime(int op)
{
	int a;
	
	a = op >> 3 & 7;
	if(a == 2 || a == 6 || (op & 077) == 073)
		tim += 2;
	else if((op & 077) == 071)
		tim -= 2;
}

static void
op_jmp(u16int op)
{
	pc = amode(op >> 3, op, 1);
	jtime(op);
	tim += 2;
}

static void
op_jsr(u16int op)
{
	vlong a;

	a = amode(op >> 3, op, 1);
	push32(pc);
	pc = a;
	jtime(op);
	tim += 10;
}

static void
op_reset(u16int op)
{
	USED(op);
	tim += 132;
}

static void
op_nop(u16int op)
{
	USED(op);
	tim += 4;
}

static void
op_stop(u16int op)
{
	USED(op);
	if((rS & FLAGS) != 0){
		rS = fetch16();
		stop = 1;
	}else
		trap(8, curpc);
	tim += 4;
}

static void
op_rte(u16int op)
{
	u32int v;

	USED(op);
	if((rS & FLAGS) != 0){
		v = rS;
		rS = pop16();
		pc = pop32();
		if(((v ^ rS) & FLAGS) != 0){
			v = asp;
			asp = ra[7];
			ra[7] = v;
		}
		tim += 20;
	}else
		trap(8, curpc);
}

static void
op_rts(u16int op)
{
	USED(op);
	pc = pop32();
	tim += 16;
}

void
op_trapv(u16int op)
{
	USED(op);
	if((rS & FLAGV) != 0)
		trap(7, curpc); 
	tim += 4;
}

void
op_rtr(u16int op)
{
	USED(op);
	rS = rS & 0xff00 | pop16() & 0x1f;
	pc = pop32();
	tim += 20;
}

static void
op_dbcc(u16int op)
{
	int n;
	u32int v;
	
	n = op & 7;
	v = (s16int)fetch16();
	if(!cond((op >> 8) & 0xf)){
		if((u16int)r[n] != 0){
			r[n]--;
			pc = pc + v - 2;
			tim += 10;
		}else{
			r[n] |= 0xffff;
			tim += 14;
		}
	}else
		tim += 12;
}

static void
op_scc(u16int op)
{
	u32int v;
	vlong a;

	a = amode(op >> 3, op, 0);
	v = cond(op >> 8 & 0xf);
	wmode(a, 0, -v);
	if(a < 0)
		tim += 4 + 2 * v;
	else
		tim += 8;
}

static void
op_addq_subq(u16int op)
{
	int s, n;
	u32int v;
	vlong a;
	
	s = op >> 6 & 3;
	n = op >> 9 & 7;
	rS |= FLAGZ;
	if((op & 0x38) == 0x08)
		s = 2;
	a = amode(op >> 3, op, s);
	v = rmode(a, s);
	if(n == 0)
		n = 8;
	if((op & 0x100) == 0)
		v = add(v, n, 0, s);
	else
		v = sub(v, n, 0, s);
	rS = rS & ~FLAGX | rS << 4 & FLAGX;
	if(a < 0)
		tim += s == 2 ? 8 : 4;
	else
		tim += s == 2 ? 12 : 8;
	wmode(a, s, v);
}

static void
op_addq_subq_a(u16int op)
{
	int s, n;
	
	s = op >> 6 & 3;
	n = op >> 9 & 7;
	if(n == 0)
		n = 8;
	tim += s == 2 || (op & 0x100) != 0 ? 8 : 4;
	if((op & 0x100) == 0)
		ra[op & 7] += n;
	else
		ra[op & 7] -= n;
}

static void
op_bra(u16int op)
{
	u32int v;
	
	v = (s8int)op;
	if(v == 0)
		v = (s16int)fetch16();
	else if(v == (u32int)-1)
		v = fetch32();
	if((op & 0xf00) == 0x100){ /* BSR */
		push32(pc);
		pc = curpc + 2 + v;
		tim += 18;
		return;
	}
	if(cond((op >> 8) & 0xf)){
		pc = curpc + 2 + v;
		tim += 10;
	}else
		tim += (u8int)(op + 1) <= 1 ? 12 : 8;
}

static void
op_moveq(u16int op)
{
	int n;
	
	n = op >> 9 & 7;
	r[n] = (s8int)op;
	nz(r[n], 0);
	tim += 4;
}

static void
op_divu_divs(u16int op)
{
	int n;
	u32int v, w;
	vlong a;
	
	n = op >> 9 & 7;
	a = amode(op >> 3, op, 1);
	v = rmode(a, 1);
	if(v == 0){
		trap(5, curpc);
		return;
	}
	if((op & 0x100) != 0){
		w = (s32int)r[n] % (s16int)v;
		v = (s32int)r[n] / (s16int)v;
		if((s32int)(w ^ r[n]) < 0)
			w = -w;
		tim += 158;
		if(v != (u32int)(s16int)v){
			rS = rS & ~FLAGC | FLAGV;
			return;
		}
	}else{
		w = r[n] % (u16int)v;
		v = r[n] / (u16int)v;
		tim += 140;
		if(v >= 0x10000){
			rS = rS & ~FLAGC | FLAGV;
			return;
		}
	}
	r[n] = (u16int)v | w << 16;
	nz(v, 1);
}

static void
op_sbcd(u16int op)
{
	int n, m;
	u32int v, w;
	vlong src, dst;
	
	n = op >> 9 & 7;
	m = op & 7;
	if((op & 8) != 0){
		src = amode(4, m, 0);
		dst = amode(4, n, 0);
		w = rmode(src, 0);
		v = rmode(dst, 0);
		v = subbcd(v, w);
		wmode(dst, 0, v);
	}else
		r[n] = r[n] & 0xffffff00 | subbcd(r[n], r[m]);
	tim += 6;
}

static void
logic(u16int op)
{
	int s, n;
	u32int v;
	vlong a;
	
	s = op >> 6 & 3;
	a = amode(op >> 3, op, s);
	n = op >> 9 & 7;
	v = rmode(a, s);
	switch(op >> 12){
	case 8: v |= r[n]; break;
	case 11: v ^= r[n]; break;
	case 12: v &= r[n]; break;
	}
	if((op & 0x100) == 0)
		a = ~n;
	wmode(a, s, v);
	nz(v, s);
	dtime(op, s);
}

static void
op_cmpa(u16int op)
{
	int s, n;
	vlong a;

	n = op >> 9 & 7;
	s = (op >> 8 & 1) + 1;
	a = amode(op >> 3, op, s);
	rS |= FLAGZ;
	sub(ra[n], rmode(a, s), 0, 2);
	tim += 6;
}

static void
op_cmpm(u16int op)
{
	int s, n, m;
	vlong src, dst;
	u32int v, w;
	
	s = op >> 6 & 3;
	n = op >> 9 & 7;
	m = op & 7;
	rS |= FLAGZ;
	src = amode(3, m, s);
	dst = amode(3, n, s);
	v = rmode(src, s);
	w = rmode(dst, s);
	sub(w, v, 0, s);
	tim += 4;
}

static void
op_cmp(u16int op)
{
	int s, n;
	vlong a;
	
	s = op >> 6 & 3;
	n = op >> 9 & 7;
	a = amode(op >> 3, op, s);
	rS |= FLAGZ;
	sub(r[n], rmode(a, s), 0, s);
	tim += s == 2 ? 6 : 4;
}

static void
op_mulu_muls(u16int op)
{
	int n;
	u32int v;
	vlong a;
	
	n = op >> 9 & 7;
	a = amode(op >> 3, op, 1);
	v = rmode(a, 1);
	if((op & 0x100) != 0)
		v *= (s16int)r[n];
	else
		v = (u16int)v * (u16int)r[n];
	r[n] = v;
	nz(v, 2);
	tim += 70;
}

static void
op_abcd(u16int op)
{
	int n, m;
	u32int v, w;
	vlong src, dst;
	
	n = op >> 9 & 7;
	m = op & 7;
	if((op & 8) != 0){
		src = amode(4, m, 0);
		dst = amode(4, n, 0);
		v = rmode(src, 0);
		w = rmode(dst, 0);
		v = addbcd(v, w);
		wmode(dst, 0, v);
	}else
		r[n] = r[n] & 0xffffff00 | addbcd(r[n], r[m]);
	tim += 6;
}

static void
op_exg(u16int op)
{
	int n, m;
	u32int v;
	
	n = op >> 9 & 7;
	m = op & 0xf;
	if((op & 0xc8) == 0x48)
		n |= 8;
	v = r[n];
	r[n] = r[m];
	r[m] = v;
	tim += 6;
}

static void
op_adda_suba(u16int op)
{
	int s, n;
	vlong a;
	
	n = op >> 9 & 7;
	if((op & 0x100) != 0){
		s = 2;
		if((op & 0x30) == 0 || (op & 0x3f) == 0x3c)
			tim += 8;
		else
			tim += 6;
	}else{
		s = 1;
		tim += 8;
	}
	a = amode(op >> 3, op, s);
	if((op >> 12) == 13)
		ra[n] += rmode(a, s);
	else
		ra[n] -= rmode(a, s);
}

static void
op_addx_subx(u16int op)
{
	int s, n, m;
	u32int v, w;
	vlong src, dst;
	
	s = op >> 6 & 3;
	n = op >> 9 & 7;
	m = op & 7;
	if((op & 8) != 0){
		src = amode(4, m, s);
		dst = amode(4, n, s);
		w = rmode(src, s);
		v = rmode(dst, s);
		tim += s == 2 ? 10 : 6;
	}else{
		w = r[m];
		v = r[n];
		dst = ~n;
		tim += s == 2 ? 8 : 4;
	}
	if((op >> 12) == 13)
		v = add(v, w, (rS & FLAGX) != 0, s);
	else
		v = sub(v, w, (rS & FLAGX) != 0, s);
	wmode(dst, s, v);
	rS = rS & ~FLAGX | rS << 4 & FLAGX;
}

static void
op_add_sub(u16int op)
{
	int s, n, d;
	u32int v;
	vlong a;
	
	s = op >> 6 & 3;
	n = op >> 9 & 7;
	a = amode(op >> 3, op, s);
	rS |= FLAGZ;
	d = (op & 0x100) == 0;
	v = rmode(a, s);
	if((op >> 12) == 13)
		v = add(v, r[n], 0, s);
	else
		v = sub(d ? r[n] : v, d ? v : r[n], 0, s);
	rS = rS & ~FLAGX | rS << 4 & FLAGX;
	if(d)
		a = ~n;
	wmode(a, s, v);
	dtime(op, s);
}

static void
shifts(u16int op)
{
	int s, n, m;
	vlong a;

	s = op >> 6 & 3;
	if(s == 3){
		m = op >> 8 & 7;
		n = 1;
		s = 1;
		a = amode(op >> 3, op, s);
	}else{
		a = ~(uvlong)(op & 7);
		m = op >> 2 & 6 | op >> 8 & 1;
		n = (op >> 9) & 7;
		if((op & 0x20) != 0)
			n = r[n] & 63;
		else if(n == 0)
			n = 8;
	}
	wmode(a, s, rot(rmode(a, s), m, n, s));
}

int
step(void)
{
	int s;
	int n;
	static int cnt;

	if(0 && pc == 0x4118c){
		trace++;
		print("%x\n", curpc);
	}
//	if(pc == 0x410de) trace++;
	tim = 0;
	curpc = pc;
	if(irq && (irqla[(rS >> 8) & 7] & irq) != 0){
		trap(-1, curpc);
		return tim;
	}
	if(stop)
		return 1;
	op = fetch16();
	if(trace)
		cputrace();
	s = op >> 6 & 3;
	n = op >> 9 & 7;
	switch(op >> 12){
	case 0:
		if((op & 0x3f) == 0x3c)
			ccr_sr_op(op);
		else if((op & 0x138) == 0x108)
			op_movep(op);
		else if((op & 0x100) != 0 || n == 4)
			bitop(op);
		else
			immop(op);
		break;
	case 1: op_move(op, 0); break;
	case 2: op_move(op, 2); break;
	case 3: op_move(op, 1); break;
	case 4:
		if((op & 0x1c0) == 0x1c0)
			op_lea(op);
		else if((op & 0x1c0) == 0x180)
			op_chk(op);
		else if((op & 0xb80) == 0x880 && (op & 0x38) >= 0x10)
			op_movem(op);
		else
			switch(op >> 8 & 0xf){
			case 0:
				if(s == 3)
					op_move_from_sr(op);
				else
					op_negx(op);
				break;
			case 2: op_clr(op); break;
			case 4:
				if(s == 3)
					op_move_to_ccr(op);
				else
					op_neg(op);
				break;
			case 6:
				if(s == 3)
					op_move_to_sr(op);
				else
					op_not(op);
				break;
			case 8:
				switch(s){
				case 0: op_nbcd(op); break;
				case 1:
					if((op >> 3 & 7) != 0)
						op_pea(op);
					else
						op_swap(op);
					break;
				case 2: op_ext_b(op); break;
				case 3: op_ext_w(op); break;
				}
				break;
			case 10:
				if(s == 3)
					op_tas(op);
				else
					op_tst(op);
				break;
			case 14:
				switch(op >> 4 & 0xf){
				case 4: op_trap(op); break;
				case 5:
					if((op & 8) == 0)
						op_link(op);
					else
						op_unlk(op);
					break;
				case 6: op_move_usp(op); break;
				default:
					if((op & 0xc0) == 0xc0)
						op_jmp(op);
					else if((op & 0x80) == 0x80)
						op_jsr(op);
					else
						switch(op){
						case 0x4e70: op_reset(op); break;
						case 0x4e71: op_nop(op); break;
						case 0x4e72: op_stop(op); break;
						case 0x4e73: op_rte(op); break;
						case 0x4e75: op_rts(op); break;
						case 0x4e76: op_trapv(op); break;
						case 0x4e77: op_rtr(op); break;
						default: undef();
						}
				}
				break;
			default:
				undef();
				break;
			}
		break;
	case 5:
		if((op & 0xf8) == 0xc8)
			op_dbcc(op);
		else if(s == 3)
			op_scc(op);
		else if((op & 070) == 010)
			op_addq_subq_a(op);
		else
			op_addq_subq(op);
		break;
	case 6:
		op_bra(op);
		break;
	case 7:
		op_moveq(op);
		break;
	case 8:
		if(s == 3)
			op_divu_divs(op);
		else if((op & 0x1f0) == 0x100)
			op_sbcd(op);
		else
			logic(op);
		break;
	case 11:
		if(s == 3)
			op_cmpa(op);
		else if((op & 0x138) == 0x108)
			op_cmpm(op);
		else if((op & 0x100) == 0)
			op_cmp(op);
		else
			logic(op);
		break;
	case 12:
		if(s == 3)
			op_mulu_muls(op);
		else if((op & 0x1f0) == 0x100)
			op_abcd(op);
		else if((op & 0x130) == 0x100)
			op_exg(op);
		else
			logic(op);
		break;
	case 9:
	case 13:
		if(s == 3)
			op_adda_suba(op);
		else if((op & 0x130) == 0x100)
			op_addx_subx(op);
		else
			op_add_sub(op);
		break;
	case 14:
		shifts(op);
		break;
	case 10:
		trap(10, curpc);
		break;
	case 15:
		trap(11, curpc);
		break;
	default:
		undef();
	}
	return tim;
}
