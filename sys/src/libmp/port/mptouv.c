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
		b = mpnew(VLDIGITS*sizeof(mpdigit));
		setmalloctag(b, getcallerpc(&v));
	}else
		mpbits(b, VLDIGITS*sizeof(mpdigit));
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

	if(b->top == 0)
		return 0LL;

	if(b->top > VLDIGITS)
		return MAXVLONG;

	v = 0ULL;
	for(s = 0; s < b->top; s++)
		v |= (uvlong)b->p[s]<<(s*sizeof(mpdigit)*8);

	return v;
}
