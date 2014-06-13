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
			tim += s == 2 ? 14 : 4;
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

	msb = 1 << ((8 << s) - 1);
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
		vf |= x ^ (v & msb) != 0;
		tim += 2;
	}
	nz(v, s);
	rS |= l;
	if(m <= 1 && vf)
		rS |= FLAGV;
	tim += s == 2 ? 8 : 6;
	return v;
}

static u8int
addbcd(u8int a, u8int b)
{
	int r;
	
	r = (a & 0xf) + (b & 0xf) + ((rS & FLAGX) != 0);
	if(r > 0x09) r += 0x06;
	if(r > 0x1f) r -= 0x10;
	r += (a & 0xf0) + (b & 0xf0);
	if(r > 0x9f) r += 0x60;
	if((u8int)r != 0)
		rS &= ~FLAGZ;
	if(r > 0xff)
		rS |= FLAGC|FLAGX;
	else
		rS &= ~(FLAGC|FLAGX);
	return r;
}

static u8int
subbcd(u8int a, u8int b)
{
	int x;
	
	x = (a & 0xf) + (~b & 0xf) + ((rS & FLAGX) == 0);
	if(x < 0x10) x -= 0x06;
	if(x < 0) x += 0x10;
	x += (a & 0xf0) + (~b & 0xf0);
	if(x > 0xff)
		rS &= ~(FLAGC|FLAGX);
	else{
		rS |= FLAGC|FLAGX;
		x -= 0x60;
	}
	if((u8int)x != 0)
		rS &= ~FLAGZ;
	return x;
}

