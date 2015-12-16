#include "os.h"
#include <mp.h>
#include <libsec.h>
#include <ctype.h>

void
ecassign(ECdomain *, ECpoint *a, ECpoint *b)
{
	b->inf = a->inf;
	mpassign(a->x, b->x);
	mpassign(a->y, b->y);
}

void
ecadd(ECdomain *dom, ECpoint *a, ECpoint *b, ECpoint *s)
{
	mpint *l, *k, *sx, *sy;

	if(a->inf && b->inf){
		s->inf = 1;
		return;
	}
	if(a->inf){
		ecassign(dom, b, s);
		return;
	}
	if(b->inf){
		ecassign(dom, a, s);
		return;
	}
	if(mpcmp(a->x, b->x) == 0 && (mpcmp(a->y, mpzero) == 0 || mpcmp(a->y, b->y) != 0)){
		s->inf = 1;
		return;
	}
	l = mpnew(0);
	k = mpnew(0);
	sx = mpnew(0);
	sy = mpnew(0);
	if(mpcmp(a->x, b->x) == 0 && mpcmp(a->y, b->y) == 0){
		mpadd(mpone, mptwo, k);
		mpmul(a->x, a->x, l);
		mpmul(l, k, l);
		mpadd(l, dom->a, l);
		mpleft(a->y, 1, k);
		mpmod(k, dom->p, k);
		mpinvert(k, dom->p, k);
		mpmul(k, l, l);
		mpmod(l, dom->p, l);

		mpleft(a->x, 1, k);
		mpmul(l, l, sx);
		mpsub(sx, k, sx);
		mpmod(sx, dom->p, sx);

		mpsub(a->x, sx, sy);
		mpmul(l, sy, sy);
		mpsub(sy, a->y, sy);
		mpmod(sy, dom->p, sy);
		mpassign(sx, s->x);
		mpassign(sy, s->y);
		mpfree(sx);
		mpfree(sy);
		mpfree(l);
		mpfree(k);
		return;
	}
	mpsub(b->y, a->y, l);
	mpmod(l, dom->p, l);
	mpsub(b->x, a->x, k);
	mpmod(k, dom->p, k);
	mpinvert(k, dom->p, k);
	mpmul(k, l, l);
	mpmod(l, dom->p, l);
	
	mpmul(l, l, sx);
	mpsub(sx, a->x, sx);
	mpsub(sx, b->x, sx);
	mpmod(sx, dom->p, sx);
	
	mpsub(a->x, sx, sy);
	mpmul(sy, l, sy);
	mpsub(sy, a->y, sy);
	mpmod(sy, dom->p, sy);
	
	mpassign(sx, s->x);
	mpassign(sy, s->y);
	mpfree(sx);
	mpfree(sy);
	mpfree(l);
	mpfree(k);
}

void
ecmul(ECdomain *dom, ECpoint *a, mpint *k, ECpoint *s)
{
	ECpoint ns, na;
	mpint *l;

	if(a->inf || mpcmp(k, mpzero) == 0){
		s->inf = 1;
		return;
	}
	ns.inf = 1;
	ns.x = mpnew(0);
	ns.y = mpnew(0);
	na.x = mpnew(0);
	na.y = mpnew(0);
	ecassign(dom, a, &na);
	l = mpcopy(k);
	l->sign = 1;
	while(mpcmp(l, mpzero) != 0){
		if(l->p[0] & 1)
			ecadd(dom, &na, &ns, &ns);
		ecadd(dom, &na, &na, &na);
		mpright(l, 1, l);
	}
	if(k->sign < 0){
		ns.y->sign = -1;
		mpmod(ns.y, dom->p, ns.y);
	}
	ecassign(dom, &ns, s);
	mpfree(ns.x);
	mpfree(ns.y);
	mpfree(na.x);
	mpfree(na.y);
	mpfree(l);
}

int
ecverify(ECdomain *dom, ECpoint *a)
{
	mpint *p, *q;
	int r;

	if(a->inf)
		return 1;
	
	p = mpnew(0);
	q = mpnew(0);
	mpmul(a->y, a->y, p);
	mpmod(p, dom->p, p);
	mpmul(a->x, a->x, q);
	mpadd(q, dom->a, q);
	mpmul(a->x, q, q);
	mpadd(q, dom->b, q);
	mpmod(q, dom->p, q);
	r = mpcmp(p, q);
	mpfree(p);
	mpfree(q);
	return r == 0;
}

int
ecpubverify(ECdomain *dom, ECpub *a)
{
	ECpoint p;
	int r;

	if(a->inf)
		return 0;
	if(!ecverify(dom, a))
		return 0;
	p.x = mpnew(0);
	p.y = mpnew(0);
	ecmul(dom, a, dom->n, &p);
	r = p.inf;
	mpfree(p.x);
	mpfree(p.y);
	return r;
}

static void
fixnibble(uchar *a)
{
	if(*a >= 'a')
		*a -= 'a'-10;
	else if(*a >= 'A')
		*a -= 'A'-10;
	else
		*a -= '0';
}

