#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>
#include "dat.h"
#include "fns.h"

ldint _ldzero = {1, (u8int*)"\0"};
ldint _ldone = {2, (u8int*)"\1\0"};
ldint *ldzero = &_ldzero;
ldint *ldone = &_ldone;

int
ldget(ldint *a, int n)
{
	if(n < 0) return 0;
	if(n >= a->n) return a->b[a->n - 1]&1;
	return a->b[n]&1;
}

void
ldbits(ldint *a, int n)
{
	a->b = realloc(a->b, n);
	a->n = n;
}

ldint *
ldnorm(ldint *a)
{
	int i;

	if(a->n > 0){
		for(i = a->n - 2; i >= 0; i--)
			if(a->b[i] != a->b[a->n-1])
				break;
		ldbits(a, i + 2);
	}else{
		ldbits(a, 1);
		a->b[0] = 0;
	}
	return a;
}

void
ldneg(ldint *a)
{
	int i, c, s, z;
	
	c = 1;
	s = a->b[a->n - 1];
	z = 1;
	for(i = 0; i < a->n; i++){
		if(a->b[i]) z = 0;
		c += 1 ^ a->b[i] & 1;
		a->b[i] = c & 1;
		c >>= 1;
	}
	if(!z && s == a->b[a->n - 1]){
		ldbits(a, a->n + 1);
		a->b[a->n - 1] = !s;
	}
}

int
max(int a, int b)
{
	return a>b? a : b;
}

ldint *
ldnew(int n)
{
	ldint *a;
	
	a = malloc(sizeof(ldint));
	if(n <= 0) n = 1;
	a->b = malloc(n);
	a->n = n;
	return a;
}

void
ldfree(ldint *a)
{
	if(a == nil) return;
	free(a->b);
	free(a);
}

void
ldsanity(ldint *a)
{
	int i;
	
	assert(a->n > 0);
	for(i = 0; i < a->n; i++)
		assert(a->b[i] < 2);
}

ldint *
ldrand(int n, ldint *a)
{
	int i;
	
	if(a == nil)
		a = ldnew(n);
	else
		ldbits(a, n);
	for(i = 0; i < n; i++)
		a->b[i] = rand() & 1;
	return a;
}

mpint *
ldtomp(ldint *a, mpint *b)
{
	int s, c, i;

	if(b == nil)
		b = mpnew(0);
	mpbits(b, a->n);
	s = a->b[a->n - 1] & 1;
	b->sign = 1 - 2 * s;
	c = s;
	memset(b->p, 0, (a->n + Dbits - 1) / Dbits * Dbytes);
	for(i = 0; i < a->n; i++){
		c += s ^ a->b[i] & 1;
		b->p[i / Dbits] |= (mpdigit)(c & 1) << (i & Dbits - 1);
		c >>= 1;
	}
	b->top = (a->n + Dbits - 1) / Dbits;
	mpnorm(b);
	return b;
}

void
mptold(mpint *b, ldint *a)
{
	int i, j, n, c;

	n = mpsignif(b) + 1;
	ldbits(a, n);
	memset(a->b, 0, n);
	for(i = 0; i <= b->top; i++)
		for(j = 0; j < Dbits; j++)
			if(Dbits * i + j < n)
				a->b[Dbits * i + j] = b->p[i] >> j & 1;
	if(b->sign < 0){
		c = 1;
		for(i = 0; i < a->n; i++){
			c += 1 ^ a->b[i] & 1;
			a->b[i] = c & 1;
			c >>= 1;
		}
	}	
}

ldint *
itold(int n, ldint *a)
{
	int i;

	if(a == nil)
		a = ldnew(sizeof(n)*8);
	else
		ldbits(a, sizeof(n)*8);
	for(i = 0; i < sizeof(n)*8; i++)
		a->b[i] = n >> i & 1;
	ldnorm(a);
	return a;
}

ldint *
pow2told(int n, ldint *a)
{
	int k;
	
	k = abs(n);
	if(a == nil)
		a = ldnew(k+2);
	else
		ldbits(a, k+2);
	memset(a->b, 0, k+2);
	a->b[k] = 1;
	if(n < 0) ldneg(a);
	ldnorm(a);
	return a;
}

