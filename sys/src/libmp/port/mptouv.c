#include "os.h"
#include <mp.h>
#include "dat.h"

#define VLDIGITS (sizeof(vlong)/sizeof(mpdigit))

/*
 *  this code assumes that a vlong is an integral number of
 *  mpdigits long.
 */
mpint*
uvtomp(uvlong v, mpint *b)
{
	int s;

	if(b == nil){
		b = mpnew(VLDIGITS*Dbits);
		setmalloctag(b, getcallerpc(&v));
	}else
		mpbits(b, VLDIGITS*Dbits);
	b->sign = 1;
	for(s = 0; s < VLDIGITS; s++){
		b->p[s] = v;
		v >>= sizeof(mpdigit)*8;
	}
	b->top = s;
	return mpnorm(b);
}

uvlong
mptouv(mpint *b)
{
	uvlong v;
	int s;

	if(b->top == 0 || b->sign < 0)
		return 0LL;

	if(b->top > VLDIGITS)
		return -1LL;

	v = 0ULL;
	for(s = 0; s < b->top; s++)
		v |= (uvlong)b->p[s]<<(s*sizeof(mpdigit)*8);

	return v;
}
