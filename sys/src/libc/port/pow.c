#include <u.h>
#include <libc.h>

static int
isodd(double v)
{
	double iv;

	if(modf(v, &iv) != 0)
		return 0;
	return (vlong)iv & 1;
}

double
pow(double x, double y) /* return x ^ y (exponentiation) */
{
	double xy, y1, ye;
	long i;
	int ex, ey, flip;

	/*
	 * Special cases.
	 * Checking early here prevents an infinite loop.
	 * We need to test if !isNaN() here because otherwise
	 * we trap.
	 */
	if(!isNaN(x) && x == 1.0)
		return 1.0;
	if(!isNaN(y) && y == 0.0)
		return 1.0;
	if(isNaN(x) || isNaN(y))
		return NaN();
	if(isInf(x, 1)){
		if(y < 0)
			return 0.0;
		else
			return Inf(1);
	}else if(isInf(x, -1)){
		if(y < 0)
			return isodd(y) ? -0.0 : 0.0;
		else if(y > 0)
			return isodd(y) ? Inf(-1) : Inf(1);
	}
	if(isInf(y, 1)){
		if(x == -1.0)
			return 1.0;
		else if(fabs(x) < 1.0)
			return 0.0;
		else if(fabs(x) > 1.0)
			return Inf(1);
	}else if(isInf(y, -1)){
		if(x == -1.0)
			return 1.0;
		else if(fabs(x) < 1.0)
			return Inf(1);
		else if(fabs(x) > 1.0)
			return 0.0;
	}

	flip = 0;
	if(y < 0.){
		y = -y;
		flip = 1;
	}
	y1 = modf(y, &ye);
	if(y1 != 0.0){
		if(x <= 0.)
			goto zreturn;
		if(y1 > 0.5) {
			y1 -= 1.;
			ye += 1.;
		}
		xy = exp(y1 * log(x));
	}else
		xy = 1.0;
	if(ye > 0x7FFFFFFF){	/* should be ~0UL but compiler can't convert double to ulong */
		if(x <= 0.){
 zreturn:
			if(x==0. && !flip)
				return 0.;
			return NaN();
		}
		if(flip){
			if(y == .5)
				return 1./sqrt(x);
			y = -y;
		}else if(y == .5)
			return sqrt(x);
		return exp(y * log(x));
	}
	x = frexp(x, &ex);
	ey = 0;
	i = ye;
	if(i)
		for(;;){
			if(i & 1){
				xy *= x;
				ey += ex;
			}
			i >>= 1;
			if(i == 0)
				break;
			x *= x;
			ex <<= 1;
			if(x < .5){
				x += x;
				ex -= 1;
			}
		}
	if(flip){
		xy = 1. / xy;
		ey = -ey;
	}
	return ldexp(xy, ey);
}