int
ldfmt(Fmt *f)
{
	ldint *a;
	char *b, *p;
	int i, d, s, c;
	
	a = va_arg(f->args, ldint *);
	d = (a->n + 3) / 4;
	b = calloc(1, d + 1);
	c = s = a->b[a->n - 1];
	for(i = 0; i < a->n; i++){
		c += s^ldget(a, i);
		b[d - 1 - (i >> 2)] |= (c & 1) << (i & 3);
		c >>= 1;
	}
	for(i = 0; i < d; i++)
		b[i] = "0123456789ABCDEF"[b[i]];
	p = b;
	while(*p == '0' && p[1] != 0) p++;
	if(a->b[a->n - 1]) fmtrune(f, '-');
	fmtprint(f, "0x%s", p);
	free(b);
	return 0;
}

int
ldcmp(ldint *a, ldint *b)
{
	int x, y;
	int i, r;
	
	r = max(a->n, b->n);
	if(a->b[a->n-1] != b->b[b->n-1])
		return b->b[b->n - 1] - a->b[a->n - 1];
	for(i = r - 1; --i >= 0; ){
		x = ldget(a, i);
		y = ldget(b, i);
		if(x != y)
			return x - y;
	}
	return 0;
}

int
ldmagcmp(ldint *a, ldint *b)
{
	int s1, s2, r;
	
	s1 = a->b[a->n - 1];
	s2 = b->b[b->n - 1];
	if(s1) ldneg(a);
	if(s2) ldneg(b);
	r = ldcmp(a, b);
	if(s1) ldneg(a);
	if(s2) ldneg(b);
	return r;
}

int
ldmpeq(ldint *a, mpint *b)
{
	int i, c;

	if(b->sign > 0){
		for(i = 0; i < b->top * Dbits; i++)
			if(ldget(a, i) != (b->p[i / Dbits] >> (i & Dbits - 1) & 1))
				return 0;
		for(; i < a->n; i++)
			if(a->b[i] != 0)
				return 0;
		return 1;
	}else{
		c = 1;
		for(i = 0; i < b->top * Dbits; i++){
			c += !ldget(a, i);
			if((c & 1) != (b->p[i / Dbits] >> (i & Dbits - 1) & 1))
				return 0;
			c >>= 1;
		}
		for(; i < a->n; i++)
			if(a->b[i] != 1)
				return 0;
		return 1;
	}
}

void
mptarget(mpint *r)
{
	int n;

	n = nrand(16);
	mpbits(r, n * Dbits);
	r->top = n;
	prng((void *) r->p, n * Dbytes);
	r->sign = 1 - 2 * (rand() & 1);
}

void
ldadd(ldint *a, ldint *b, ldint *q)
{
	int r, i, c;
	
	r = max(a->n, b->n) + 1;
	ldbits(q, r);
	c = 0;
	for(i = 0; i < r; i++){
		c += ldget(a, i) + ldget(b, i);
		q->b[i] = c & 1;
		c >>= 1;
	}
	ldnorm(q);
}

void
ldmagadd(ldint *a, ldint *b, ldint *q)
{
	int i, r, s1, s2, c1, c2, co;
	
	r = max(a->n, b->n) + 2;
	ldbits(q, r);
	co = 0;
	s1 = c1 = a->b[a->n - 1] & 1;
	s2 = c2 = b->b[b->n - 1] & 1;
	for(i = 0; i < r; i++){
		c1 += s1 ^ ldget(a, i) & 1;
		c2 += s2 ^ ldget(b, i) & 1;
		co += (c1 & 1) + (c2 & 1);
		q->b[i] = co & 1;
		co >>= 1;
		c1 >>= 1;
		c2 >>= 1;
	}
	ldnorm(q);
}

void
ldmagsub(ldint *a, ldint *b, ldint *q)
{
	int i, r, s1, s2, c1, c2, co;
	
	r = max(a->n, b->n) + 2;
	ldbits(q, r);
	co = 0;
	s1 = c1 = a->b[a->n - 1] & 1;
	s2 = c2 = 1 ^ b->b[b->n - 1] & 1;
	for(i = 0; i < r; i++){
		c1 += s1 ^ ldget(a, i) & 1;
		c2 += s2 ^ ldget(b, i) & 1;
		co += (c1 & 1) + (c2 & 1);
		q->b[i] = co & 1;
		co >>= 1;
		c1 >>= 1;
		c2 >>= 1;
	}
	ldnorm(q);
}

