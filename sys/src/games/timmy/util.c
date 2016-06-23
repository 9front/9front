#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include "dat.h"
#include "fns.h"

Vec
vecadd(Vec a, Vec b)
{
	return (Vec){a.x + b.x, a.y + b.y};
}

Vec
vecsub(Vec a, Vec b)
{
	return (Vec){a.x - b.x, a.y - b.y};
}

Vec
vecmul(Vec v, double s)
{
	return (Vec){v.x * s, v.y * s};
}

Vec
vecnorm(Vec v)
{
	double r;
	
	r = hypot(v.x, v.y);
	if(r == 0) return (Vec){0, 0};
	v.x /= r;
	v.y /= r;
	return v;
}

double
vecdist(Vec a, Vec b)
{
	return hypot(a.x - b.x, a.y - b.y);
}

double
vecdot(Vec a, Vec b)
{
	return a.x * b.x + a.y * b.y;
}

Vec
vecnormal(Vec n)
{
	Vec m;
	
	m.x = -n.y;
	m.y = n.x;
	return vecnorm(m);
}
