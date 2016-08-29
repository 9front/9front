#include "os.h"
#include <mp.h>

// use extended gcd to find the multiplicative inverse
// res = b**-1 mod m
void
mpinvert(mpint *b, mpint *m, mpint *res)
{
	mpint *v;

	v = mpnew(0);
	mpextendedgcd(b, m, v, res, nil);
	if(mpcmp(v, mpone) != 0)
		abort();
	mpfree(v);
	mpmod(res, m, res);
}
