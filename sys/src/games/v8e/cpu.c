#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

u32int r[16];
u32int curpc;
u32int ps;
int trace;

enum {
	ADD,
	SUB,
	MUL,
	DIV,
	CMP,
	TST,
	BIC,
	BIS,
	XOR,
	BIT,
};

#define fetch8() memread8(r[15]++)

static u16int
fetch16(void)
{
	u16int v;
	v = memread16(r[15]);
	r[15] += 2;
	return v;
}

static u32int
fetch32(void)
{
	u32int v;
	v = memread32(r[15]);
	r[15] += 4;
	return v;
}

static u32int
sxt(u32int v, int s)
{
	switch(s){
	case 0: return (s8int) v;
	case 1: return (s16int) v;
	default: return v;
	}
}

static void
nz(u32int v, int s)
{
	int i;

	i = sxt(v, s);
	ps &= ~(FLAGN|FLAGZ);
	if(i == 0) ps |= FLAGZ;
	if(i < 0) ps |= FLAGN;
}

static void
nz64(u64int v, int s)
{
	if(s < 3)
		nz(v, s);
	else{
		if(v == 0) ps |= FLAGZ;
		else if((s64int)v < 0) ps |= FLAGN;
	}
}

static void
nzv(u32int v, int s)
{
	nz(v, s);
	ps &= ~FLAGV;
}

u32int
addrof(vlong v)
{
	if(v < 0) sysfatal("addr of register or literal (pc=%.8ux)", curpc);
	return v;
}

vlong
amode(int s)
{
	u8int c;
	u32int v;
	
	s &= 0xf;
	c = fetch8();
	switch(c >> 4){
	case 0: case 1: case 2: case 3: return ~(vlong)(64 | c & 63);
	case 4: v = addrof(amode(s)); v += r[c & 15] << s; return v;
	case 5: return ~(vlong)(c & 15);
	case 6: return r[c & 15];
	case 7: return r[c & 15] -= 1<<s;
	case 8: v = r[c & 15]; r[c & 15] += 1<<s; return v;
	case 9: v = r[c & 15]; r[c & 15] += 4; return memread32(v);
	case 10: v = fetch8(); return (u32int)(r[c & 15] + (s8int) v);
	case 11: v = fetch8(); return memread32(r[c & 15] + (s8int) v);
	case 12: v = fetch16(); return (u32int)(r[c & 15] + (s16int) v);
	case 13: v = fetch16(); return memread32(r[c & 15] + (s16int) v);
	case 14: v = fetch32(); return (u32int)(r[c & 15] + v);
	case 15: v = fetch32(); return memread32(r[c & 15] + v);
	default: sysfatal("unimplemented addressing mode %.2x", c); return -1;
	}
}

u32int
readm(vlong a, int s)
{
	vlong v;

	if(a < 0){
		if(a <= ~64LL){
			if(s >= 0x10)
				return (~a & 63) << 4 | 0x4000;
			return ~a & 63;
		}
		assert(a >= ~15LL);
		v = r[~a];
		switch(s & 0xf){
		case 0: return (uchar) v;
		case 1: return (ushort) v;
		case 2: return v;
		}
	}
	switch(s & 0xf){
	case 0: return memread8(a);
	case 1: return memread16(a);
	case 2: return memread32(a);
	default: sysfatal("readm: unimplemented size %d (a=%.llx, pc=%.8x)", s, a, curpc); return -1;
	}
}

static vlong
highw(vlong v)
{
	if(v >= 0)
		return (u32int)(v + 4);
	if(v <= ~64LL)
		return ~64LL;
	return v - 1 | ~15LL;
}

u64int
readm64(vlong a, int s)
{
	u64int v;
	
	if((s & 0xf) == 3){
		v = readm(a, s - 1);
		if(a > ~64LL)
			v |= (u64int)readm(highw(a), 2) << 32;
		return v;
	}
	return readm(a, s);
}

void
writem(vlong a, u32int v, int s)
{
	int n;

	assert(a >= ~15LL);
	s &= 0xf;
	if(a < 0){
		switch(s){
		case 0: r[~a] = r[~a] & ~0xff | v & 0xff; break;
		case 1: r[~a] = r[~a] & ~0xffff | v & 0xffff; break;
		case 2: r[~a] = v; break;
		default: sysfatal("writem: unimplemented size %d", s);
		}
		return;
	}
	switch(s){
	case 0:
		n = (a & 3) << 3;
		memwrite(a & -4, v << n, 0xff << n);
		break;
	case 1:
		n = (a & 3) << 3;
		memwrite(a & -4, v << n, 0xffff << n);
		if(n == 24) memwrite(-(-a & -4), v >> 8, 0xff);
		break;
	case 2:
		n = (a & 3) << 3;
		memwrite(a & -4, v << n, -1 << n);
		if(n != 0) memwrite(-(-a & -4), v >> 32 - n, (u32int)-1 >> 32 - n);
		break;
	default: sysfatal("writem: unimplemented size %d", s);
	}
}

