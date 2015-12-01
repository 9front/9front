#include "os.h"
#include <mp.h>
#include "dat.h"

void
mpmod(mpint *x, mpint *n, mpint *r)
{
	static int busy;
	static mpint *p, *m, *c, *v;
	mpdigit q[32], t[64], d;
	int sign, k, s, qn, tn;

	sign = x->sign;

	assert(n->flags & MPnorm);
	if(n->top <= 2 || n->top > nelem(q) || (x->top-n->top) > nelem(q))
		goto hard;

	/*
	 * check if n = 2**k - c where c has few power of two factors
	 * above the lowest digit.
	 */
	for(k = n->top-1; k > 0; k--){
		d = n->p[k] >> 1;
		if((d+1 & d) != 0)
			goto hard;
	}

	d = n->p[n->top-1];
	for(s = 0; (d & (mpdigit)1<<Dbits-1) == 0; s++)
		d <<= 1;

	/* lo(x) = x[0:k-1], hi(x) = x[k:xn-1] */
	k = n->top;

	while(_tas(&busy))
		;

	if(p == nil || mpmagcmp(n, p) != 0){
		if(m == nil){
			m = mpnew(0);
			c = mpnew(0);
			p = mpnew(0);
		}
		mpleft(p, s, m);
		mpleft(mpone, k*Dbits, c);
		mpsub(c, m, c);
		if(c->top >= k){
			mpassign(mpzero, p);
			busy = 0;
			goto hard;
		}
		mpassign(n, p);
	}

	mpleft(x, s, r);
	if(r->top <= k){
		mpbits(r, (k+1)*Dbits);
		r->top = k+1;
	}

	/* q = hi(r) */
	qn = r->top - k;
	memmove(q, r->p+k, qn*Dbytes);

	/* r = lo(r) */
	r->top = k;

	do {
		/* t = q*c */
		tn = qn + c->top;
		memset(t, 0, tn*Dbytes);
		mpvecmul(q, qn, c->p, c->top, t);

		/* q = hi(t) */
		qn = tn - k;
		if(qn <= 0) qn = 0;
		else memmove(q, t+k, qn*Dbytes);

		/* r += lo(t) */
		if(tn > k)
			tn = k;
		mpvecadd(r->p, k, t, tn, r->p);

		/* if(r >= m) r -= m */
		mpvecsub(r->p, k+1, m->p, k, t), d = t[k];
		for(tn = 0; tn < k; tn++)
			r->p[tn] = (r->p[tn] & d) | (t[tn] & ~d);
	} while(qn > 0);

	busy = 0;

	if(s != 0)
		mpright(r, s, r);
	else
		mpnorm(r);
	goto done;

hard:
	mpdiv(x, n, nil, r);

done:
	if(sign < 0)
		mpmagsub(n, r, r);
}
