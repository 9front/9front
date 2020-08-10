#include <u.h>
#include <libc.h>

Tm*
localtime(long tim)
{
	static Tm tm;
	Tzone *tz;

	/*
	 * We have no way to report errors,
	 * so we just ignore them here.
	 */
	tz = tzload("local");
	tmtime(&tm, tim, tz);
	return &tm;
}

Tm*
gmtime(long abs)
{
	static Tm tm;
	return tmtime(&tm, abs, nil);
}

char*
ctime(long abs)
{
	Tzone *tz;
	Tm tm;

	/*
	 * We have no way to report errors,
	 * so we just ignore them here.
	 */
	tz = tzload("local");
	tmtime(&tm, abs, tz);
	return asctime(&tm);
}