void
writem64(vlong a, u64int v, int s)
{
	if((s & 0xf) == 3){
		writem(a, v, 2);
		writem(highw(a), v >> 32, 2);
	}else
		writem(a, v, s);
}

static u32int
add(u32int a, u32int b, int s)
{
	int v;

	ps &= ~(FLAGC|FLAGV);
	switch(s){
	case 0:
		v = (u8int)a + (u8int)b;
		if(v >= 0x100) ps |= FLAGC;
		if(((a ^ ~b) & (v ^ a) & 0x80) != 0) ps |= FLAGV;
		return v;
	case 1:
		v = (u16int)a + (u16int)b;
		if(v >= 0x10000) ps |= FLAGC;
		if(((a ^ ~b) & (v ^ a) & 0x8000) != 0) ps |= FLAGV;
		return v;
	case 2:
		v = a + b;
		if((u32int)v < a) ps |= FLAGC;
		if(((a ^ ~b) & (v ^ a) & 0x80000000) != 0) ps |= FLAGV;
		return v;
	default:
		sysfatal("subtract: unimplemented size %d", s);
		return 0;
	}
}

static void
adwc(void)
{
	vlong ad;
	u32int a, b, v;
	u8int c;
	
	a = readm(amode(2), 2);
	ad = amode(2);
	b = readm(ad, 2);
	c = ps & FLAGC;
	ps &= ~15;
	v = a + b + c;
	if(v < a || c && v == a) ps |= FLAGC;
	if(((a ^ ~b) & (v ^ a) & 0x80000000) != 0) ps |= FLAGV;
	writem(ad, v, 2);
	nzv(v, 2);
}

static u32int
subtract(u32int a, u32int b, int s)
{
	int v;

	ps &= ~(FLAGC|FLAGV);
	switch(s){
	case 0:
		v = (u8int)b - (u8int)a;
		if(v < 0) ps |= FLAGC;
		if(((a ^ b) & (v ^ a) & 0x80) != 0) ps |= FLAGV;
		return v;
	case 1:
		v = (u16int)b - (u16int)a;
		if(v < 0) ps |= FLAGC;
		if(((a ^ b) & (v ^ a) & 0x8000) != 0) ps |= FLAGV;
		return v;
	case 2:
		v = b - a;
		if((u32int)v > b) ps |= FLAGC;
		if(((a ^ b) & (v ^ a) & 0x80000000) != 0) ps |= FLAGV;
		return v;
	default:
		sysfatal("subtract: unimplemented size %d", s);
		return 0;
	}
}

static void
sbwc(void)
{
	vlong ad;
	u32int a, b, v;
	u8int c;
	
	a = readm(amode(2), 2);
	ad = amode(2);
	b = readm(ad, 2);
	c = ps & FLAGC;
	ps &= ~15;
	v = a - b - c;
	if(v > a || c && v == a) ps |= FLAGC;
	if(((a ^ b) & (v ^ a) & 0x80000000) != 0) ps |= FLAGV;
	writem(ad, v, 2);
	nzv(v, 2);
}

static void
cmp(u32int a, u32int b, int s)
{
	ps &= ~15;
	switch(s){
	case 0:
		if((s8int) a < (s8int) b) ps |= FLAGN;
		if((u8int) a == (u8int) b) ps |= FLAGZ;
		if((u8int) a < (u8int) b) ps |= FLAGC;
		break;
	case 1:
		if((s16int) a < (s16int) b) ps |= FLAGN;
		if((u16int) a == (u16int) b) ps |= FLAGZ;
		if((u16int) a < (u16int) b) ps |= FLAGC;
		break;
	case 2:
		if((s32int) a < (s32int) b) ps |= FLAGN;
		if(a == b) ps |= FLAGZ;
		if(a < b) ps |= FLAGC;
		break;
	default:
		sysfatal("cmp: unimplemented size %d", s);
	}
}