void
ldsub(ldint *a, ldint *b, ldint *q)
{
	int r, i, c;
	
	r = max(a->n, b->n) + 1;
	ldbits(q, r);
	c = 1;
	for(i = 0; i < r; i++){
		c += ldget(a, i) + (1^ldget(b, i));
		q->b[i] = c & 1;
		c >>= 1;
	}
	ldnorm(q);
}

void
ldmul(ldint *a, ldint *b, ldint *q)
{
	int c1, c2, co, s1, s2, so, i, j;
	
	c1 = s1 = a->b[a->n - 1] & 1;
	s2 = b->b[b->n - 1] & 1;
	so = s1 ^ s2;
	ldbits(q, a->n + b->n + 1);
	memset(q->b, 0, a->n + b->n + 1);
	for(i = 0; i < a->n; i++){
		c1 += s1 ^ a->b[i] & 1;
		if((c1 & 1) != 0){
			c2 = s2;
			for(j = 0; j < b->n; j++){
				c2 += (s2 ^ b->b[j] & 1) + q->b[i + j];
				q->b[i + j] = c2 & 1;
				c2 >>= 1;
			}
			for(; c2 > 0; j++){
				assert(i + j < q->n);
				q->b[i + j] = c2 & 1;
				c2 >>= 1;
			}
		}
		c1 >>= 1;
	}
	co = so;
	for(i = 0; i < q->n; i++){
		co += so ^ q->b[i];
		q->b[i] = co & 1;
		co >>= 1;
	}
}

void
lddiv(ldint *a, ldint *b, ldint *q, ldint *r)
{
	int n, i, j, c, s;
	
	n = max(a->n, b->n) + 1;
	ldbits(q, n);
	ldbits(r, n);
	memset(r->b, 0, n);
	c = s = a->b[a->n-1];
	for(i = 0; i < n; i++){
		c += s ^ ldget(a, i);
		q->b[i] = c & 1;
		c >>= 1;
	}
	for(i = 0; i < n; i++){
		for(j = n-1; --j >= 0; )
			r->b[j + 1] = r->b[j];
		r->b[0] = q->b[n - 1];
		for(j = n-1; --j >= 0; )
			q->b[j + 1] = q->b[j];
		q->b[0] = !r->b[n - 1];
		c = s = r->b[n - 1] == b->b[b->n - 1];
		for(j = 0; j < n; j++){
			c += r->b[j] + (s ^ ldget(b, j));
			r->b[j] = c & 1;
			c >>= 1;
		}
	}
	for(j = n-1; --j >= 0; )
		q->b[j + 1] = q->b[j];
	q->b[0] = 1;
	if(r->b[r->n - 1]){
		c = 0;
		for(j = 0; j < n; j++){
			c += 1 + q->b[j];
			q->b[j] = c & 1;
			c >>= 1;
		}
		c = s = b->b[b->n - 1];
		for(j = 0; j < n; j++){
			c += r->b[j] + (s ^ ldget(b, j));
			r->b[j] = c & 1;
			c >>= 1;
		}
	}
	c = s = a->b[a->n-1] ^ b->b[b->n-1];
	for(j = 0; j < n; j++){
		c += s ^ q->b[j];
		q->b[j] = c & 1;
		c >>= 1;
	}
	c = s = a->b[a->n-1];
	for(j = 0; j < n; j++){
		c += s ^ r->b[j];
		r->b[j] = c & 1;
		c >>= 1;
	}
	ldnorm(q);
	ldnorm(r);
}

void
lddiv_(ldint *a, ldint *b, ldint *q, ldint *r)
{
	if(ldmpeq(b, mpzero)){
		memset(q->b, 0, q->n);
		memset(r->b, 0, r->n);
		return;
	}
	lddiv(a, b, q, r);
}

