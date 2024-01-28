#include "os.h"
#include <mp.h>
#include "dat.h"

static char*
frompow2(char *a, mpint *b, int s)
{
	char *p, *next;
	mpdigit x;
	int i;

	i = 1<<s;
	for(p = a; (dec16chr(*p) & 255) < i; p++)
		;

	mpbits(b, (p-a)*s);
	b->top = 0;
	next = p;

	while(p > a){
		x = 0;
		for(i = 0; i < Dbits; i += s){
			if(p <= a)
				break;
			x |= dec16chr(*--p)<<i;
		}
		b->p[b->top++] = x;
	}
	return next;
}

static char*
from8(char *a, mpint *b)
{
	char *p, *next;
	mpdigit x, y;
	int i;

	for(p = a; ((*p - '0') & 255) < 8; p++)
		;

	mpbits(b, (p-a)*3);
	b->top = 0;
	next = p;

	i = 0;
	x = y = 0;
	while(p > a){
		y = *--p - '0';
		x |= y << i;
		i += 3;
		if(i >= Dbits){
Digout:
			i -= Dbits;
			b->p[b->top++] = x;
			x = y >> (3-i);
		}
	}
	if(i > 0)
		goto Digout;

	return next;
}

static ulong mppow10[] = {
	1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000
};

static char*
from10(char *a, mpint *b)
{
	ulong x, y;
	mpint *pow, *r;
	int i;

	pow = mpnew(0);
	r = mpnew(0);

	b->top = 0;
	for(;;){
		// do a billion at a time in native arithmetic
		x = 0;
		for(i = 0; i < 9; i++){
			y = *a - '0';
			if(y > 9)
				break;
			a++;
			x *= 10;
			x += y;
		}
		if(i == 0)
			break;

		// accumulate into mpint
		uitomp(mppow10[i], pow);
		uitomp(x, r);
		mpmul(b, pow, b);
		mpadd(b, r, b);
		if(i < 9)
			break;
	}
	mpfree(pow);
	mpfree(r);
	return a;
}

static char*
fromdecx(char *a, mpint *b, int (*chr)(int), int (*dec)(uchar*, int, char*, int))
{
	char *buf = a;
	uchar *p;
	int n, m;

	b->top = 0;
	for(; (*chr)(*a) >= 0; a++)
		;
	n = a-buf;
	if(n > 0){
		p = malloc(n);
		if(p == nil)
			sysfatal("malloc: %r");
		m = (*dec)(p, n, buf, n);
		if(m > 0)
			betomp(p, m, b);
		free(p);
	}
	return a;
}

mpint*
strtomp(char *a, char **pp, int base, mpint *b)
{
	int sign;
	char *e;

	if(b == nil){
		b = mpnew(0);
		setmalloctag(b, getcallerpc(&a));
	}

	while(*a==' ' || *a=='\t')
		a++;

	sign = 1;
	for(;; a++){
		switch(*a){
		case '-':
			sign *= -1;
			continue;
		}
		break;
	}

	if(base == 0){
		base = 10;
		if(a[0] == '0'){
			if(a[1] == 'x' || a[1] == 'X') {
				a += 2;
				base = 16;
			} else if(a[1] == 'b' || a[1] == 'B') {
				a += 2;
				base = 2;
			} else if(a[1] >= '0' && a[1] <= '7') {
				a++;
				base = 8;
			}
		}
	}

	switch(base){
	case 2:
		e = frompow2(a, b, 1);
		break;
	case 4:
		e = frompow2(a, b, 2);
		break;
	case 8:
		e = from8(a, b);
		break;
	case 10:
		e = from10(a, b);
		break;
	case 16:
		e = frompow2(a, b, 4);
		break;
	case 32:
		e = fromdecx(a, b, dec32chr, dec32);
		break;
	case 64:
		e = fromdecx(a, b, dec64chr, dec64);
		break;
	default:
		abort();
	}

	if(pp != nil)
		*pp = e;

	// if no characters parsed, there wasn't a number to convert
	if(e == a)
		return nil;

	b->sign = sign;
	return mpnorm(b);
}
