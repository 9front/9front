#include "os.h"
#include <mp.h>
#include "dat.h"

double
mptod(mpint *a)
{
	u64int v;
	mpdigit w, r;
	int sf, i, n, m, s;
	FPdbleword x;
	
	if(a->top == 0) return 0.0;
	sf = mpsignif(a);
	if(sf > 1024) return Inf(a->sign);
	i = a->top - 1;
	v = a->p[i];
	n = sf & Dbits - 1;
	n |= n - 1 & Dbits;
	r = 0;
	if(n > 54){
		s = n - 54;
		r = v & (1<<s) - 1;
		v >>= s;
	}
	while(n < 54){
		if(--i < 0)
			w = 0;
		else
			w = a->p[i];
		m = 54 - n;
		if(m > Dbits) m = Dbits;
		s = Dbits - m & Dbits - 1;
		v = v << m | w >> s;
		r = w & (1<<s) - 1;
		n += m;
	}
	if((v & 3) == 1){
		while(--i >= 0)
			r |= a->p[i];
		if(r != 0)
			v++;
	}else
		v++;
	v >>= 1;
	while((v >> 53) != 0){
		v >>= 1;
		if(++sf > 1024)
			return Inf(a->sign);
	}
	x.lo = v;
	x.hi = (u32int)(v >> 32) & (1<<20) - 1 | sf + 1022 << 20 | a->sign & 1<<31;
	return x.x;
}

mpint *
dtomp(double d, mpint *a)
{
	FPdbleword x;
	uvlong v;
	int e;

	if(a == nil)
		a = mpnew(0);
	x.x = d;
	e = x.hi >> 20 & 2047;
	assert(e != 2047);
	if(e < 1022){
		mpassign(mpzero, a);
		return a;
	}
	v = x.lo | (uvlong)(x.hi & (1<<20) - 1) << 32 | 1ULL<<52;
	if(e < 1075){
		v += (1ULL<<1074 - e) - (~v >> 1075 - e & 1);
		v >>= 1075 - e;
	}
	uvtomp(v, a);
	if(e > 1075)
		mpleft(a, e - 1075, a);
	if((int)x.hi < 0)
		a->sign = -1;
	return a;
}
