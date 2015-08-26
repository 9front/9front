#include "os.h"
#include <mp.h>
#include <libsec.h>
#include "dat.h"

/* return uniform random [0..n-1] */
mpint*
mpnrand(mpint *n, void (*gen)(uchar*, int), mpint *b)
{
	mpint *m;
	int bits;

	/* m = 2^bits - 1 */
	bits = mpsignif(n);
	m = mpnew(bits+1);
	mpleft(mpone, bits, m);
	mpsub(m, mpone, m);

	if(b == nil)
		b = mpnew(bits);

	/* m = m - (m % n) */
	mpmod(m, n, b);
	mpsub(m, b, m);

	do {
		mprand(bits, gen, b);
	} while(mpcmp(b, m) >= 0);

	mpmod(b, n, b);
	mpfree(m);

	return b;
}
