#include <u.h>
#include <libc.h>
#include <draw.h>

/*
 * XOVFL protects the unitsperline() calculation, and its
 * limit is based on:
 * 	-2³¹ ≤	r.max.x*32 + r.min.x*32	< 2³¹
 * 	-2²⁶ ≤	r.max.x + r.min.x	< 2²⁶
 * 	-2²⁵ ≤	r.min.x, r.max.x	< 2²⁵
 * for the worst case scenario where the depth is 32 and one of
 * the values is negative.
 *
 * YOVFL is meant to protect against overflow when
 * calculating the Memimage.zero and byteaddr(2) offsets.  Its
 * limit is derived from:
 * 	-2³¹ ≤	p.y*4*wpl + p.x*depth/8	< 2³¹
 * 	-2³⁰ ≤	p.y*4*wpl		< 2³⁰
 *	-2²⁸ ≤	p.y*wpl			< 2²⁸
 * for the worst case scenario where both coordinates have the
 * same sign and wpl = Dx(r).
 */
#define XOVFL(p)	((p).x < -(1<<25) || (p).x >= (1<<25))
#define YOVFL(y)	((y) < -(1<<28) || (y) >= (1<<28))

/*
 * check for zero, negative size or insanely huge rectangle.
 */
int
badrect(Rectangle r)
{
	int x, y, zy0, zy1;
	uint z;

	x = Dx(r);
	y = Dy(r);
	if(x > 0 && y > 0){
		if(XOVFL(r.min) || XOVFL(r.max))
			return 1;
		z = x*y;
		zy0 = x*r.min.y;
		zy1 = x*r.max.y;
		if(z/x == y
		&& zy0/x == r.min.y && !YOVFL(zy0)
		&& zy1/x == r.max.y && !YOVFL(zy1))
			return 0;
	}
	return 1;
}