static int
octet(char **s)
{
	uchar c, d;
	
	c = *(*s)++;
	if(!isxdigit(c))
		return -1;
	d = *(*s)++;
	if(!isxdigit(d))
		return -1;
	fixnibble(&c);
	fixnibble(&d);
	return (c << 4) | d;
}

static mpint*
halfpt(ECdomain *dom, char *s, char **rptr, mpint *out)
{
	char *buf, *r;
	int n;
	mpint *ret;
	
	n = ((mpsignif(dom->p)+7)/8)*2;
	if(strlen(s) < n)
		return 0;
	buf = malloc(n+1);
	buf[n] = 0;
	memcpy(buf, s, n);
	ret = strtomp(buf, &r, 16, out);
	*rptr = s + (r - buf);
	free(buf);
	return ret;
}

static int
mpleg(mpint *a, mpint *b)
{
	int r, k;
	mpint *m, *n, *t;
	
	r = 1;
	m = mpcopy(a);
	n = mpcopy(b);
	for(;;){
		if(mpcmp(m, n) > 0)
			mpmod(m, n, m);
		if(mpcmp(m, mpzero) == 0){
			r = 0;
			break;
		}
		if(mpcmp(m, mpone) == 0)
			break;
		k = mplowbits0(m);
		if(k > 0){
			if(k & 1)
				switch(n->p[0] & 15){
				case 3: case 5: case 11: case 13:
					r = -r;
				}
			mpright(m, k, m);
		}
		if((n->p[0] & 3) == 3 && (m->p[0] & 3) == 3)
			r = -r;
		t = m;
		m = n;
		n = t;
	}
	mpfree(m);
	mpfree(n);
	return r;
}

static int
mpsqrt(mpint *n, mpint *p, mpint *r)
{
	mpint *a, *t, *s, *xp, *xq, *yp, *yq, *zp, *zq, *N;

	if(mpleg(n, p) == -1)
		return 0;
	a = mpnew(0);
	t = mpnew(0);
	s = mpnew(0);
	N = mpnew(0);
	xp = mpnew(0);
	xq = mpnew(0);
	yp = mpnew(0);
	yq = mpnew(0);
	zp = mpnew(0);
	zq = mpnew(0);
	for(;;){
		for(;;){
			mprand(mpsignif(p), genrandom, a);
			if(mpcmp(a, mpzero) > 0 && mpcmp(a, p) < 0)
				break;
		}
		mpmul(a, a, t);
		mpsub(t, n, t);
		mpmod(t, p, t);
		if(mpleg(t, p) == -1)
			break;
	}
	mpadd(p, mpone, N);
	mpright(N, 1, N);
	mpmul(a, a, t);
	mpsub(t, n, t);
	mpassign(a, xp);
	uitomp(1, xq);
	uitomp(1, yp);
	uitomp(0, yq);
	while(mpcmp(N, mpzero) != 0){
		if(N->p[0] & 1){
			mpmul(xp, yp, zp);
			mpmul(xq, yq, zq);
			mpmul(zq, t, zq);
			mpadd(zp, zq, zp);
			mpmod(zp, p, zp);
			mpmul(xp, yq, zq);
			mpmul(xq, yp, s);
			mpadd(zq, s, zq);
			mpmod(zq, p, yq);
			mpassign(zp, yp);
		}
		mpmul(xp, xp, zp);
		mpmul(xq, xq, zq);
		mpmul(zq, t, zq);
		mpadd(zp, zq, zp);
		mpmod(zp, p, zp);
		mpmul(xp, xq, zq);
		mpadd(zq, zq, zq);
		mpmod(zq, p, xq);
		mpassign(zp, xp);
		mpright(N, 1, N);
	}
	if(mpcmp(yq, mpzero) != 0)
		abort();
	mpassign(yp, r);
	mpfree(a);
	mpfree(t);
	mpfree(s);
	mpfree(N);
	mpfree(xp);
	mpfree(xq);
	mpfree(yp);
	mpfree(yq);
	mpfree(zp);
	mpfree(zq);
	return 1;
}

ECpoint*
strtoec(ECdomain *dom, char *s, char **rptr, ECpoint *ret)
{
	int allocd, o;
	mpint *r;

	allocd = 0;
	if(ret == nil){
		allocd = 1;
		ret = mallocz(sizeof(*ret), 1);
		if(ret == nil)
			return nil;
		ret->x = mpnew(0);
		ret->y = mpnew(0);
	}
	o = 0;
	switch(octet(&s)){
	case 0:
		ret->inf = 1;
		return ret;
	case 3:
		o = 1;
	case 2:
		if(halfpt(dom, s, &s, ret->x) == nil)
			goto err;
		r = mpnew(0);
		mpmul(ret->x, ret->x, r);
		mpadd(r, dom->a, r);
		mpmul(r, ret->x, r);
		mpadd(r, dom->b, r);
		if(!mpsqrt(r, dom->p, r)){
			mpfree(r);
			goto err;
		}
		if((r->p[0] & 1) != o)
			mpsub(dom->p, r, r);
		mpassign(r, ret->y);
		mpfree(r);
		if(!ecverify(dom, ret))
			goto err;
		return ret;
	case 4:
		if(halfpt(dom, s, &s, ret->x) == nil)
			goto err;
		if(halfpt(dom, s, &s, ret->y) == nil)
			goto err;
		if(!ecverify(dom, ret))
			goto err;
		return ret;
	}
err:
	if(rptr)
		*rptr = s;
	if(allocd){
		mpfree(ret->x);
		mpfree(ret->y);
		free(ret);
	}
	return nil;
}

