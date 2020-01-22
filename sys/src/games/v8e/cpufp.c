#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

/* BUGS: Not bit accurate. */

enum {
	ADD,
	SUB,
	MUL,
	DIV,
	CMP,
};

enum {
	EBIAS = 129
};

#define zero(x) (((x) & 0xff80) == 0)
#define inval(x) (((x) & 0xff80) == 0x8000)
#define expo(x) ((x) >> 7 & 0xff)
#define mantf(x) (1<<23 | ((x) & 0x7f) << 16 | (x) >> 16)
#define mantd(x) (1ULL<<55|((x)&0x7f)<<48|((x)&0xffff0000)<<16| \
	(x)>>16&0xffff0000|(u16int)((x)>>48))
#define sign(x) ((int)x << 16 >> 30 | 1)
#define makef(s, e, m) ((s)&0x8000|(e)<<7|(m)<<16|(m)>>16&0x7f)
#define maked(s, e, m) (s&0x8000|(e)<<7|(uvlong)(m)<<48|(uvlong)((m)&0xffff0000)<<16| \
	(m)>>16&0xffff0000|(m)>>48&0x7f)

static double
vfc(u32int a)
{
	union { u32int a; float b; } u;
	
	if(zero(a)) return 0;
	a -= 0x100;
	u.a = a >> 16 | a << 16;
	return u.b;
}

static double
vdc(u64int a)
{
	union { u64int a; double b; } u;
	
	if(zero(a)) return 0;
	u.a = mantd(a) >> 3 & (1ULL<<52)-1 | expo(a) + 894 << 52 | sign(a) & 1ULL<<63;
	return u.b;
}

static int
clz32(u32int a)
{
	int n;
	static uchar c[16] = {4, 3, 2, 2, 1, 1, 1, 1};

	n = 0;
	if((a >> 16) == 0){n += 16; a <<= 16;}
	if((a >> 24) == 0){n += 8; a <<= 8;}
	if((a >> 28) == 0){n += 4; a <<= 4;}
	return n + c[a >> 28];
}

static int
clz64(uvlong a)
{
	int n;
	static uchar c[16] = {4, 3, 2, 2, 1, 1, 1, 1};

	n = 0;
	if((a >> 32) == 0){n += 32; a <<= 32;}
	if((a >> 48) == 0){n += 16; a <<= 16;}
	if((a >> 56) == 0){n += 8; a <<= 8;}
	if((a >> 60) == 0){n += 4; a <<= 4;}
	return n + c[a >> 60];
}

static int
magcmpd(u64int a, u64int b)
{
	int e;
	s64int m;

	e = expo(a) - expo(b);
	if(e > 0) return 1;
	if(e < 0) return -1;
	m = mantd(a) - mantd(b);
	if(m > 0) return 1;
	if(m < 0) return -1;
	return 0;
}

static int
cmpd(u64int a, u64int b)
{
	int s;

	if(zero(a)){
		if(zero(b)) return 0;
		return -sign(b);
	}
	if(zero(b)) return sign(a);
	s = sign(a) - sign(b);
	if(s > 0) return 1;
	if(s < 0) return -1;
	return magcmpd(a, b);
}

static u32int
addf(u32int a, u32int b, int sub)
{
	int e1, e2, m1, m2, s1, s2;
	int n;
	u32int c;

	if(inval(a) || inval(b)) return 0x8000;
	if(zero(b)) return a;
	if(sub) b ^= 0x8000;
	if(zero(a)) return b;
	if(magcmpd(a, b) < 0){
		c = a;
		a = b;
		b = c;
	}
	e1 = expo(a); m1 = mantf(a); s1 = sign(a);
	e2 = expo(b); m2 = mantf(b); s2 = sign(b);
	if(e1 - e2 >= 24) return a;
	m2 >>= e1 - e2;
	if(s1 == s2)
		m1 += m2;
	else
		m1 -= m2;
	if(m1 == 0) return 0;
	n = 8 - clz32(m1);
	e1 += n;
	if(n > 0) m1 >>= n;
	else m1 <<= -n;
	return makef(s1, e1, m1);
}

static u32int
mulf(u32int a, u32int b)
{
	int e1, e2, m1, m2, s1, s2, l;
	
	if(zero(a) || zero(b)) return 0;
	e1 = expo(a); m1 = mantf(a); s1 = sign(a);
	e2 = expo(b); m2 = mantf(b); s2 = sign(b);
	s1 ^= s2 & -2;
	e1 += e2 - EBIAS;
	if(e1 <= 0) return 0;
	l = (uvlong)m1 * m2 + (1<<22) >> 23;
	if((l & 1<<24) != 0){
		l >>= 1;
		e1++;
	}
	if(e1 >= 256) return 0x8000;
	return makef(s1, e1, l);
}

