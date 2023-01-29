#include <u.h>
#include <libc.h>
#include <geometry.h>

double
flerp(double a, double b, double t)
{
	return a + (b - a)*t;
}

double
fclamp(double n, double min, double max)
{
	return n < min? min: n > max? max: n;
}
