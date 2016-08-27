#include "os.h"
#include <mp.h>
#include "dat.h"

static struct {
	int	inited;

	uchar	t64[256];
	uchar	t32[256];
	uchar	t16[256];
	uchar	t10[256];
} tab;

enum {
	INVAL=	255
};

static char set64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static char set32[] = "23456789abcdefghijkmnpqrstuvwxyz";
static char set16[] = "0123456789ABCDEF0123456789abcdef";
static char set10[] = "0123456789";

static void
init(void)
{
	char *p;

	memset(tab.t64, INVAL, sizeof(tab.t64));
	memset(tab.t32, INVAL, sizeof(tab.t32));
	memset(tab.t16, INVAL, sizeof(tab.t16));
	memset(tab.t10, INVAL, sizeof(tab.t10));

	for(p = set64; *p; p++)
		tab.t64[*p] = p-set64;
	for(p = set32; *p; p++)
		tab.t32[*p] = p-set32;
	for(p = set16; *p; p++)
		tab.t16[*p] = (p-set16)%16;
	for(p = set10; *p; p++)
		tab.t10[*p] = (p-set10);

	tab.inited = 1;
}

static char*
frompow2(char *a, mpint *b, int s)
{
	char *p, *next;
	int i;
	mpdigit x;
	int sn;

	sn = 1<<s;
	for(p = a; *p; p++)
		if(tab.t16[*(uchar*)p] >= sn)
			break;

	mpbits(b, (p-a)*s);
	b->top = 0;
	next = p;

	while(p > a){
		x = 0;
		for(i = 0; i < Dbits; i += s){
			if(p <= a)
				break;
			x |= tab.t16[*(uchar*)--p]<<i;
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

	for(p = a; *p; p++)
		if(tab.t10[*(uchar*)p] >= 8)
			break;

	mpbits(b, (p-a)*3);
	b->top = 0;
	next = p;

	i = 0;
	x = y = 0;
	while(p > a){
		y = tab.t10[*(uchar*)--p];
		x |= y << i;
		i += 3;
		if(i >= Dbits){
Digout:
			i -= Dbits;
			b->p[b->top++] = x;
			x = y >> 3-i;
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
			y = tab.t10[*(uchar*)a];
			if(y == INVAL)
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
		if(i != 9)
			break;
	}
	mpfree(pow);
	mpfree(r);
	return a;
}

static char*
fromdecx(char *a, mpint *b, uchar tab[256], int (*dec)(uchar*, int, char*, int))
{
	char *buf = a;
	uchar *p;
	int n, m;

	b->top = 0;
	for(; tab[*(uchar*)a] != INVAL; a++)
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

	if(tab.inited == 0)
		init();

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
		e = fromdecx(a, b, tab.t32, dec32);
		break;
	case 64:
		e = fromdecx(a, b, tab.t64, dec64);
		break;
	default:
		abort();
		return nil;
	}

	if(pp != nil)
		*pp = e;

	// if no characters parsed, there wasn't a number to convert
	if(e == a)
		return nil;

	b->sign = sign;
	return mpnorm(b);
}
