#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

static s8int
getsign(u16int l, u32int a)
{
	u8int c;
	
	if(l == 0) return 1;
	c = readm(a + l/2, 0);
	c &= 0xf;
	if(c == 11 || c == 13) return -1;
	return 1;
}

static int
getdig(u16int l, u32int a, int i)
{
	u8int c;

	if(i >= l) return 0;
	i = l - 1 - i;
	if((l & 1) == 0) i++;
	a += i/2;
	c = readm(a, 0);
	if((i & 1) == 0) return c >> 4;
	return c & 0xf;
}

static void
putdig(u16int l, u32int a, int i, int v)
{
	u8int c;

	if(i >= l){
		if(v != 0)
			ps |= FLAGV;
		return;
	}
	i = l - 1 - i;
	if((l & 1) == 0) i++;
	a += i/2;
	if((l & 1) == 0 && i == 1)
		c = 0;
	else
		c = readm(a, 0);
	if((i & 1) == 0) c = c & 0x0f | v << 4;
	else c = c & 0xf0 | v;
	writem(a, c, 0);
}

static void
putsign(u16int l, u32int a, s8int s)
{
	u8int c;

	a += l/2;
	c = readm(a, 0);
	c = c & 0xf0 | 12 | s < 0;
	writem(a, c, 0);
}

static u32int
sigdig(u16int l, u32int a)
{
	u32int e;
	
	e = a + l/2 + 1;
	for(; a < e; a++)
		if(readm(a, 0) != 0)
			return a;
	return a;
}

void
cvtlp(void)
{
	s32int x;
	u16int l;
	u32int a;
	u8int v;
	int i;
	
	x = readm(amode(2), 2);
	l = readm(amode(1), 1);
	a = addrof(amode(0));
	
	ps = ps & ~15 | FLAGZ;
	if(x < 0){
		x = -x;
		ps |= FLAGN;
	}
	for(i = 0; i < l; i++){
		v = x % 10;
		x /= 10;
		putdig(l, a, i, v);
		if(v != 0) ps &= ~FLAGZ;
	}
	if(x != 0) ps |= FLAGV;
	if((ps & (FLAGN|FLAGZ)) == 0)
		ps &= ~FLAGN;
	putsign(l, a, -((ps & FLAGN) != 0));
	r[0] = 0;
	r[1] = 0;
	r[2] = 0;
	r[3] = sigdig(l, a);
}

static uchar
editread(void)
{
	u8int rc;

	if((s32int) r[0] <= 0){
		if(r[0] == 0)
			sysfatal("editread");
		r[0] += 0x10000;
		return 0;
	}else{
		rc = readm(r[1], 0);
		if((r[0] & 1) != 0)
			rc >>= 4;
		else
			rc &= 0xf;
		r[0]--;
		if((r[0] & 1) != 0)
			r[1]++;
		return rc;
	}
}

void
editpc(void)
{
	u8int p, c;
	int i;
	
	r[0] = readm(amode(1), 1);
	r[1] = addrof(amode(0));
	r[2] = 0x2020;
	r[3] = addrof(amode(0));
	r[5] = addrof(amode(0));
	
	ps = ps & ~15 | FLAGZ;
	c = readm(r[1] + r[0]/2, 0) & 0xf;
	if(c == 11 || c == 13){
		ps |= FLAGN;
		r[2] |= 0xd00;
	}
	
	for(;;){
		p = readm(r[3]++, 0);
		switch(p){
		case 0x00:
			if(r[0] != 0) sysfatal("editpc");
			if((ps & FLAGZ) != 0)
				ps &= ~FLAGN;
			return;
		case 0x01:
			if((ps & FLAGC) == 0){
				writem(r[5]++, r[2] >> 8, 0);
				ps |= FLAGC;
			}
			break;
		case 0x02: ps &= ~FLAGC; break;
		case 0x03: ps |= FLAGC; break;
		case 0x04: writem(r[5]++, r[2] >> 8, 0); break;
		case 0x40: r[2] = r[2] & ~0xff | (u8int)readm(r[3]++, 0); break;
		case 0x41: r[2] = r[2] & ~0xff00 | (u8int)readm(r[3]++, 0) << 8; break;
		case 0x42:
			p = readm(r[3]++, 0);
			if((ps & FLAGN) == 0)
				r[2] = r[2] & ~0xff00 | p << 8;
			break;
		case 0x43:
			p = readm(r[3]++, 0);
			if((ps & FLAGN) != 0)
				r[2] = r[2] & ~0xff00 | p << 8;
			break;
		case 0x44:
			p = readm(r[3]++, 0);
			if((ps & FLAGC) != 0)
				writem(r[5]++, p, 0);
			else
				writem(r[5]++, r[2], 0);
			break;
		case 0x45:
			p = readm(r[3]++, 0);
			if((ps & FLAGZ) != 0){
				r[5] -= p;
				while(p-- != 0)
					writem(r[5]++, r[2], 0);
			}
			break;
		case 0x46:
			p = readm(r[3]++, 0);
			if((ps & (FLAGN|FLAGZ)) == (FLAGN|FLAGZ))
				writem(r[5] - p, r[2], 0);
			break;
		case 0x47:
			p = readm(r[3]++, 0);
			if((u16int)r[0] > p){
				r[0] = (u16int) r[0];
				i = r[0] - p;
				while(i-- > 0)
					if(editread() != 0)
						ps = ps & ~FLAGZ | FLAGV;
			}else
				r[0] = (u16int) r[0] | r[0] - p << 16;
		case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
		case 0x88: case 0x89: case 0x8a: case 0x8b: case 0x8c: case 0x8d: case 0x8e: case 0x8f:
			for(; p > 0x80; p--)
				writem(r[5]++, r[2], 0);
			break;
		case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
		case 0x98: case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d: case 0x9e: case 0x9f:
			for(; p > 0x90; p--){
				c = editread();
				if(c != 0) ps = ps & ~FLAGZ | FLAGC;
				if((ps & FLAGC) == 0)
					writem(r[5]++, r[2], 0);
				else
					writem(r[5]++, '0' + c, 0);
			}
			break;
		case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa4: case 0xa5: case 0xa6: case 0xa7:
		case 0xa8: case 0xa9: case 0xaa: case 0xab: case 0xac: case 0xad: case 0xae: case 0xaf:
			for(; p > 0xa0; p--){
				c = editread();
				if(c != 0){
					if((ps & FLAGC) == 0)
						writem(r[5]++, r[2] >> 8, 0);
					ps = ps & ~FLAGZ | FLAGC;
				}
				if((ps & FLAGC) == 0)
					writem(r[5]++, r[2], 0);
				else
					writem(r[5]++, '0' + c, 0);
			}
			break;
		default: sysfatal("editpc: unknown pattern %.2x (pc=%.8ux)", p, curpc);
		}
	}
}