void
mpdiv_(mpint *a, mpint *b, mpint *q, mpint *r)
{
	if(mpcmp(b, mpzero) == 0){
		mpassign(mpzero, q);
		mpassign(mpzero, r);
		return;
	}
	mpdiv(a, b, q, r);
}

void
ldand(ldint *a, ldint *b, ldint *q)
{
	int r, i;
	
	r = max(a->n, b->n);
	ldbits(q, r);
	for(i = 0; i < r; i++)
		q->b[i] = ldget(a, i) & ldget(b, i);
	ldnorm(q);
}

void
ldbic(ldint *a, ldint *b, ldint *q)
{
	int r, i;
	
	r = max(a->n, b->n);
	ldbits(q, r);
	for(i = 0; i < r; i++)
		q->b[i] = ldget(a, i) & ~ldget(b, i);
	ldnorm(q);
}

void
ldor(ldint *a, ldint *b, ldint *q)
{
	int r, i;
	
	r = max(a->n, b->n);
	ldbits(q, r);
	for(i = 0; i < r; i++)
		q->b[i] = ldget(a, i) | ldget(b, i);
	ldnorm(q);
}

void
ldxor(ldint *a, ldint *b, ldint *q)
{
	int r, i;
	
	r = max(a->n, b->n);
	ldbits(q, r);
	for(i = 0; i < r; i++)
		q->b[i] = ldget(a, i) ^ ldget(b, i);
	ldnorm(q);
}

void
ldleft(ldint *a, int n, ldint *b)
{
	int i, c;

	if(n < 0){
		if(a->n <= -n){
			b->n = 0;
			ldnorm(b);
			return;
		}
		c = 0;
		if(a->b[a->n - 1])
			for(i = 0; i < -n; i++)
				if(a->b[i]){
					c = 1;
					break;
				}
		ldbits(b, a->n + n);
		for(i = 0; i < a->n + n; i++){
			c += a->b[i - n] & 1;
			b->b[i] = c & 1;
			c >>= 1;
		}
	}else{
		ldbits(b, a->n + n);
		memmove(b->b + n, a->b, a->n);
		memset(b->b, 0, n);
	}
	ldnorm(b);
}

void
ldright(ldint *a, int n, ldint *b)
{
	ldleft(a, -n, b);
}

void
ldasr(ldint *a, int n, ldint *b)
{
	if(n < 0){
		ldleft(a, -n, b);
		return;
	}
	if(a->n <= n){
		ldbits(b, 1);
		b->b[0] = a->b[a->n - 1];
		return;
	}
	ldbits(b, a->n - n);
	memmove(b->b, a->b + n, a->n - n);
	ldnorm(b);
}

void
ldtrunc(ldint *a, int n, ldint *b)
{
	ldbits(b, n+1);
	b->b[n] = 0;
	if(a->n >= n)
		memmove(b->b, a->b, n);
	else{
		memmove(b->b, a->b, a->n);
		memset(b->b + a->n, a->b[a->n - 1], n - a->n);
	}
	ldnorm(b);
}

void
ldxtend(ldint *a, int n, ldint *b)
{
	ldbits(b, n);
	if(a->n >= n)
		memmove(b->b, a->b, n);
	else{
		memmove(b->b, a->b, a->n);
		memset(b->b + a->n, a->b[a->n - 1], n - a->n);
	}
	ldnorm(b);
}

void
ldnot(ldint *a, ldint *b)
{
	int i;
	
	ldbits(b, a->n);
	for(i = 0; i < a->n; i++)
		b->b[i] = a->b[i] ^ 1;
}

u32int xorshift(u32int *state)
{
	u32int x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

void
testgen(int i, ldint *a)
{
	u32int state;
	u32int r;
	int j;

	if(i < 257)
		itold(i-128, a);
	else if(i < 514)
		pow2told(i-385, a);
	else{
		state = i;
		xorshift(&state);
		xorshift(&state);
		xorshift(&state);
		ldbits(a, Dbits * (1 + (xorshift(&state) & 15)));
		SET(r);
		for(j = 0; j < a->n; j++){
			if((j & 31) == 0)
				r = xorshift(&state);
			a->b[j] = r & 1;
			r >>= 1;
		}
	}
}
