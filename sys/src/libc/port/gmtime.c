#include<u.h>
#include <libc.h>

Tm*
gmtime(long abs)
{
	static Tm tm;
	return tmtime(&tm, abs, nil);
}

Tm*
localtime(long abs)
{
	Tzone *tz;
	static Tm tm;

	/* No error checking: the API doesn't allow it. */
	tz = tmgetzone("local");
	return tmtime(&tm, abs, tz);
}

long
tm2sec(Tm *tm)
{
	tmnorm(tm);
	return tm->abs;
}
