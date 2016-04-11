#include "libc.h"

#undef gmtime

Tm*
_gmtime(time_t t)
{
	static Tm r;
	struct tm *p;

	p = gmtime(&t);
	r.sec = p->tm_sec;
	r.min = p->tm_min;
	r.hour = p->tm_hour;
	r.mday = p->tm_mday;
	r.mon = p->tm_mon;
	r.year = p->tm_year;
	r.wday = p->tm_wday;
	r.yday = p->tm_yday;
	strcpy(r.zone, "GMT");
	return &r;
}
