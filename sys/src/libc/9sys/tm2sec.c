#include <u.h>
#include <libc.h>

long
tm2sec(Tm *tm)
{
	Tm tt;

	tt = *tm;
	return tmnorm(&tt);
}
