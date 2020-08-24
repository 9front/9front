#include <u.h>
#include <libc.h>

long
tm2sec(Tm *tm)
{
	Tm tt;

	tt = *tm;
	/*
	 * The zone offset should be calculated,
	 * but old code may not init tz member.
	 * nil it out so we don't access junk.
	 * while we're at it, old code probably
	 * leaves junk in nsec.
	 */
	tt.nsec = 0;
	tt.tz = nil;
	return tmnorm(&tt);
}