static u32int
divf(u32int a, u32int b)
{
	int e1, e2, m1, m2, s1, s2;
	uvlong l;

	if(zero(a)) return 0;
	if(zero(b)) return 0x8000;
	e1 = expo(a); m1 = mantf(a); s1 = sign(a);
	e2 = expo(b); m2 = mantf(b); s2 = sign(b);
	s1 ^= s2 & -2;
	e1 -= e2 - EBIAS;
	l = (uvlong) m1 << 40;
	l /= m2;
	l >>= 17;
	if(l == 0) return 0;
	while((l & 1<<23) == 0){
		l <<= 1;
		e1--;
	}
	if(e1 <= 0) return 0;
	if(e1 >= 256) return 0x8000;
	return makef(s1, e1, l);
}

static u64int
addd(u64int a, u64int b, int sub)
{
	int e1, e2, s1, s2;
	u64int m1, m2;
	int n;
	u64int c;

	if(inval(a) || inval(b)) return 0x8000;
	if(zero(b)) return a;
	if(sub) b ^= 0x8000;
	if(zero(a)) return b;
	if(magcmpd(a, b) < 0){
		c = a;
		a = b;
		b = c;
	}
	e1 = expo(a); m1 = mantd(a); s1 = sign(a);
	e2 = expo(b); m2 = mantd(b); s2 = sign(b);
	if(e1 - e2 >= 56) return a;
	m2 >>= e1 - e2;
	if(s1 == s2)
		m1 += m2;
	else
		m1 -= m2;
	if(m1 == 0) return 0;
	n = 8 - clz64(m1);
	e1 += n;
	if(n > 0) m1 >>= n;
	else m1 <<= -n;
	return maked(s1, e1, m1);
}

static u64int
mul55(u64int a, u64int b)
{
	u64int l;

	l = (uvlong)(u32int)a * (u32int)b >> 32;
	l += (a >> 32) * (u32int)b;
	l += (u32int)a * (b >> 32);
	l = l + (1<<21) >> 22;
	l += (a >> 32) * (b >> 32) << 10;
	l = l + 1 >> 1;
	return l;
}

static u64int
mul62(u64int a, u64int b)
{
	u64int l;

	l = (uvlong)(u32int)a * (u32int)b >> 32;
	l += (a >> 32) * (u32int)b;
	l += (u32int)a * (b >> 32);
	l = l + (1<<28) >> 29;
	l += (a >> 32) * (b >> 32) << 3;
	l = l + 1 >> 1;
	return l;
}

static u64int
muld(u64int a, u64int b)
{
	int e1, e2, s1, s2;
	uvlong m1, m2;
	uvlong l;
	
	if(zero(a) || zero(b)) return 0;
	e1 = expo(a); m1 = mantd(a); s1 = sign(a);
	e2 = expo(b); m2 = mantd(b); s2 = sign(b);
	s1 ^= s2 & -2;
	e1 += e2 - EBIAS;
	if(e1 <= 0) return 0;
	l = mul55(m1, m2);
	if((l & 1ULL<<56) != 0){
		l >>= 1;
		e1++;
	}
	if(e1 >= 256) return 0x8000;
	return maked(s1, e1, l);
}

static u64int
divd(u64int a, u64int b)
{
	int e1, e2, s1, s2;
	uvlong m1, m2, l;

	if(zero(a)) return 0;
	if(zero(b)) return 0x8000;
	e1 = expo(a); m1 = mantd(a); s1 = sign(a);
	e2 = expo(b); m2 = mantd(b); s2 = sign(b);
	s1 ^= s2 & -2;
	e1 -= e2 - EBIAS;
	l = (1ULL<<63) / (m2 >> 28) << 26;
	m2 <<= 7;
	l = mul62(l, (1ULL<<63) - mul62(l, m2));
	l = mul62(l, (1ULL<<63) - mul62(l, m2));
	l = mul62(l, (1ULL<<63) - mul62(l, m2));
	l = mul62(l, m1 << 7);
	l += 1<<6;
	l >>= 7;
	if(l == 0) return 0;
	while((l & 1ULL<<55) == 0){
		l <<= 1;
		e1--;
	}
	if(e1 <= 0) return 0;
	if(e1 >= 256) return 0x8000;
	return maked(s1, e1, l);
}

void
alufp(int o, int r, int s)
{
	u64int a, b, v;
	vlong c;
	int i;
	
	switch(r){
	case 2:
		b = readm64(amode(s), s);
		c = amode(s);
		a = readm64(c, s);
		break;
	case 3:
		b = readm64(amode(s), s);
		a = readm64(amode(s), s);
		c = amode(s);
		break;
	default: sysfatal("alufp: r==%d", r);
	}
	switch(o){
	case ADD:
		if(s == 0x13) v = addd(a, b, 0);
		else v = addf(a, b, 0);
		break;
	case SUB:
		if(s == 0x13) v = addd(a, b, 1);
		else v = addf(a, b, 1);
		break;
	case MUL:
		if(s == 0x13) v = muld(a, b);
		else v = mulf(a, b);
		break;
	case DIV:
		if(s == 0x13) v = divd(a, b);
		else v = divf(a, b);
		break;
	case CMP:
		ps &= ~15;
		i = cmpd(b, a);
		if(i < 0) ps |= FLAGN;
		if(i == 0) ps |= FLAGZ;
		return;
	default:
		sysfatal("alufp: unimplemented op=%d", o);
	}
//	print("%.8ux %d %20.16g %20.16g %20.16g\n", curpc, o, vdc(a), vdc(b), vdc(v));
	ps &= ~15;
	if(zero(v)) ps |= FLAGZ;
	if((v & 0x8000) != 0) ps |= FLAGN;
	writem64(c, v, s);
}