ECpriv*
ecgen(ECdomain *dom, ECpriv *p)
{
	if(p == nil){
		p = mallocz(sizeof(*p), 1);
		if(p == nil)
			return nil;
		p->x = mpnew(0);
		p->y = mpnew(0);
		p->d = mpnew(0);
	}
	for(;;){
		mprand(mpsignif(dom->n), genrandom, p->d);
		if(mpcmp(p->d, mpzero) > 0 && mpcmp(p->d, dom->n) < 0)
			break;
	}
	ecmul(dom, dom->G, p->d, p);
	return p;
}

void
ecdsasign(ECdomain *dom, ECpriv *priv, uchar *dig, int len, mpint *r, mpint *s)
{
	ECpriv tmp;
	mpint *E, *t;

	tmp.x = mpnew(0);
	tmp.y = mpnew(0);
	tmp.d = mpnew(0);
	E = betomp(dig, len, nil);
	t = mpnew(0);
	if(mpsignif(dom->n) < 8*len)
		mpright(E, 8*len - mpsignif(dom->n), E);
	for(;;){
		ecgen(dom, &tmp);
		mpmod(tmp.x, dom->n, r);
		if(mpcmp(r, mpzero) == 0)
			continue;
		mpmul(r, priv->d, s);
		mpadd(E, s, s);
		mpinvert(tmp.d, dom->n, t);
		mpmul(s, t, s);
		mpmod(s, dom->n, s);
		if(mpcmp(s, mpzero) != 0)
			break;
	}
	mpfree(t);
	mpfree(E);
	mpfree(tmp.x);
	mpfree(tmp.y);
	mpfree(tmp.d);
}

int
ecdsaverify(ECdomain *dom, ECpub *pub, uchar *dig, int len, mpint *r, mpint *s)
{
	mpint *E, *t, *u1, *u2;
	ECpoint R, S;
	int ret;

	if(mpcmp(r, mpone) < 0 || mpcmp(s, mpone) < 0 || mpcmp(r, dom->n) >= 0 || mpcmp(r, dom->n) >= 0)
		return 0;
	E = betomp(dig, len, nil);
	if(mpsignif(dom->n) < 8*len)
		mpright(E, 8*len - mpsignif(dom->n), E);
	t = mpnew(0);
	u1 = mpnew(0);
	u2 = mpnew(0);
	R.x = mpnew(0);
	R.y = mpnew(0);
	S.x = mpnew(0);
	S.y = mpnew(0);
	mpinvert(s, dom->n, t);
	mpmul(E, t, u1);
	mpmod(u1, dom->n, u1);
	mpmul(r, t, u2);
	mpmod(u2, dom->n, u2);
	ecmul(dom, dom->G, u1, &R);
	ecmul(dom, pub, u2, &S);
	ecadd(dom, &R, &S, &R);
	ret = 0;
	if(!R.inf){
		mpmod(R.x, dom->n, t);
		ret = mpcmp(r, t) == 0;
	}
	mpfree(t);
	mpfree(u1);
	mpfree(u2);
	mpfree(R.x);
	mpfree(R.y);
	mpfree(S.x);
	mpfree(S.y);
	return ret;
}

static char *code = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

void
base58enc(uchar *src, char *dst, int len)
{
	mpint *n, *r, *b;
	char *sdst, t;
	
	sdst = dst;
	n = betomp(src, len, nil);
	b = uitomp(58, nil);
	r = mpnew(0);
	while(mpcmp(n, mpzero) != 0){
		mpdiv(n, b, n, r);
		*dst++ = code[mptoui(r)];
	}
	for(; *src == 0; src++)
		*dst++ = code[0];
	dst--;
	while(dst > sdst){
		t = *sdst;
		*sdst++ = *dst;
		*dst-- = t;
	}
}

int
base58dec(char *src, uchar *dst, int len)
{
	mpint *n, *b, *r;
	char *t;
	
	n = mpnew(0);
	r = mpnew(0);
	b = uitomp(58, nil);
	for(; *src; src++){
		t = strchr(code, *src);
		if(t == nil){
			mpfree(n);
			mpfree(r);
			mpfree(b);
			werrstr("invalid base58 char");
			return -1;
		}
		uitomp(t - code, r);
		mpmul(n, b, n);
		mpadd(n, r, n);
	}
	mptober(n, dst, len);
	mpfree(n);
	mpfree(r);
	mpfree(b);
	return 0;
}