void
movp(void)
{
	u16int l;
	u32int sa, da;
	u8int c, d;
	int i, n;
	
	l = readm(amode(1), 1);
	sa = addrof(amode(0));
	da = addrof(amode(1));
	n = l/2 + 1;
	ps = ps & ~(FLAGN|FLAGV) | FLAGZ;
	for(i = 0; i < n-1; i++){
		c = readm(sa++, 0);
		writem(da++, c, 0);
		if(c != 0) ps &= ~FLAGZ;
	}
	c = readm(sa, 0);
	if((c & 0xf0) != 0) ps &= ~FLAGZ;
	d = c & 0xf0;
	c &= 0xf;
	if(c == 11 || c == 13) ps |= FLAGN;
	if((ps & (FLAGN|FLAGZ)) == (FLAGN|FLAGZ)) ps &= ~FLAGN;
	if((ps & FLAGN) != 0) d |= 13;
	else d |= 12;
	writem(da, d, 0);
}


static int
cmpmag(u16int l1, u32int a1, u16int l2, u32int a2)
{
	int i;
	u8int c1, c2;
	
	for(i = l1 > l2 ? l1 : l2; --i >= 0; ){
		c1 = getdig(l1, a1, i);
		c2 = getdig(l2, a2, i);
		if(c1 > c2) return 1;
		if(c1 < c2) return -1;
	}
	return 0;
}

void
addp(int rn, int sub)
{
	u16int l1, l2, l3;
	u32int a1, a2, a3;
	s8int s1, s2;
	int c, i, l, t;
	
	l1 = readm(amode(1), 1);
	a1 = addrof(amode(0));
	l2 = readm(amode(1), 1);
	a2 = addrof(amode(0));
	if(rn == 6){
		l3 = readm(amode(1), 1);
		a3 = addrof(amode(0));
	}else{
		l3 = l2;
		a3 = a2;
	}
	s1 = getsign(l1, a1);
	s2 = getsign(l2, a2);
	r[0] = 0;
	r[1] = sigdig(l1, a1);
	r[2] = 0;
	r[3] = sigdig(l2, a2);
	r[4] = 0;
	if(sub) s1 ^= -2;
	if(s1 == s2){
		c = 0;
		l = l1;
		if(l2 > l) l = l2;
		if(l3 > l) l = l3;
		for(i = 0; i < l; i++){
			t = c + getdig(l1, a1, i) + getdig(l2, a2, i);
			for(c = 0; t >= 10; c++) t -= 10;
			putdig(l3, a3, i, t);
		}
	}else{
		if(cmpmag(l1, a1, l2, a2) < 0){
			t = l1; l1 = l2; l2 = t;
			t = a1; a1 = a2; a2 = t;
			s1 = s2;
		}
		c = 0;
		l = l3 > l1 ? l3 : l1;
		for(i = 0; i < l; i++){
			t = getdig(l1, a1, i) - getdig(l2, a2, i);
			for(c = 0; t < 0; c++) t += 10;
			putdig(l3, a3, i, t);
		}
	}
	if(c != 0) ps |= FLAGV;
	putsign(l3, a3, s1);
	r[5] = sigdig(l3, a3);
}

