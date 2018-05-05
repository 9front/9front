/*

tests missing for:

          mpint*  strtomp(char *buf, char **rptr, int base, mpint *b)
          char*   mptoa(mpint *b, int base, char *buf, int blen)
          mpint*  betomp(uchar *buf, uint blen, mpint *b)
          int     mptobe(mpint *b, uchar *buf, uint blen, uchar **bufp)
          void    mptober(mpint *b, uchar *buf, int blen)
          mpint*  letomp(uchar *buf, uint blen, mpint *b)
          int     mptole(mpint *b, uchar *buf, uint blen, uchar **bufp)
          void    mptolel(mpint *b, uchar *buf, int blen)
          uint    mptoui(mpint*)
          mpint*  uitomp(uint, mpint*)
          int     mptoi(mpint*)
          mpint*  itomp(int, mpint*)
          mpint*  vtomp(vlong, mpint*)
          vlong   mptov(mpint*)
          mpint*  uvtomp(uvlong, mpint*)
          uvlong  mptouv(mpint*)
          mpint*  dtomp(double, mpint*)
          double  mptod(mpint*)
          void    mpexp(mpint *b, mpint *e, mpint *m, mpint *res)
          void    mpmod(mpint *b, mpint *m, mpint *remainder)
          void    mpmodadd(mpint *b1, mpint *b2, mpint *m, mpint *sum)
          void    mpmodsub(mpint *b1, mpint *b2, mpint *m, mpint *diff)
          void    mpmodmul(mpint *b1, mpint *b2, mpint *m, mpint *prod)
          void    mpsel(int s, mpint *b1, mpint *b2, mpint *res)
          void    mpextendedgcd(mpint *a, mpint *b, mpint *d, mpint *x, mpint *y)
          void    mpinvert(mpint *b, mpint *m, mpint *res)
          void    mpdigdiv(mpdigit *dividend, mpdigit divisor, mpdigit *quotient)
          void    mpvecadd(mpdigit *a, int alen, mpdigit *b, int blen, mpdigit *sum)
          void    mpvecsub(mpdigit *a, int alen, mpdigit *b, int blen, mpdigit *diff)
          void    mpvecdigmuladd(mpdigit *b, int n, mpdigit m, mpdigit *p)
          int     mpvecdigmulsub(mpdigit *b, int n, mpdigit m, mpdigit *p)
          void    mpvecmul(mpdigit *a, int alen, mpdigit *b, int blen,mpdigit *p)
          int     mpveccmp(mpdigit *a, int alen, mpdigit *b, int blen)
*/

#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>

typedef struct ldint ldint;

struct ldint {
	int n;
	u8int *b;
};

ldint _ldzero = {1, (u8int*)"\0"};
ldint _ldone = {2, (u8int*)"\1\0"};
ldint *ldzero = &_ldzero;
ldint *ldone = &_ldone;

static int
ldget(ldint *a, int n)
{
	if(n < 0) return 0;
	if(n >= a->n) return a->b[a->n - 1]&1;
	return a->b[n]&1;
}

static void
ldbits(ldint *a, int n)
{
	a->b = realloc(a->b, n);
	a->n = n;
}

static ldint *
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

static void
ldneg(ldint *a)
{
	int i, c;
	
	c = 1;
	for(i = 0; i < a->n; i++){
		c += 1 ^ a->b[i] & 1;
		a->b[i] = c & 1;
		c >>= 1;
	}
	if(c != a->b[a->n - 1]){
		ldbits(a, a->n + 1);
		a->b[a->n - 1] = c;
	}
}

static int
max(int a, int b)
{
	return a>b? a : b;
}

static ldint *
ldnew(int n)
{
	ldint *a;
	
	a = malloc(sizeof(ldint));
	if(n <= 0) n = 1;
	a->b = malloc(n);
	a->n = n;
	return a;
}

static void
ldfree(ldint *a)
{
	if(a == nil) return;
	free(a->b);
	free(a);
}

static void
ldsanity(ldint *a)
{
	int i;
	
	assert(a->n > 0);
	for(i = 0; i < a->n; i++)
		assert(a->b[i] < 2);
}

static ldint *
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

static mpint *
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

static void
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

static ldint *
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

static ldint *
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

static int
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