static void
dtime(u16int op, u8int s)
{
	if((op & 0x100) != 0){
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
		for(l = 7; l >= ((rS >> 8) & 7); l--)
			if((irql[l] & irq) != 0)
				break;
		v = intack(l);
		rS = rS & ~0x700 | l << 8;
		irq = 0;
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
	for(i = 7, v = 0; i >= 0; i--)
		irqla[i] = v |= irql[i];
}

int
step(void)
{
	u32int v, w;
	vlong a;
	int s;
	int n, m, d;
	static int cnt;

	if(0 && pc == 0x1300){
		trace++;
		print("%x\n", curpc);
	}
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
		print("%.6ux %.6uo %.4ux %.8ux | %.8ux %.8ux %.8ux %.8ux | %.8ux %.8ux %.8ux\n", curpc, op, rS, memread(ra[7])<<16|memread(ra[7]+2), r[0], r[1], r[2], r[3], ra[0], ra[1], ra[7]);
	s = op >> 6 & 3;
	n = op >> 9 & 7;
	switch(op >> 12){
	case 0:
		if((op & 0x3f) == 0x3c){ /* (ORI|ANDI|EORI) to (CCR|SR) */
			if(s == 1 && (rS & FLAGS) == 0){
				trap(8, curpc);
				break;
			}
			v = rS;
			w = fetch16();
			switch(n){
			case 0: v |= w; break;
			case 1: v &= w; break;
			case 5: v ^= w; break;
			default: undef();
			}
			if(s != 1)
				v = v & 0xff | rS & 0xff00;
			rS = v;
			if(s == 1 && (rS & FLAGS) == 0){
				v = ra[7];
				ra[7] = asp;
				asp = v;
			}
			tim += 20;
			break;
		}
		if((op & 0x13f) == 0x108){ /* MOVEP */
			a = ra[op & 7] + (s16int)fetch16();
			switch(s){
			case 0:
				v = (u8int)rmode(a, 0) << 8;
				v |= (u8int)rmode(a + 2, 0);
				r[n] = r[n] & 0xff00 | v;
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
			break;
		}
		if((op & 0x100) != 0 || n == 4){ /* BTST, BCHG, BCLR, BSET */
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
			break;
		}
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
		case 2: rS |= FLAGZ; v = sub(v, w, 0, s); break;
		case 3: rS |= FLAGZ; v = add(v, w, 0, s); break;
		case 5: nz(v ^= w, s); break;
		case 6: rS |= FLAGZ; sub(v, w, 0, s); break;
		default: undef();
		}
		if(a < 0)
			tim += s == 2 ? (n == 1 || n == 6 ? 14 : 16) : 8;
		else
			tim += s == 2 ? 20 : 12;
		if(n != 6)
			wmode(a, s, v);
		break;
	case 1: /* MOVE */
		s = 0;
		goto move;
	case 2:
		s = 2;
		goto move;
	case 3:
		s = 1;
	move:
		v = rmode(amode(op >> 3, op, s), s);
		wmode(amode(op >> 6, op >> 9, s), s, v);
		if((op & 0x1c0) != 0x40)
			nz(v, s);
		tim += 4;
		break;
	case 4:
		if((op & 0x1c0) == 0x1c0){ /* LEA */
			ra[n] = amode(op >> 3, op, 2);
			break;
		}
		if((op & 0x1c0) == 0x180){ /* CHK */
			a = amode(op >> 3, op, s);
			v = rmode(a, s);
			if((s32int)r[n] < 0 || (s32int)r[n] > (s32int)v)
				trap(6, curpc);
			else
				tim += 10;
		}
		if((op & 0xb80) == 0x880 && (op & 0x38) >= 0x10){ /* MOVEM */
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
				break;
			}
			if((op & 0x38) == 0x20){
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
				break;
			}
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
			tim += (op & 0x400) != 0 ? 8 : 12;
			break;
		}
		switch(op >> 8 & 0xf){
		case 0:
			if(s == 3){ /* MOVE from SR */
				if((rS & FLAGS) != 0){
					a = amode(op >> 3, op, 1);
					wmode(a, 1, rS);
					tim += a < 0 ? 6 : 8;
				}else
					trap(8, curpc);
				break;
			} /* NEGX */
			a = amode(op >> 3, op, s);
			m = (rS & FLAGX) != 0;
			d = 1<<(8<<s)-1;
			v = -rmode(a, s);
			w = -(v+m) & (d << 1) - 1;
			rS &= ~(FLAGC|FLAGX|FLAGN|FLAGV);
			if((w & d) != 0)
				rS |= FLAGN;
			if(m && w == d)
				rS |= FLAGV;
			if(w != 0){
				rS |= FLAGC|FLAGX;
				rS &= ~FLAGZ;
			}
			wmode(a, s, v);
			stime(a < 0, s);
			break;
		case 2: /* CLR */
			a = amode(op >> 3, op, s);
			wmode(a, s, 0);
			nz(0, 0);
			stime(a < 0, s);
			break;
		case 4:
			if(s == 3){ /* MOVE to CCR */
				rS = rS & 0xff00 | rmode(amode(op >> 3, op, 1), 1);
				tim += 12;
				break;
			} /* NEG */
			a = amode(op >> 3, op, s);
			v = -rmode(a, s);
			nz(v, s);
			wmode(a, s, v);
			stime(a < 0, s);
			break;
		case 6:
			if(s == 3){ /* MOVE to SR */
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
				break;
			} /* NOT */
			a = amode(op >> 3, op, s);
			v = ~rmode(a, s);
			nz(v, s);
			wmode(a, s, v);
			stime(a < 0, s);
			break;
		case 8:
			n = op & 7;
			switch(s){
			case 0: /* NBCD */
				a = amode(op >> 3, op, 0);
				v = rmode(a, 0);
				wmode(a, 0, subbcd(0, v));
				if(a < 0)
					tim += 8;
				else
					tim += 6;
				break;
			case 1:
				if((op >> 3 & 7) != 0){
					push32(amode(op >> 3, op, 0)); /* PEA */
					tim += 8;
				}else{
					nz(r[n] = r[n] >> 16 | r[n] << 16, 2); /* SWAP */
					tim += 4;
				}
				break;
			case 2: /* EXT */
				nz(r[n] = r[n] & 0xffff0000 | (u16int)(s8int)r[n], 1);
				tim += 4;
				break;
			case 3: /* EXT */
				nz(r[n] = (s16int)r[n], 2);
				tim += 4;
				break;
			}
			break;
		case 10:
			if(s == 3){ /* TAS */
				a = amode(op >> 3, op, 0);
				v = rmode(a, 0);
				nz(v, 0);
				wmode(a, s, v | 0x80);
				tim += a < 0 ? 4 : 14;
				break;
			} /* TST */
			a = amode(op >> 3, op, s);
			nz(rmode(a, s), s);
			tim += 4;
			break;
		case 14:
			v = op >> 4 & 0xf;
			n = op & 7;
			if(v == 4){ /* TRAP */
				trap(op & 0xf, curpc);
				break;
			}else if(v == 5){
				if((op & 8) == 0){ /* LINK */
					push32(ra[n]);
					ra[n] = ra[7];
					ra[7] += (s16int)fetch16();
					tim += 16;
				}else{ /* UNLK */
					ra[7] = ra[n];
					ra[n] = pop32();
					tim += 12;
				}
				break;
			}else if(v == 6){ /* MOVE USP */
				if((rS & FLAGS) != 0){
					if((op & 8) != 0)
						ra[n] = asp;
					else
						asp = ra[n];
					tim += 4;
				}else
					trap(8, curpc);
				break;
			}
			if((op & 0xc0) == 0xc0){ /* JMP */
				pc = amode(op >> 3, op, 2);
				tim += 4;
				break;
			}
			if((op & 0xc0) == 0x80){ /* JSR */
				a = amode(op >> 3, op, 2);
				push32(pc);
				pc = a;
				tim += 12;
				break;
			}
			switch(op){
			case 0x4e70: tim += 132; break; /* RESET */
			case 0x4e71: tim += 4; break; /* NOP */
			case 0x4e72: /* STOP */
				if((rS & FLAGS) != 0){
					rS = fetch16();
					stop = 1;
				}else
					trap(8, curpc);
				tim += 4;
				break;
			case 0x4e73: /* RTE */
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
				break;
			case 0x4e75: pc = pop32(); tim += 16; break; /* RTS */
			case 0x4e76: if((rS & FLAGV) != 0) trap(7, curpc); tim += 4; break; /* TRAPV */
			case 0x4e77: /* RTR */
				rS = rS & 0xff00 | fetch16() & 0xff;
				pc = pop32();
				tim += 20;
				break;
			default: undef();
			}
			break;
		default:
			undef();
		}
		break;
	case 5:
		if((op & 0xf8) == 0xc8){ /* DBcc */
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
			break;
		}
		if(s == 3){ /* Scc */
			a = amode(op >> 3, op, 0);
			v = cond(op >> 8 & 0xf);
			wmode(a, 0, -v);
			if(a < 0)
				tim += 4 + 2 * v;
			else
				tim += 8;
			break;
		} /* ADDQ, SUBQ */
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
		if(a < 0)
			tim += s == 2 || (op & 0x130) == 0x110 ? 8 : 4;
		else
			tim += s == 2 ? 12 : 8;
		wmode(a, s, v);
		break;
	case 6: /* BRA */
		v = (s8int)op;
		if(v == 0)
			v = (s16int)fetch16();
		else if(v == (u32int)-1)
			v = fetch32();
		if((op & 0xf00) == 0x100){ /* BSR */
			push32(pc);
			pc = curpc + 2 + v;
			tim += 18;
			break;
		}
		if(cond((op >> 8) & 0xf)){
			pc = curpc + 2 + v;
			tim += 10;
		}else
			tim += (u8int)(op + 1) <= 1 ? 12 : 8;
		break;
	case 7: /* MOVEQ */
		r[n] = (s8int)op;
		nz(r[n], 0);
		tim += 4;
		break;
	case 8:
		if(s == 3){ /* DIVU, DIVS */
			a = amode(op >> 3, op, 1);
			v = rmode(a, 1);
			if(v == 0){
				trap(5, curpc);
				break;
			}
			if((op & 0x100) != 0){
				w = (s32int)r[n] % (s16int)v;
				v = (s32int)r[n] / (s16int)v;
				if(((s16int)w ^ (s16int)v) < 0)
					w = -w;
				if(v != (u32int)(s16int)v){
					rS = rS & ~FLAGC | FLAGV;
					break;
				}
				tim += 158;
			}else{
				w = r[n] % (u16int)v;
				v = r[n] / (u16int)v;
				if(v >= 0x10000){
					rS = rS & ~FLAGC | FLAGV;
					break;
				}
				tim += 140;
			}
			r[n] = (u16int)v | w << 16;
			nz(v, 1);
			break;
		}
		if((op & 0x1f0) == 0x100){ /* SBCD */
			n = (op >> 9) & 7;
			m = op & 7;
			if((op & 8) != 0){
				a = amode(4, n, 0);
				v = rmode(a, 0);
				w = rmode(amode(4, m, 0), 0);
				v = subbcd(v, w);
				wmode(a, 0, v);
				tim += 18;
			}else{
				r[n] = r[n] & 0xffffff00 | subbcd((u8int)r[n], (u8int)r[m]);
				tim += 6;
			}
			break;
		}
	logic: /* OR, EOR, AND */
		a = amode(op >> 3, op, s);
		n = (op >> 9) & 7;
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
		break;
	case 11:
		if(s == 3){ /* CMPA */
			s = (op >> 8 & 1) + 1;
			a = amode(op >> 3, op, s);
			rS |= FLAGZ;
			sub(ra[n], rmode(a, s), 0, 2);
			tim += 6;
			break;
		}
		if((op & 0x138) == 0x108){ /* CMPM */
			m = op & 7;
			rS |= FLAGZ;
			sub(rmode(amode(3, n, s), s), rmode(amode(3, m, s), s), 0, s);
			tim += s == 2 ? 20 : 12;
			break;
		}
		if((op & 0x100) == 0){ /* CMP */
			a = amode(op >> 3, op, s);
			rS |= FLAGZ;
			sub(r[n], rmode(a, s), 0, s);
			tim += s == 2 ? 6 : 4;
			break;
		}
		goto logic;
	case 12:
		if(s == 3){ /* MULU, MULS */
			a = amode(op >> 3, op, 1);
			v = rmode(a, 1);
			if((op & 0x100) != 0)
				v *= (s16int)r[n];
			else
				v = (u16int)v * (u16int)r[n];
			r[n] = v;
			nz(v, 1);
			tim += 70;
			break;
		}
		if((op & 0x1f0) == 0x100){ /* ABCD */
			n = (op >> 9) & 7;
			m = op & 7;
			if((op & 8) != 0){
				a = amode(4, n, 0);
				v = rmode(a, 0);
				w = rmode(amode(4, m, 0), 0);
				v = addbcd(v, w);
				wmode(a, 0, v);
				tim += 18;
			}else{
				r[n] = r[n] & 0xffffff00 | addbcd((u8int)r[n], (u8int)r[m]);
				tim += 6;
			}
			break;
		
		}
		if((op & 0x130) == 0x100){ /* EXG */
			m = op & 0xf;
			if((op & 0xc8) == 0x48)
				n |= 8;
			v = r[n];
			r[n] = r[m];
			r[m] = v;
			tim += 6;
			break;
		}
		goto logic;
	case 9:
	case 13:
		if(s == 3){ /* ADDA, SUBA */
			if((op & 0x100) != 0){
				s = 2;
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
			break;
		}
		if((op & 0x130) == 0x100){ /* ADDX, SUBX */
			m = op & 7;
			if((op & 8) != 0){
				a = ra[n] -= 1<<s;
				v = rmode(a, s);
				w = rmode(ra[m] -= 1<<s, s);
				tim += s == 2 ? 30 : 18;
			}else{
				v = r[n];
				w = r[m];
				a = ~n;
				tim += s == 2 ? 8 : 4;
			}
			if((op >> 12) == 13)
				v = add(v, w, (rS & FLAGX) != 0, s);
			else
				v = sub(v, w, (rS & FLAGX) != 0, s);
			wmode(a, s, v);
			rS = rS & ~FLAGX | rS & FLAGC << 4;
			break;
		} /* ADD, SUB */
		a = amode(op >> 3, op, s);
		rS |= FLAGZ;
		d = (op & 0x100) == 0;
		v = rmode(a, s);
		if((op >> 12) == 13)
			v = add(v, r[n], 0, s);
		else
			v = sub(d ? r[n] : v, d ? v : r[n], 0, s);
		rS = rS & ~FLAGX | rS & FLAGC << 4;
		if(d)
			a = ~n;
		wmode(a, s, v);
		dtime(op, s);
		break;
	case 14: /* shifts */
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