void
cmpp(int rn)
{
	u16int l1, l2;
	u32int a1, a2;
	s8int s1, s2;
	int rc;
	
	l1 = readm(amode(1), 1);
	a1 = addrof(amode(0));
	if(rn == 4)
		l2 = readm(amode(1), 1);
	else
		l2 = l1;
	a2 = addrof(amode(0));
	s1 = getsign(l1, a1);
	s2 = getsign(l2, a2);
	r[0] = 0;
	r[1] = sigdig(l1, a1);
	r[2] = 0;
	r[3] = sigdig(l2, a2);
	ps &= ~15;
	if(s1 != s2){
		if(s1 < s2) ps |= FLAGN;
		return;
	}
	rc = cmpmag(l1, a1, l2, a2);
	if(rc == 0) ps |= FLAGZ;
	else if(rc > 0) ps |= FLAGN;
}

void
ashp(void)
{
	s8int cnt, rnd;
	s16int sl, dl;
	u32int sa, da;
	int i, c, x;
	
	cnt = readm(amode(0), 0);
	sl = readm(amode(1), 1);
	sa = addrof(amode(2));
	rnd = readm(amode(0), 0);
	dl = readm(amode(1), 1);
	da = addrof(amode(2));
	ps = ps & ~15 | FLAGZ;
	x = getsign(sl, sa);
	if(x < 0) ps |= FLAGN;
	putsign(dl, da, x);
	for(i = 0; i < cnt; i++)
		putdig(dl, da, i, 0);
	c = cnt < 0 && getdig(sl, sa, -1-cnt) >= rnd;
	for(i = cnt >= 0 ? 0 : -cnt; i < sl; i++){
		x = getdig(sl, sa, i) + c;
		for(c = 0; x >= 10; c++) x -= 10;
		putdig(dl, da, i + cnt, x);
		if(x != 0) ps &= ~FLAGZ;
	}
	putdig(dl, da, i + cnt, c);
	r[0] = 0;
	r[1] = sigdig(sl, sa);
	r[2] = 0;
	r[3] = sigdig(dl, da);
}

void
locc(int inv)
{
	u8int chr;
	u16int len;
	u32int addr;
	
	chr = readm(amode(0), 0);
	len = readm(amode(1), 1);
	addr = addrof(amode(0));
	ps &= ~15;
	for(; len != 0; addr++, len--)
		if(inv ^ (readm(addr, 0) == chr))
			break;
	if(len == 0) ps |= FLAGZ;
	r[0] = len;
	r[1] = addr;
}

void
cmpc(int op5)
{
	u16int l1, l2;
	u32int a1, a2;
	u8int a, b, f;
	
	if(op5){
		l1 = readm(amode(1), 1);
		a1 = addrof(amode(2));
		f = readm(amode(0), 0);
		l2 = readm(amode(1), 1);
		a2 = addrof(amode(2));
	}else{
		l1 = l2 = readm(amode(1), 1);
		a1 = addrof(amode(2));
		a2 = addrof(amode(2));
		f = 0;
	}
	a = b = f;
	for(; l1 > 0 && l2 > 0; l1--, l2--, a1++, a2++){
		a = readm(a1, 0);
		b = readm(a2, 0);
		if(a != b) goto ineq;
	}
	for(; l1 > 0; l1--, a1++){
		a = readm(a1, 0);
		b = f;
		if(a != b) goto ineq;
	}
	for(; l2 > 0; l2--, a2++){
		a = f;
		b = readm(a2, 0);
		if(a != b) goto ineq;
	}
ineq:
	ps = ps & ~15;
	if((s8int)a < (s8int)b) ps |= FLAGN;
	if(a == b) ps |= FLAGZ;
	if(a < b) ps |= FLAGC;
	r[0] = l1;
	r[1] = a1;
	r[2] = l2;
	r[3] = a2;
}

void
movc(int op5)
{
	u16int sl, dl, l;
	u32int sa, da;
	int i;
	u8int f;
	
	sl = readm(amode(1), 1);
	sa = addrof(amode(0));
	if(op5){
		f = readm(amode(0), 0);
		dl = readm(amode(1), 1);
	}else{
		f = 0;
		dl = sl;
	}
	da = addrof(amode(0));
	l = sl < dl ? sl : dl;
	if(sa < da)
		for(i = l; --i >= 0; )
			writem(da + i, readm(sa + i, 0), 0);
	else
		for(i = 0; i < l; i++)
			writem(da + i, readm(sa + i, 0), 0);
	for(i = l; i < dl; i++)
		writem(da + i, f, 0);
	r[0] = sl - l;
	r[1] = sa + sl;
	r[2] = 0;
	r[3] = da + dl;
	r[4] = 0;
	r[5] = 0;
	ps &= ~15;
	if((s16int) sl < (s16int) dl) ps |= FLAGN;
	else if(sl == dl) ps |= FLAGZ;
	if(sl < dl) ps |= FLAGC;
}