static int
mpdetfmt(Fmt *f)
{
	mpint *a;
	int i, j;
	
	a = va_arg(f->args, mpint *);
	fmtprint(f, "(sign=%d,top=%d,size=%d,", a->sign, a->top, a->size);
	for(i=0;i<a->top;){
		fmtprint(f, "%ullx", (uvlong)a->p[i]);
		if(++i == a->top) break;
		fmtrune(f, ',');
		for(j = i+1; j < a->top;  j++)
			if(a->p[i] != a->p[j])
				goto next;
		fmtprint(f, "...");
		break;
	next:;
	}
	fmtrune(f, '|');
	for(i=a->top;i<a->size;){
		fmtprint(f, "%ullx", (uvlong)a->p[i]);
		if(++i == a->size) break;
		fmtrune(f, ',');
		for(j = i+1; j < a->top;  j++)
			if(a->p[i] != a->p[j])
				goto next2;
		fmtprint(f, "...");
		break;
	next2:;
	}
	fmtrune(f, ')');
	return 0;
}

static int
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

static int
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

static int
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

static mpint *
mptarget(void)
{
	mpint *r;
	int i, n;
	
	r = mpnew(0);
	n = nrand(16);
	mpbits(r, n * Dbits);
	r->top = n;
	prng((void *) r->p, n * Dbytes);
	r->sign = 1 - 2 * (rand() & 1);
	return r;
}

