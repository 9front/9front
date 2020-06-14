#include <u.h>
#include <libc.h>

long
tm2sec(Tm *tm)
{
	tmnorm(tm);
	return tm->abs;
}