static u32int
mul(u32int a, u32int b, int s)
{
	vlong v;

	ps &= ~(FLAGC|FLAGV);
	switch(s){
	case 0:
		v = (s8int) a * (s8int) b;
		if((uvlong)(v + 0x80) > 0xff) ps |= FLAGV;
		return v;
	case 1:
		v = (s16int) a * (s16int) b;
		if((uvlong)(v + 0x8000) > 0xffff) ps |= FLAGV;
		return v;
	case 2:
		v = (s32int)a * (s32int) b;
		if((uvlong)(v + 0x80000000) > 0xffffffff) ps |= FLAGV;
		return v;
	default:
		sysfatal("mul: unimplemented size %d", s);
		return 0;
	}
}

static u32int
div(u32int a, u32int b, int s)
{
	vlong v;

	ps &= ~(FLAGC|FLAGV);
	switch(s){
	case 0:
		if((s8int) a == 0 || (s8int) b == -0x80 && (s8int) a == -1){
			ps |= FLAGV;
			return b;
		}
		v = (s8int) b / (s8int) a;
		return v;
	case 1:
		if((s16int) b == 0 || (s16int) b == -0x8000 && (s16int) a == -1){
			ps |= FLAGV;
			return b;
		}
		v = (s16int) b / (s16int) a;
		return v;
	case 2:
		if(b == 0 || (s32int) b == -0x8000 && (s32int) a == -1){
			ps |= FLAGV;
			return b;
		}
		v = (s32int) b / (s32int) a;
		return v;
	default:
		sysfatal("div: unimplemented size %d", s);
		return 0;
	}
}

static void
alu(int o, int r, int s)
{
	u32int a, b, v;
	vlong c;
	
	switch(r){
	case 1:
		c = amode(s);
		if(o == ADD || o == SUB)
			a = 1;
		else
			a = 0;
		b = readm(c, s);
		break;
	case 2:
		a = readm(amode(s), s);
		c = amode(s);
		b = readm(c, s);
		break;
	case 3:
		a = readm(amode(s), s);
		b = readm(amode(s), s);
		c = amode(s);
		break;
	case 4:
		a = readm(amode(s), s);
		if(o == XOR)
			b = -1;
		else
			b = 0;
		c = amode(s);
		break;
	default:
		sysfatal("alu: unimplemented %d", r);
		return;
	}
	switch(o){
	case ADD: v = add(a, b, s); break;
	case SUB: v = subtract(a, b, s); break;
	case MUL: v = mul(a, b, s); break;
	case DIV: v = div(a, b, s); break;
	case CMP: cmp(a, b, s); return;
	case TST: cmp(b, 0, s); return;
	case BIC: v = ~a & b; ps &= ~FLAGV; break;
	case BIS: v = a | b; ps &= ~FLAGV; break;
	case XOR: v = a ^ b; ps &= ~FLAGV; break;
	case BIT: nzv(a & b, s); return;
	default: sysfatal("unimplemented %d in alu", o); v = 0;
	}
	nz(v, s);
	writem(c, v, s);
}

static void
ediv(void)
{
	s32int divr;
	vlong divd;
	vlong q;
	s32int r;
	vlong quo, rem;
	
	divr = readm(amode(2), 2);
	divd = readm64(amode(3), 3);
	quo = amode(2);
	rem = amode(2);
	ps &= ~15;
	if(divr == 0){
	nope:
		writem(quo, divd, 2);
		writem(rem, 0, 2);
		nz(divd, 2);
		return;
	}
	q = divd / divr;
	r = divd % divr;
	if((uvlong)(q + 0x80000000) > 0xffffffff)
		goto nope;
	writem(quo, q, 2);
	writem(rem, r, 2);
	nz(q, 2);
}

static void
move(int s)
{
	u32int v, w;
	vlong src, dst;
	
	src = amode(s);
	dst = amode(s);
	if(s != 3){
		v = readm(src, s);
		writem(dst, v, s);
		nzv(v, s);
	}else{
		v = readm(src, 2);
		w = readm(highw(src), 2);
		writem(dst, v, 2);
		writem(highw(dst), w, 2);
		nzv(w, 2);
		if(v != 0) ps &= ~FLAGZ;
	}
}

static void
cvt(int s, int t)
{
	u32int v;
	
	v = readm(amode(s), s);
	v = sxt(v, s);
	writem(amode(t), v, t);
	nzv(v, t);
	switch(t){
	case 0: if((uvlong)(v + 0x80) > 0xff) ps |= FLAGV; break;
	case 1: if((uvlong)(v + 0x8000) > 0xffff) ps |= FLAGV; break;
	}
	ps &= ~FLAGC;
}

