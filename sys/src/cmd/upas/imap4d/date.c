#include "imap4d.h"

int
imap4date(Tm *tm, char *date)
{
	if(tmparse(tm, "DD-?MM-YYYY", date, nil, nil) == nil)
		return 0;
	return 1;
}

/*
 * parse imap4 dates
 */
ulong
imap4datetime(char *date)
{
	Tm tm;
	vlong s;

	s = -1;
	if(tmparse(&tm, "?DD-?MM-YYYY hh:mm:ss ?Z", date, nil, nil) != nil)
		s = tmnorm(&tm);
	else if(tmparse(&tm, "?W, ?DD-?MM-YYYY hh:mm:ss ?Z", date, nil, nil) != nil)
		s = tmnorm(&tm);
	if(s > 0 && s < (1ULL<<31))
		return s;
	return ~0;
}

/*
 * parse dates of formats
 * [Wkd[,]] DD Mon YYYY HH:MM:SS zone
 * [Wkd] Mon ( D|DD) HH:MM:SS zone YYYY
 * plus anything similar
 * return nil for a failure
 */
Tm*
date2tm(Tm *tm, char *date)
{
	char **f, *fmts[] = {
		"?W, ?DD ?MMM YYYY hh:mm:ss ?Z",
		"?W ?M ?DD hh:mm:ss ?Z YYYY",
		"?W, DD-?MM-YY hh:mm:ss ?Z",
		"?DD ?MMM YYYY hh:mm:ss ?Z",
		"?M ?DD hh:mm:ss ?Z YYYY",
		"DD-?MM-YYYY hh:mm:ss ?Z",
		nil,
	};

	for(f = fmts; *f; f++)
		if(tmparse(tm, *f, date, nil, nil) != nil)
			return tm;
	return nil;
}