static void
ldadd(ldint *a, ldint *b, ldint *q)
{
	int r, i, x, c;
	
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

static void
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

static void
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

static void
ldsub(ldint *a, ldint *b, ldint *q)
{
	int r, i, x, c;
	
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

static void
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

static void
lddiv(ldint *a, ldint *b, ldint *q, ldint *r)
{
	int n, i, j, c, s, k;
	
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

static void
lddivq(ldint *a, ldint *b, ldint *q)
{
	ldint *r;
	
	if(ldmpeq(b, mpzero)){
		memset(q->b, 0, q->n);
		return;
	}
	r = ldnew(0);
	lddiv(a, b, q, r);
	ldfree(r);
}

static void
mpdivq(mpint *a, mpint *b, mpint *q)
{
	if(mpcmp(b, mpzero) == 0){
		mpassign(mpzero, q);
		return;
	}
	mpdiv(a, b, q, nil);
}

static void
lddivr(ldint *a, ldint *b, ldint *r)
{
	ldint *q;
	
	if(ldmpeq(b, mpzero)){
		memset(r->b, 0, r->n);
		return;
	}
	q = ldnew(0);
	lddiv(a, b, q, r);
	ldfree(q);
}

static void
mpdivr(mpint *a, mpint *b, mpint *r)
{
	if(mpcmp(b, mpzero) == 0){
		mpassign(mpzero, r);
		return;
	}
	mpdiv(a, b, nil, r);
}

static void
ldand(ldint *a, ldint *b, ldint *q)
{
	int r, i, x, c;
	
	r = max(a->n, b->n);
	ldbits(q, r);
	for(i = 0; i < r; i++)
		q->b[i] = ldget(a, i) & ldget(b, i);
	ldnorm(q);
}

static void
ldbic(ldint *a, ldint *b, ldint *q)
{
	int r, i, x, c;
	
	r = max(a->n, b->n);
	ldbits(q, r);
	for(i = 0; i < r; i++)
		q->b[i] = ldget(a, i) & ~ldget(b, i);
	ldnorm(q);
}

static void
ldor(ldint *a, ldint *b, ldint *q)
{
	int r, i, x, c;
	
	r = max(a->n, b->n);
	ldbits(q, r);
	for(i = 0; i < r; i++)
		q->b[i] = ldget(a, i) | ldget(b, i);
	ldnorm(q);
}

static void
ldxor(ldint *a, ldint *b, ldint *q)
{
	int r, i, x, c;
	
	r = max(a->n, b->n);
	ldbits(q, r);
	for(i = 0; i < r; i++)
		q->b[i] = ldget(a, i) ^ ldget(b, i);
	ldnorm(q);
}

static void
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

static void
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

static void
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

static void
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

static void
mpnot_(mpint *a, int, mpint *b)
{
	mpnot(a, b);
}

static void
ldnot(ldint *a, int, ldint *b)
{
	int i;
	
	ldbits(b, a->n);
	for(i = 0; i < a->n; i++)
		b->b[i] = a->b[i] ^ 1;
}

enum { NTEST = 2*257 };
static void
testgen(int i, ldint *a)
{
	if(i < 257)
		itold(i-128, a);
	else
		pow2told(i-385, a);
}

typedef struct Test2 Test2;
struct Test2 {
	char *name;
	void (*dut)(mpint *, mpint *, mpint *);
	void (*ref)(ldint *, ldint *, ldint *);
};

typedef struct Test1i Test1i;
struct Test1i {
	char *name;
	enum { NONEG = 1 } flags;
	void (*dut)(mpint *, int, mpint *);
	void (*ref)(ldint *, int, ldint *);
};

int
validate(char *name, ldint *ex, mpint *res, char *str)
{
	int rv;

	rv = 1;
	if(res->top == 0 && res->sign < 0){
		fprint(2, "FAIL: %s: %s: got -0, shouldn't happen\n", name, str);
		rv =0;
	}else if(!ldmpeq(ex, res)){
		fprint(2, "FAIL: %s: %s: got %#B, expected %L\n", name, str, res, ex);
		rv = 0;
	}
	free(str);
	return rv;
}

int
test2(Test2 *t, ldint *a, ldint *b)
{
	ldint *c;
	mpint *ma, *mb, *rc;
	int rv;
	
	c = ldnew(0);
	t->ref(a, b, c);
	ldsanity(a);
	ldsanity(b);
	ldsanity(c);
	ma = ldtomp(a, nil);
	mb = ldtomp(b, nil);
	rc = mptarget();
	t->dut(ma, mb, rc);
	rv = validate(t->name, c, rc, smprint("%L and %L", a, b));
	ldtomp(a, ma);
	ldtomp(b, mb);
	t->dut(ma, mb, mb);
	rv = validate(t->name, c, mb, smprint("%L and %L (aliased to result)", a, b));
	ldtomp(a, ma);
	ldtomp(b, mb);
	t->dut(ma, mb, ma);
	rv = validate(t->name, c, ma, smprint("%L (aliased to result) and %L", a, b));
	ldfree(c);
	mpfree(rc);
	mpfree(ma);
	mpfree(mb);
	return rv;
}

int
test2x(Test2 *t, ldint *a)
{
	ldint *c;
	mpint *ma, *rc;
	int rv;
	
	c = ldnew(0);
	t->ref(a, a, c);
	ldsanity(a);
	ldsanity(c);
	ma = ldtomp(a, nil);
	rc = mptarget();
	t->dut(ma, ma, rc);
	rv = validate(t->name, c, rc, smprint("%L and %L (aliased to each other)", a, a));
	ldtomp(a, ma);
	t->dut(ma, ma, ma);
	rv = validate(t->name, c, ma, smprint("%L and %L (both aliased to result)", a, a));
	ldfree(c);
	mpfree(rc);
	mpfree(ma);
	return rv;
}

void
run2(Test2 *t)
{
	int i, j, ok;
	ldint *a, *b, *c;
	
	a = ldnew(32);
	b = ldnew(32);
	c = ldnew(32);
	ok = 1;
	for(i = 0; i < NTEST; i++){
		for(j = 0; j < NTEST; j++){
			testgen(i, a);
			testgen(j, b);
			ok &= test2(t, a, b);
		}
		itold(i, a);
		ok &= test2x(t, a);
	}
	for(i = 1; i <= 4; i++)
		for(j = 1; j <= 4; j++){
			ldrand(i * Dbits, a);
			ldrand(j * Dbits, b);
			ok &= test2(t, a, b);
		}
	ldfree(a);
	ldfree(b);
	if(ok)
		fprint(2, "%s: passed\n", t->name);
}

Test2 tests2[] = {
	"mpadd", mpadd, ldadd,
	"mpmagadd", mpmagadd, ldmagadd,
	"mpsub", mpsub, ldsub,
	"mpmagsub", mpmagsub, ldmagsub,
	"mpand", mpand, ldand,
	"mpor", mpor, ldor,
	"mpbic", mpbic, ldbic,
	"mpxor", mpxor, ldxor,
	"mpmul", mpmul, ldmul,
	"mpdiv(q)", mpdivq, lddivq,
	"mpdiv(r)", mpdivr, lddivr,
};

void
all2(void)
{
	Test2 *t;
	
	for(t = tests2; t < tests2 + nelem(tests2); t++)
		run2(t);
}

int
test1i(Test1i *t, ldint *a, int b)
{
	ldint *c;
	mpint *ma, *rc;
	int rv;
	
	c = ldnew(0);
	t->ref(a, b, c);
	ldsanity(a);
	ldsanity(c);
	ma = ldtomp(a, nil);
	rc = mptarget();
	t->dut(ma, b, rc);
	rv = validate(t->name, c, rc, smprint("%L and %d", a, b));
	ldtomp(a, ma);
	t->dut(ma, b, ma);
	rv = validate(t->name, c, ma, smprint("%L (aliased to result) and %d", a, b));
	ldfree(c);
	mpfree(rc);
	mpfree(ma);
	return rv;
}

void
run1i(Test1i *t)
{
	int i, j, ok;
	ldint *a, *c;
	
	a = ldnew(32);
	c = ldnew(32);
	ok = 1;
	for(i = 0; i < NTEST; i++)
		for(j = (t->flags & NONEG) != 0 ? 0 : -128; j <= 128; j++){
			testgen(i, a);
			ok &= test1i(t, a, j);		
		}
	ldfree(a);
	ldfree(c);
	if(ok)
		fprint(2, "%s: passed\n", t->name);
}


Test1i tests1i[] = {
	"mpleft", 0, mpleft, ldleft,
	"mpasr", 0, mpasr, ldasr,
	"mptrunc", NONEG, mptrunc, ldtrunc,
	"mpxtend", NONEG, mpxtend, ldxtend,
	"mpnot", NONEG, mpnot_, ldnot, /* hack */
};

void
all1i(void)
{
	Test1i *t;
	
	for(t = tests1i; t < tests1i + nelem(tests1i); t++)
		run1i(t);
}

void
siglo(void)
{
	int i, j, k;
	ldint *a;
	mpint *ma;
	int sigok, lowok0;
	
	a = ldnew(32);
	ma = mpnew(0);
	sigok = 1;
	lowok0 = 1;
	for(i = 0; i < NTEST; i++){
		testgen(i, a);
		for(j = 0; j < a->n; j++)
			if(a->b[j] != 0)
				break;
		if(j == a->n) j = 0;
		ldtomp(a, ma);
		k = mplowbits0(ma);
		if(k != j){
			fprint(2, "FAIL: mplowbits0: %#B: got %d, expected %d\n", ma, k, j);
			lowok0 = 0;
		}
		for(j = a->n - 2; j >= 0; j--)
			if(a->b[j] != a->b[a->n-1])
				break;
		for(k = j-1; k >= 0; k--)
			if(a->b[k] != 0)
				break;
		if(a->b[a->n - 1] && k < 0) j++;
		j++;
		ldtomp(a, ma);
		k = mpsignif(ma);
		if(k != j){
			fprint(2, "FAIL: mpsignif: %#B: got %d, expected %d\n", ma, k, j);
			sigok = 0;
		}
	}
	if(sigok) fprint(2, "mpsignif: passed\n");
	if(lowok0) fprint(2, "mplowbits0: passed\n");
	ldfree(a);
	mpfree(ma);
}

void
cmptest(void)
{
	int i, j, k, l;
	ldint *a, *b;
	mpint *ma, *mb;
	int cmpok, magcmpok;
	
	a = ldnew(32);
	b = ldnew(32);
	ma = mpnew(0);
	mb = mpnew(0);
	cmpok = 1;
	magcmpok = 1;
	for(i = 0; i < NTEST; i++)
		for(j = 0; j < NTEST; j++){
			testgen(i, a);
			testgen(j, b);
			ldtomp(a, ma);
			ldtomp(b, mb);
			l = ldcmp(a, b);
			k = mpcmp(ma, mb);
			if(k < 0) k = -1;
			if(k > 0) k = 1;
			if(k != l){
				fprint(2, "FAIL: mpcmp: %L and %L: got %d, expected %d\n", a, b, k, l);
				cmpok = 1;
			}
			ldtomp(a, ma);
			ldtomp(b, mb);
			l = ldmagcmp(a, b);
			k = mpmagcmp(ma, mb);
			if(k < 0) k = -1;
			if(k > 0) k = 1;
			if(k != l){
				fprint(2, "FAIL: mpmagcmp: %L and %L: got %d, expected %d\n", a, b, k, l);
				magcmpok = 1;
			}
		}
	ldfree(a);
	ldfree(b);
	mpfree(ma);
	mpfree(mb);
	if(cmpok) fprint(2, "mpcmp: passed\n");
	if(magcmpok) fprint(2, "mpmagcmp: passed\n");
}

void
main()
{
	fmtinstall('B', mpfmt);
	fmtinstall(L'β', mpdetfmt);
	fmtinstall('L', ldfmt);
	siglo();
	cmptest();
	all1i();
	all2();
}