static void
movez(int s, int t)
{
	u32int v;
	
	v = readm(amode(s), s);
	writem(amode(t), v, t);
	nzv(v, t);
}

static void
movea(int s)
{
	vlong v;
	
	v = amode(s);
	if(v < 0) sysfatal("invalid movea (pc=%.8x)", curpc);
	writem(amode(2), v, 2);
	nzv(v, 2);
}

static void
pusha(int s)
{
	vlong v;
	
	v = amode(s);
	if(v < 0) sysfatal("invalid pusha (pc=%.8x)", curpc);
	writem(r[14] -= 4, v, 2);
	nzv(v, 2);
}

static void
pushl(void)
{
	u32int v;
	
	v = readm(amode(2), 2);
	writem(r[14] -= 4, v, 2);
	nzv(v, 2);
}

static void
branch(int s, int c)
{
	int off;
	
	if(s == 0)
		off = (s8int) fetch8();
	else
		off = (s16int) fetch16();
	if(c)
		r[15] += off;
}

static void
calls(void)
{
	u32int narg, sp;
	vlong dst;
	u16int m;
	int i;
	
	narg = readm(amode(2), 2);
	dst = amode(0);
	if(dst < 0) sysfatal("call to illegal location pc=%.8ux", curpc);
	writem(r[14] -= 4, narg, 2);
	sp = r[14];
	r[14] &= -4;
	m = readm(dst, 1);
	for(i = 12; --i >= 0; )
		if((m & 1<<i) != 0)
			writem(r[14] -= 4, r[i], 2);
	writem(r[14] -= 4, r[15], 2);
	writem(r[14] -= 4, r[13], 2);
	writem(r[14] -= 4, r[12], 2);
	ps &= ~0xf;
	writem(r[14] -= 4, (sp & 3)<<30|1<<29|(m & 0xfff)<<16|ps&~0x10, 2);
	ps &= ~0xc0;
	if((m & 0x8000) != 0) ps |= 0x80;
	if((m & 0x4000) != 0) ps |= 0x40;
	writem(r[14] -= 4, 0, 2);
	r[13] = r[14];
	r[12] = sp;
	r[15] = dst + 2;
}

static void
ret(void)
{
	u32int m;
	u8int n;
	int i;

	r[14] = r[13] + 4;
	m = readm(r[14], 2);
	r[14] += 4;
	r[12] = readm(r[14], 2); r[14] += 4;
	r[13] = readm(r[14], 2); r[14] += 4;
	r[15] = readm(r[14], 2); r[14] += 4;
	for(i = 0; i < 12; i++)
		if((m & 1<<16+i) != 0){
			r[i] = readm(r[14], 2);
			r[14] += 4;
		}
	r[14] += m >> 30;
	ps = (u16int) m;
	if((m & 1<<29) != 0){
		n = readm(r[14], 2);
		r[14] += 4 + 4 * n;
	}
}

static void
bbs(int inv, int new)
{
	u32int pos;
	vlong base;
	s8int displ;
	u32int val;
	
	pos = readm(amode(2), 2);
	base = amode(0);
	displ = fetch8();
	if(base < 0){
		if(pos >= 32) sysfatal("invalid bbs (pc=%.8ux)", curpc);
		val = readm(base, 2);
		if((val >> pos & 1) != inv)
			r[15] += displ;
		if(new != 0){
			if(new > 0) val |= 1<<pos;
			else val &= ~(1<<pos);
			writem(base, val, 2);
		}
	}else{
		base += pos >> 3;
		pos &= 7;
		val = readm(base, 0);
		if((val >> pos & 1) != inv)
			r[15] += displ;
		if(new != 0){
			if(new > 0) val |= 1<<pos;
			else val &= ~(1<<pos);
			writem(base, val, 0);
		}
	}
}

static void
ashl(void)
{
	s8int cnt;
	s32int v;
	
	cnt = readm(amode(0), 0);
	v = readm(amode(2), 2);
	ps &= ~15;
	if(cnt >= 32){
		if(v != 0) ps |= FLAGV;
		v = 0;
	}else if(cnt >= 0){
		if(v + (v & 1<<31 >> cnt) != 0)
			ps |= FLAGV;
		v <<= cnt;
	}else if(cnt > -32)
		v >>= -cnt;
	else
		v >>= 31;
	nz(v, 2);
	writem(amode(2), v, 2);
}

