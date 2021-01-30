/*
	log returns the natural logarithm of its floating
	point argument.

	The coefficients are #2705 from Hart & Cheney. (19.38D)

	It calls frexp.
*/

#include <math.h>
#include <errno.h>

#define	Log2    0.693147180559945309e0
#define	Ln2o1   1.4426950408889634073599
#define	Ln10o1  .4342944819032518276511
#define	Sqrto2  0.707106781186547524e0
#define	P0      -.240139179559210510e2
#define	P1      0.309572928215376501e2
#define	P2      -.963769093377840513e1
#define	P3      0.421087371217979714e0
#define	Q0      -.120069589779605255e2
#define	Q1      0.194809660700889731e2
#define	Q2      -.891110902798312337e1

double
log(double arg)
{
	double x, z, zsq, temp;
	int exp;

	if(arg <= 0) {
		errno = (arg==0)? ERANGE : EDOM;
		return -HUGE_VAL;
	}
	x = frexp(arg, &exp);
	while(x < 0.5) {
		x *= 2;
		exp--;
	}
	if(x < Sqrto2) {
		x *= 2;
		exp--;
	}

	z = (x-1) / (x+1);
	zsq = z*z;

	temp = ((P3*zsq + P2)*zsq + P1)*zsq + P0;
	temp = temp/(((zsq + Q2)*zsq + Q1)*zsq + Q0);
	temp = temp*z + exp*Log2;
	return temp;
}

double
log10(double arg)
{
	if(arg <= 0) {
		errno = (arg==0)? ERANGE : EDOM;
		return -HUGE_VAL;
	}
	return log(arg) * Ln10o1;
}

double
log2(double arg)
{
	if(arg <= 0) {
		errno = (arg==0)? ERANGE : EDOM;
		return -HUGE_VAL;
	}
	return log(arg) * Ln2o1;
}