static u64int
itof(s32int i)
{
	int n;
	u64int l;

	l = 0;
	if(i < 0){
		l |= 0x8000;
		i = -i;
	}else if(i == 0)
		return 0;
	n = clz32(i);
	l |= maked(0, 160 - n, (uvlong)i << 24 + n);
	return l;
}

static s64int
ftoi(u64int l)
{
	int s, e;
	
	s = sign(l);
	e = expo(l);
	l = mantd(l);
	if(e >= EBIAS + 64) return 1LL<<63;
	if(e < EBIAS) return 0;
	l >>= EBIAS + 55 - e;
	if(s < 0) return -l;
	else return l;
}

void
cvtfp(int s, int t, int r)
{
	u64int l;
	int si, e;

	switch(s){
	case 0: l = itof((s8int) readm(amode(0), 0)); break;
	case 1: l = itof((s16int) readm(amode(1), 1)); break;
	case 2: l = itof(readm(amode(2), 2)); break;
	case 0x12: l = readm(amode(2), 2); break;
	case 0x13: l = readm64(amode(3), 3); break;
	default: sysfatal("cvtfp: s==%d", s);
	}
	if(r) l = addd(l, maked(sign(l), 128, 0), 0);
	if(t < 0x10) l = ftoi(l);
	ps &= ~15;
	switch(t){
	case 0:
		if((s64int)l != (s8int)l) ps |= FLAGV;
		l = (s8int) l;
		break;
	case 1:
		if((s64int)l != (s16int)l) ps |= FLAGV;
		l = (s16int) l;
		break;
	case 2:
		if((s64int)l != (s32int)l) ps |= FLAGV;
		l = (s32int) l;
		break;	
	case 0x12:
		si = sign(l);
		e = expo(l);
		l = mantd(l);
		l += 1ULL<<31;
		if((l & 1ULL<<56) != 0){
			l >>= 1;
			e++;
		}
		l = maked(si, e, l);
		break;
	}
	writem64(amode(t), l, t);
	if(t >= 0x10){
		if(zero(l)) ps |= FLAGZ;
		if((l & 0x8000) != 0) ps |= FLAGN;
	}else{
		if(l == 0) ps |= FLAGZ;
		if((s64int)l < 0) ps |= FLAGN;
	}
}

void
movefp(int t, int n)
{
	u64int x;

	x = readm64(amode(t), t);
	if(inval(x)) sysfatal("invalid float");
	ps &= ~(FLAGN|FLAGZ|FLAGV);
	if(zero(x))
		ps |= FLAGZ;
	else{
		if(n) x ^= 0x8000;
		if((x & 0x8000) != 0) ps |= FLAGN;
	}
	writem64(amode(t), x, t);
}

void
emod(int s)
{
	u64int a, b, m1, m2, l;
	u8int a8;
	vlong ai, af;
	int e1, e2, s1, s2, n;

	a = readm64(amode(s), s);
	a8 = readm(amode(0), 0);
	b = readm64(amode(s), s);
	ai = amode(2);
	af = amode(s);
	
	if(zero(a) || zero(b)){
		ps = ps & ~15 | FLAGZ;
		writem(ai, 0, 2);
		writem64(af, 0, s);
		return;
	}
	e1 = expo(a); m1 = mantd(a) << 8 | a8; s1 = sign(a);
	e2 = expo(b); m2 = mantd(b); s2 = sign(b);
	s1 ^= s2 & -2;
	e1 += e2 - EBIAS;
	if(e1 <= 0){
		ps = ps & ~15 | FLAGZ;
		writem(ai, 0, 2);
		writem64(af, 0, s);
		return;
	}
	l = (uvlong)(u32int)m1 * (u32int)m2 >> 32;
	l += (m1 >> 32) * (u32int)m2;
	l += (u32int)m1 * (m2 >> 32);
	l = l + (1<<29) >> 30;
	l += (m1 >> 32) * (m2 >> 32) << 2;
	l = l + 1 >> 1;
	while((l & 1ULL<<56) != 0){
		l = l + 1 >> 1;
		e1++;
	}
	if(e1 >= 256){
		ps |= FLAGV;
		return;
	}
	if(e1 < EBIAS){
		writem(ai, 0, 2);
		writem64(af, maked(s1, e1, l), s);
		if(s1 < 0) ps |= FLAGN;
		return;
	}
	writem(ai, l >> 55+EBIAS-e1, 2);
	l &= (1ULL<<55+EBIAS-e1) - 1;
	if(l == 0){
		writem64(af, 0, s);
		ps |= FLAGZ;
		return;
	}
	n = clz64(l)-8;
	l <<= n;
	e1 -= n;
	writem64(af, maked(s1, e1, l), s);
	if(s1 < 0) ps |= FLAGN;
}

void
fptest(void)
{
}
