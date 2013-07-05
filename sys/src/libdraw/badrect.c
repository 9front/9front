#include <u.h>
#include <libc.h>
#include <draw.h>

/*
 * check for zero, negative size or insanely huge rectangle.
 */
int
badrect(Rectangle r)
{
	int x, y;
	uint z;

	x = Dx(r);
	y = Dy(r);
	if(x > 0 && y > 0){
		z = x*y;
		if(z/x == y && z < 0x10000000)
			return 0;
	}
	return 1;
}
