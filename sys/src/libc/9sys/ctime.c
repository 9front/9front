/*
 * This routine converts time as follows.
 * The epoch is 0000 Jan 1 1970 GMT.
 * The argument time is in seconds since then.
 * The localtime(t) entry returns a pointer to an array
 * containing
 *
 *	seconds (0-59)
 *	minutes (0-59)
 *	hours (0-23)
 *	day of month (1-31)
 *	month (0-11)
 *	year-1970
 *	weekday (0-6, Sun is 0)
 *	day of the year
 *	daylight savings flag
 *
 * The routine gets the daylight savings time from the environment.
 *
 * asctime(tvec))
 * where tvec is produced by localtime
 * returns a ptr to a character string
 * that has the ascii time in the form
 *
 *	                            \\
 *	Thu Jan 01 00:00:00 GMT 1970n0
 *	012345678901234567890123456789
 *	0	  1	    2
 *
 * ctime(t) just calls localtime, then asctime.
 */

#include <u.h>
#include <libc.h>

Tm*
localtime(long tim)
{
	static Tm tm;
	Tzone *tz;

	/* No error checking: the API doesn't allow it. */
	tz = tmgetzone("local");
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

	/* No error checking: the API doesn't allow it. */
	tz = tmgetzone("local");
	tmtime(&tm, abs, tz);
	return asctime(&tm);
}
