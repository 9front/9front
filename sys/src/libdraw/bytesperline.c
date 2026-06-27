#include <u.h>
#include <libc.h>
#include <draw.h>

static
int
unitsperline(Rectangle r, int d, int bitsperunit)
{
	if(d <= 0 || d > 32)	/* being called wrong.  d is image depth. */
		abort();
	return (r.max.x*d - (r.min.x*d & -bitsperunit) + bitsperunit - 1) / bitsperunit;
}

int
wordsperline(Rectangle r, int d)
{
	return unitsperline(r, d, 8*sizeof(ulong));
}

int
bytesperline(Rectangle r, int d)
{
	return unitsperline(r, d, 8);
}
