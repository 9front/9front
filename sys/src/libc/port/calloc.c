#include <u.h>
#include <libc.h>

void*
calloc(ulong n, ulong s)
{
	void *v;
	if(n > 1 && ((ulong)-1)/n < s)
		return nil;
	if(v = mallocz(n*s, 1))
		setmalloctag(v, getcallerpc(&n));
	return v;
}
