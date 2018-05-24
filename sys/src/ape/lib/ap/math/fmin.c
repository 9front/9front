#include <math.h>

double
fmin(double a, double b)
{
	if(isNaN(a))
		return b;
	if(isNaN(b))
		return a;
	return a < b ? a : b;
}