static void
rotl(void)
{
	s8int cnt;
	s32int v;
	
	cnt = readm(amode(0), 0);
	v = readm(amode(2), 2);
	ps &= ~FLAGV;
	cnt &= 31;
	v = v << cnt | v >> 32 - cnt;
	nz(v, 2);
	writem(amode(2), v, 2);
}

static void
ashq(void)
{
	s8int cnt;
	s64int v;
	
	cnt = readm(amode(0), 0);
	v = readm64(amode(3), 3);
	ps &= ~15;
	if(cnt >= 64){
		if(v != 0) ps |= FLAGV;
		v = 0;
	}else if(cnt >= 0){
		if(v + (v & 1ULL<<63 >> cnt) != 0)
			ps |= FLAGV;
		v <<= cnt;
	}else if(cnt > -64)
		v >>= -cnt;
	else
		v >>= 63;
	nz64(v, 3);
	writem64(amode(3), v, 3);
}

static void
blb(int val)
{
	u32int v;
	s8int disp;
	
	v = readm(amode(2), 2);
	disp = fetch8();
	if((v & 1) == val)
		r[15] += disp;
}

static void
sob(int geq)
{
	vlong v;
	s32int i;
	s8int disp;
	
	v = amode(2);
	disp = fetch8();
	i = readm(v, 2) - 1;
	writem(v, i, 2);
	nzv(i, 2);
	if(i == 0x7fffffff) ps |= FLAGV;
	if(i > 0 || i == 0 && geq)
		r[15] += disp;
}

static void
aob(int leq)
{
	s32int l, v;
	vlong a;
	s8int disp;
	
	l = readm(amode(2), 2);
	a = amode(2);
	disp = fetch8();
	v = readm(a, 2) + 1;
	writem(a, v, 2);
	nzv(v, 2);
	if(v == 0x80000000) ps |= FLAGV;
	if(v < l || v == l && leq)
		r[15] += disp;
}

static void
bsb(int s)
{
	u32int v;
	
	switch(s){
	case 0:
		v = fetch8();
		writem(r[14] -= 4, r[15], 2);
		r[15] += (s8int) v;
		break;
	case 1:
		v = fetch16();
		writem(r[14] -= 4, r[15], 2);
		r[15] += (s16int) v;		
		break;
	case 2:
		v = addrof(amode(0));
		writem(r[14] -= 4, r[15], 2);
		r[15] = v;	
		break;
	}
}

static void
casei(int s)
{
	u32int sel, base, lim;
	
	sel = readm(amode(s), s);
	base = readm(amode(s), s);
	lim = readm(amode(s), s);
	sel -= base;
	if(sel <= lim)
		r[15] += (s16int) readm(r[15] + 2 * sel, 1);
	else
		r[15] += 2 * (lim + 1);
}

static void
pushr(void)
{
	u16int m;
	u32int sp;
	int i;
	
	m = readm(amode(1), 1);
	sp = r[14];
	for(i = 15; --i >= 0; )
		if((m & 1<<i) != 0)
			writem(sp -= 4, r[i], 2);
	r[14] = sp;
}

static void
popr(void)
{
	u16int m;
	u32int sp;
	int i;
	
	m = readm(amode(1), 1);
	sp = r[14];
	for(i = 0; i < 15; i++)
		if((m & 1<<i) != 0){
			r[i] = readm(sp, 2);
			sp += 4;
		}
	if((m & 1<<14) == 0)
		r[14] = sp;
}

static void
acb(int s)
{
	vlong a;
	s32int lim, n, v;
	s16int disp;
	int c;
	
	lim = readm(amode(s), s);
	n = readm(amode(s), s);
	a = amode(s);
	v = readm(a, s);
	disp = fetch16();
	
	c = ps & FLAGC;
	v = add(v, n, s);
	nz(v, s);
	ps |= c;
	writem(a, v, s);
	
	if(n >= 0 && v <= lim || n < 0 && v >= lim)
		r[15] += disp;
}

