#include "os.h"
#include <mp.h>
#include "dat.h"

#define VLDIGITS (sizeof(vlong)/sizeof(mpdigit))

/*
 *  this code assumes that a vlong is an integral number of
 *  mpdigits long.
 */
mpint*
vtomp(vlong v, mpint *b)
{
	int s;
	uvlong uv;

	if(b == nil){
		b = mpnew(VLDIGITS*Dbits);
		setmalloctag(b, getcallerpc(&v));
	}else
		mpbits(b, VLDIGITS*Dbits);
	b->sign = (v >> (sizeof(v)*8 - 1)) | 1;
	uv = v * b->sign;
	for(s = 0; s < VLDIGITS; s++){
		b->p[s] = uv;
		uv >>= sizeof(mpdigit)*8;
	}
	b->top = s;
	return mpnorm(b);
}

vlong
mptov(mpint *b)
{
	uvlong v;
	int s;

	if(b->top == 0)
		return 0LL;

	if(b->top > VLDIGITS){
		if(b->sign > 0)
			return (vlong)MAXVLONG;
		else
			return (vlong)MINVLONG;
	}

	v = 0ULL;
	for(s = 0; s < b->top; s++)
		v |= (uvlong)b->p[s]<<(s*sizeof(mpdigit)*8);

	if(b->sign > 0){
		if(v > MAXVLONG)
			v = MAXVLONG;
	} else {
		if(v > MINVLONG)
			v = MINVLONG;
		else
			v = -(vlong)v;
	}

	return (vlong)v;
}
