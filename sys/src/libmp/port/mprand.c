#include "os.h"
#include <mp.h>
#include "dat.h"

mpint*
mprand(int bits, void (*gen)(uchar*, int), mpint *b)
{
	mpdigit mask;

	if(b == nil){
		b = mpnew(bits);
		setmalloctag(b, getcallerpc(&bits));
	}else
		mpbits(b, bits);

	b->sign = 1;
	b->top = DIGITS(bits);
	(*gen)((uchar*)b->p, b->top*Dbytes);

	mask = ((mpdigit)1 << (bits%Dbits))-1;
	if(mask != 0)
		b->p[b->top-1] &= mask;

	return mpnorm(b);
}