static void
extv(int sx)
{
	u32int pos, v;
	u8int c;
	u8int size;
	int i, s;
	vlong base;
	vlong dst;
	
	pos = readm(amode(2), 2);
	size = readm(amode(0), 0);
	base = amode(0);
	dst = amode(2);
	if(size > 32 || pos >= 32 && base < 0) sysfatal("extv size=%d pos=%d (pc=%#.8ux)", size, pos, curpc);
	if(base < 0){
		v = readm(base, 2) >> pos;
		if(size < 32) 
			v &= (1<<size) - 1;
	}else{
		base += pos >> 3;
		pos &= 7;
		v = 0;
		for(i = 0; i < size; i += s){
			c = readm(base, 0);
			c >>= pos;
			s = 8 - pos;
			if(s > size) s = size;
			v |= (c & (1<<s) - 1) << i;
			base++;
			pos = 0;
		}
	}
	if(sx)
		v = (s32int)(v << 32 - size) >> 32 - size;
	writem(dst, v, 2);
	ps &= ~FLAGC;
	nzv(v, 2);
}

static void
insv(void)
{
	u32int src, pos;
	u8int size;
	vlong base;
	u32int v, m;
	int i, s;

	src = readm(amode(2), 2);
	pos = readm(amode(2), 2);
	size = readm(amode(0), 0);
	base = amode(0);
	if(size > 32 || pos >= 32 && base < 0) sysfatal("extv");
	if(base < 0){
		v = readm(base, 2);
		m = (size == 32 ? 0 : 1<<size) - 1 << pos;
		v = v & ~m | src << pos & m;
		writem(base, v, 2);
	}else{
		base += pos >> 3;
		pos &= 7;
		for(i = 0; i < size; i += s){
			v = readm(base, 0);
			s = 8 - pos;
			if(s > size) s = size;
			m = (1<<s) - 1 << pos;
			v = v & ~m | src << pos & m;
			writem(base, v, 0);
			src >>= s;
			base++;
		}
	}
	ps &= ~15;
}

void addp(int, int);
void editpc(void);
void cvtlp(void);
void movp(void);
void cmpp(int);
void ashp(void);
void alufp(int, int, int);
void movefp(int, int);
void cvtfp(int, int, int);
void locc(int);
void cmpc(int);
void movc(int);
void emod(int);

