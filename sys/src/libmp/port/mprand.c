#include "os.h"
#include <mp.h>
#include "dat.h"

mpint*
mprand(int bits, void (*gen)(uchar*, int), mpint *b)
{
	mpdigit mask;
	int n, m;
	uchar *p;

	n = DIGITS(bits);
	if(b == nil){
		b = mpnew(bits);
		setmalloctag(b, getcallerpc(&bits));
	}else
		mpbits(b, bits);

	p = malloc(n*Dbytes);
	if(p == nil)
		sysfatal("mprand: %r");
	(*gen)(p, n*Dbytes);
	betomp(p, n*Dbytes, b);
	free(p);

	// make sure we don't give too many bits
	m = bits%Dbits;
	if(m == 0)
		return b;

	mask = 1;
	mask <<= m;
	mask--;
	b->p[n-1] &= mask;
	return mpnorm(b);
}