void
step(void)
{
	uchar op;

	curpc = r[15];
	op = fetch8();
	if(trace || 0 && op >= 0x40 && op < 0x78)
		print("%.8ux %.2ux %.2ux %.8ux %.8ux %.8ux %.8ux\n", curpc, op, ps, r[14], r[0], r[1], r[5]);
	switch(op){
	case 0x04: ret(); break;
	case 0x05: r[15] = readm(r[14], 2); r[14] += 4; break;
	case 0x10: bsb(0); break;
	case 0x11: branch(0, 1); break;
	case 0x12: branch(0, (ps & FLAGZ) == 0); break;
	case 0x13: branch(0, (ps & FLAGZ) != 0); break;
	case 0x14: branch(0, (ps & (FLAGZ|FLAGN)) == 0); break;
	case 0x15: branch(0, (ps & (FLAGZ|FLAGN)) != 0); break;
	case 0x16: bsb(2); break;
	case 0x17: r[15] = amode(0); break;
	case 0x18: branch(0, (ps & FLAGN) == 0); break;
	case 0x19: branch(0, (ps & FLAGN) != 0); break;
	case 0x1a: branch(0, (ps & (FLAGZ|FLAGC)) == 0); break;
	case 0x1b: branch(0, (ps & (FLAGZ|FLAGC)) != 0); break;
	case 0x1c: branch(0, (ps & FLAGV) == 0); break;
	case 0x1d: branch(0, (ps & FLAGV) != 0); break;
	case 0x1e: branch(0, (ps & FLAGC) == 0); break;
	case 0x1f: branch(0, (ps & FLAGC) != 0); break;
	case 0x20: addp(4, 0); break;
	case 0x21: addp(6, 0); break;
	case 0x22: addp(4, 1); break;
	case 0x23: addp(6, 1); break;
	case 0x28: movc(0); break;
	case 0x29: cmpc(0); break;
	case 0x2c: movc(1); break;
	case 0x2d: cmpc(1); break;
	case 0x30: bsb(1); break;
	case 0x31: branch(1, 1); break;
	case 0x32: cvt(1, 2); break;
	case 0x33: cvt(1, 0); break;
	case 0x34: movp(); break;
	case 0x35: cmpp(3); break;
	case 0x37: cmpp(4); break;
	case 0x38: editpc(); break;
	case 0x3a: locc(0); break;
	case 0x3b: locc(1); break;
	case 0x3c: movez(1, 2); break;
	case 0x3d: acb(1); break;
	case 0x3e: movea(1); break;
	case 0x3f: pusha(1); break;
	case 0x40: alufp(ADD, 2, 0x12); break;
	case 0x41: alufp(ADD, 3, 0x12); break;
	case 0x42: alufp(SUB, 2, 0x12); break;
	case 0x43: alufp(SUB, 3, 0x12); break;
	case 0x44: alufp(MUL, 2, 0x12); break;
	case 0x45: alufp(MUL, 3, 0x12); break;
	case 0x46: alufp(DIV, 2, 0x12); break;
	case 0x47: alufp(DIV, 3, 0x12); break;
	case 0x48: cvtfp(0x12, 0, 0); break;
	case 0x49: cvtfp(0x12, 1, 0); break;
	case 0x4a: cvtfp(0x12, 2, 0); break;
	case 0x4b: cvtfp(0x12, 2, 1); break;
	case 0x4c: cvtfp(0, 0x12, 0); break;
	case 0x4d: cvtfp(1, 0x12, 0); break;
	case 0x4e: cvtfp(2, 0x12, 0); break;
	case 0x50: movefp(0x12, 0); break;
	case 0x51: alufp(CMP, 2, 0x12); break;
	case 0x52: movefp(0x12, 1); break;
	case 0x53: alufp(CMP, 1, 0x12); break;
	case 0x54: emod(0x12); break;
	case 0x56: cvtfp(0x12, 0x13, 0); break;
	case 0x60: alufp(ADD, 2, 0x13); break;
	case 0x61: alufp(ADD, 3, 0x13); break;
	case 0x62: alufp(SUB, 2, 0x13); break;
	case 0x63: alufp(SUB, 3, 0x13); break;
	case 0x64: alufp(MUL, 2, 0x13); break;
	case 0x65: alufp(MUL, 3, 0x13); break;
	case 0x66: alufp(DIV, 2, 0x13); break;
	case 0x67: alufp(DIV, 3, 0x13); break;
	case 0x68: cvtfp(0x13, 0, 0); break;
	case 0x69: cvtfp(0x13, 1, 0); break;
	case 0x6a: cvtfp(0x13, 2, 0); break;
	case 0x6b: cvtfp(0x13, 2, 1); break;
	case 0x6c: cvtfp(0, 0x13, 0); break;
	case 0x6d: cvtfp(1, 0x13, 0); break;
	case 0x6e: cvtfp(2, 0x13, 0); break;
	case 0x70: movefp(0x13, 0); break;
	case 0x71: alufp(CMP, 2, 0x13); break;
	case 0x72: movefp(0x13, 1); break;
	case 0x73: alufp(CMP, 1, 0x13); break;
	case 0x74: emod(0x13); break;
	case 0x76: cvtfp(0x13, 0x12, 0); break;
	case 0x78: ashl(); break;
	case 0x79: ashq(); break;
	case 0x7b: ediv(); break;
	case 0x7c: writem64(amode(3), 0, 3); nzv(0, 0); break;
	case 0x7d: move(3); break;
	case 0x7e: movea(3); break;
	case 0x7f: pusha(3); break;
	case 0x80: alu(ADD, 2, 0); break;
	case 0x81: alu(ADD, 3, 0); break;
	case 0x82: alu(SUB, 2, 0); break;
	case 0x83: alu(SUB, 3, 0); break;
	case 0x84: alu(MUL, 2, 0); break;
	case 0x85: alu(MUL, 3, 0); break;
	case 0x86: alu(DIV, 2, 0); break;
	case 0x87: alu(DIV, 3, 0); break;
	case 0x88: alu(BIS, 2, 0); break;
	case 0x89: alu(BIS, 3, 0); break;
	case 0x8a: alu(BIC, 2, 0); break;
	case 0x8b: alu(BIC, 3, 0); break;
	case 0x8c: alu(XOR, 2, 0); break;
	case 0x8d: alu(XOR, 3, 0); break;
	case 0x8e: alu(SUB, 4, 0); break;
	case 0x8f: casei(0); break;
	case 0x90: move(0); break;
	case 0x91: alu(CMP, 2, 0); break;
	case 0x92: alu(XOR, 4, 0); break;
	case 0x93: alu(BIT, 2, 0); break;
	case 0x94: writem(amode(0), 0, 0); nzv(0, 0); break;
	case 0x95: alu(TST, 1, 0); break;
	case 0x96: alu(ADD, 1, 0); break;
	case 0x97: alu(SUB, 1, 0); break;
	case 0x98: cvt(0, 2); break;
	case 0x99: cvt(0, 1); break;
	case 0x9a: movez(0, 2); break;
	case 0x9b: movez(0, 1); break;
	case 0x9c: rotl(); break;
	case 0x9d: acb(0); break;
	case 0x9e: movea(0); break;
	case 0x9f: pusha(0); break;
	case 0xa0: alu(ADD, 2, 1); break;
	case 0xa1: alu(ADD, 3, 1); break;
	case 0xa2: alu(SUB, 2, 1); break;
	case 0xa3: alu(SUB, 3, 1); break;
	case 0xa4: alu(MUL, 2, 1); break;
	case 0xa5: alu(MUL, 3, 1); break;
	case 0xa6: alu(DIV, 2, 1); break;
	case 0xa7: alu(DIV, 3, 1); break;
	case 0xa8: alu(BIS, 2, 1); break;
	case 0xa9: alu(BIS, 3, 1); break;
	case 0xaa: alu(BIC, 2, 1); break;
	case 0xab: alu(BIC, 3, 1); break;
	case 0xac: alu(XOR, 2, 1); break;
	case 0xad: alu(XOR, 3, 1); break;
	case 0xae: alu(SUB, 4, 1); break;
	case 0xaf: casei(1); break;
	case 0xb0: move(1); break;
	case 0xb1: alu(CMP, 2, 1); break;
	case 0xb2: alu(XOR, 4, 1); break;
	case 0xb3: alu(BIT, 2, 1); break;
	case 0xb4: writem(amode(1), 0, 1); nzv(0, 1); break;
	case 0xb5: alu(TST, 1, 1); break;
	case 0xb6: alu(ADD, 1, 1); break;
	case 0xb7: alu(SUB, 1, 1); break;
	case 0xba: popr(); break;
	case 0xbb: pushr(); break;
	case 0xbc: syscall(readm(amode(1), 1)); break;
	case 0xc0: alu(ADD, 2, 2); break;
	case 0xc1: alu(ADD, 3, 2); break;
	case 0xc2: alu(SUB, 2, 2); break;
	case 0xc3: alu(SUB, 3, 2); break;
	case 0xc4: alu(MUL, 2, 2); break;
	case 0xc5: alu(MUL, 3, 2); break;
	case 0xc6: alu(DIV, 2, 2); break;
	case 0xc7: alu(DIV, 3, 2); break;
	case 0xc8: alu(BIS, 2, 2); break;
	case 0xc9: alu(BIS, 3, 2); break;
	case 0xca: alu(BIC, 2, 2); break;
	case 0xcb: alu(BIC, 3, 2); break;
	case 0xcc: alu(XOR, 2, 2); break;
	case 0xcd: alu(XOR, 3, 2); break;
	case 0xce: alu(SUB, 4, 2); break;
	case 0xcf: casei(2); break;
	case 0xd0: move(2); break;
	case 0xd1: alu(CMP, 2, 2); break;
	case 0xd2: alu(XOR, 4, 2); break;
	case 0xd3: alu(BIT, 2, 2); break;
	case 0xd4: writem(amode(2), 0, 2); nzv(0, 2); break;
	case 0xd5: alu(TST, 1, 2); break;
	case 0xd6: alu(ADD, 1, 2); break;
	case 0xd7: alu(SUB, 1, 2); break;
	case 0xd8: adwc(); break;
	case 0xd9: sbwc(); break;
	case 0xdd: pushl(); break;
	case 0xde: movea(2); break;
	case 0xdf: pusha(2); break;
	case 0xe0: bbs(0, 0); break;
	case 0xe1: bbs(1, 0); break;
	case 0xe2: bbs(0, 1); break;
	case 0xe3: bbs(1, 1); break;
	case 0xe4: bbs(0, -1); break;
	case 0xe5: bbs(1, -1); break;
	case 0xe8: blb(1); break;
	case 0xe9: blb(0); break;
	case 0xee: extv(1); break;
	case 0xef: extv(0); break;
	case 0xf0: insv(); break;
	case 0xf1: acb(2); break;
	case 0xf2: aob(0); break;
	case 0xf3: aob(1); break;
	case 0xf4: sob(1); break;
	case 0xf5: sob(0); break;
	case 0xf6: cvt(2, 0); break;
	case 0xf7: cvt(2, 1); break;
	case 0xf8: ashp(); break;
	case 0xf9: cvtlp(); break;
	case 0xfb: calls(); break;
	default: sysfatal("unimplemented op %.2x (pc=%.8x)", op, curpc);
	}
}
