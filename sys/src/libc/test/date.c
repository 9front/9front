#include <u.h>
#include <libc.h>

int failed;

/*
 * For debugging
 */
void
printtm(Tm *tm)
{
	fprint(2, "sec=%d min=%d hour=%d mday=%d mon=%d"
		" year=%d day=%d yday=%d zone=%s tzoff=%d\n",
		tm->sec, /* seconds (range 0..59) */
		tm->min, /* minutes (0..59) */
		tm->hour,     /* hours (0..23) */
		tm->mday,     /* day of the month (1..31) */
		tm->mon, /* month of the year (0..11) */
		tm->year,     /* year A.D. - 1900 */
		tm->wday,     /* day of week (0..6, Sunday = 0) */
		tm->yday,     /* day of year (0..365) */
		tm->zone,   /* time zone name */
		tm->tzoff);    /* time   zone delta from GMT */
}

void
fail(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprint(2, "failed: ");
	vfprint(2, fmt, ap);
	va_end(ap);
	failed++;
}

void
testtm(char *s, int year, int mon, int mday, int hour, int min, int sec, int nsec, Tm *tm){
	if(tm->year != year-1900) fail("%s wrong year expected=%d actual=%d\n", s, year, tm->year);
	if(tm->mon != mon)	fail("%s wrong month expected=%d actual=%d\n", s, mon, tm->mon);
	if(tm->mday != mday)	fail("%s wrong mday expected=%d actual=%d\n", s, mday, tm->mday);
	if(tm->hour != hour)	fail("%s wrong hour expected=%d actual=%d\n", s, hour, tm->hour);
	if(tm->min != min)	fail("%s wrong min expected=%d actual=%d\n", s, min, tm->min);
	if(tm->sec != sec)	fail("%s wrong sec expected=%d actual=%d\n", s, sec, tm->sec);
	if(tm->nsec != nsec)	fail("%s wrong nsec expected=%d actual=%d\n", s, nsec, tm->nsec);
}

void
rangechk(vlong sec, char *s, vlong val, vlong lo, vlong hi)
{
	if(val < lo || val > hi){
		fprint(2, "%lld: %s: expected %lld <= %lld <= %lld", sec, s, lo, val, hi);
		failed++;
	}
}

void
main(int, char **)
{
	Tm tm, tt;
	Tzone *gmt, *us_arizona, *us_eastern, *us_central;
	Tm here, there;
	Tzone *zl, *zp;
	char buf[128], buf1[128];
	int i, h;
	long s;

	tmfmtinstall();
	if((gmt = tzload("GMT")) == nil)
		sysfatal("nil gmt: %r\n");
	if((us_arizona = tzload("US_Arizona")) == nil)
		sysfatal("nil us_arizona: %r\n");
	if((us_eastern = tzload("US_Eastern")) == nil)
		sysfatal("nil us_eastern: %r\n");
	if((us_central = tzload("US_Central")) == nil)
		sysfatal("get zone: %r\n");

	if((zl = tzload("local")) == nil)
	     sysfatal("load zone: %r\n");
	if((zp = tzload("US_Pacific")) == nil)
	     sysfatal("load zone: %r\n");
	if(tmnow(&here, zl) == nil)
	     sysfatal("get time: %r\n");
	if(tmtime(&there, tmnorm(&here), zp) == nil)
	     sysfatal("shift time: %r\n");

	for(i = 0; i < 1600826575; i += 3613){
		tmtime(&tm, i, nil);
		rangechk(i, "nsec", tm.nsec, 0, 1e9);
		rangechk(i, "sec", tm.sec, 0, 59);
		rangechk(i, "min", tm.min, 0, 59);
		rangechk(i, "hour", tm.hour, 0, 23);
		rangechk(i, "mday", tm.mday, 1, 31);
		rangechk(i, "mon", tm.mon, 0, 11);
		rangechk(i, "year", tm.year, 69 ,120);
		rangechk(i, "wday", tm.wday, 0, 6);
		rangechk(i, "yday", tm.yday, 0, 365);
	}

	tmtime(&tm, 1586574870, gmt);
	testtm("tmtime-gmt", 2020, 3, 11, 3, 14, 30, 0, &tm);
	tmtime(&tm, 1586574870, us_arizona);
	testtm("tmtime-az", 2020, 3, 10, 20, 14, 30, 0, &tm);

	tmtime(&tm, 0, gmt);
	testtm("tmtime-0-gmt", 1970, 0, 1, 0, 0, 0, 0, &tm);
	tmnorm(&tm);
	testtm("tmnorm-0-gmt", 1970, 0, 1, 0, 0, 0, 0, &tm);

	tmtime(&tm, 84061, gmt);
	testtm("tmtime-near0-gmt", 1970, 0, 1, 23, 21, 1, 0, &tm);
	tmnorm(&tm);
	testtm("tmnorm-near0-gmt", 1970, 0, 1, 23, 21, 1, 0, &tm);

	tmtime(&tm, 1586574870, us_arizona);
	testtm("tmtime-recent-az", 2020, 3, 10, 20, 14, 30, 0, &tm);
	tmnorm(&tm);
	testtm("tmnorm-recent-az", 2020, 3, 10, 20, 14, 30, 0, &tm);

	tmtime(&tm, 1586574870, us_eastern);
	testtm("tmtime-recent-est", 2020, 3, 10, 23, 14, 30, 0, &tm);
	tmnorm(&tm);
	testtm("tmnorm-recent-est", 2020, 3, 10, 23, 14, 30, 0, &tm);

	if(tmparse(&tm, "hhmm", "1600", gmt, nil) == nil)
		sysfatal("failed parse: %r\n");
	testtm("hhmm", 1970, 0, 1, 16, 0, 0, 0, &tm);

	if(tmparse(&tm, "YYYY-MM-DD hh:mm:ss Z", "1969-12-31 16:00:00 -0800", nil, nil) == nil)
		fail("parse failed: %r\n");
	if(tmnorm(&tm) != 0)
		fail("wrong result: %lld != 0\n", tmnorm(&tm));

	if(tmparse(&tm, "YYYY MM DD", "1990,01,03", nil, nil) == nil)
		fail("comma parse failed");
	if(tmnorm(&tm) != 631324800)
		fail("wrong result");
	if(tmparse(&tm, "YYYY MM DD", "1990 ,\t01,03", nil, nil) == nil)
		fail("comma parse failed");
	if(tmnorm(&tm) != 631324800)
		fail("wrong result");

	if(tmparse(&tm, "YYYY MM DD hh:mm:ss", "1969 12 31 16:00:00", gmt, nil) == nil)
		sysfatal("failed parse: %r\n");
	testtm("parse-notz1", 1969, 11, 31, 16, 0, 0, 0, &tm);

	if(tmparse(&tm, "YYYY MM DD hh:mm:ss", "1970 01 01 04:00:00", gmt, nil) == nil)
	fail("failed parse: %r\n");
	testtm("parse-notz2", 1970, 0, 1, 4, 0, 0, 0, &tm);

	if(tmparse(&tm, "YYYY MM DD", "1970 01 01", gmt, nil) == nil)
	fail("failed parse: %r\n");
	testtm("parse-notz3", 1970, 0, 1, 0, 0, 0, 0, &tm);

	if(tmparse(&tm, "YYYY MMMM DD WWW hh:mm:ss", "2020 April 10 Friday 16:04:00", gmt, nil) == nil)
		sysfatal("failed parse: %r\n");
	testtm("parse-notz4", 2020, 3, 10, 16, 4, 0, 0, &tm);

	if(tmparse(&tm, "MM DD hh:mm:ss", "12 31 16:00:00", gmt, nil) == nil)
		sysfatal("failed parse: %r\n");
	testtm("parse-notz5", 1970, 11, 31, 16, 0, 0, 0, &tm);

	if(tmparse(&tm, "MM DD h:mm:ss", "12 31 4:00:00", gmt, nil) == nil)
		sysfatal("failed parse: %r\n");
	testtm("parse-mmdd-hms", 1970, 11, 31, 4, 0, 0, 0, &tm);
	if(tm.tzoff != 0) print("%d wrong tzoff expected=%d actual=%d\n", 6, 0, tm.tzoff);

	if(tmparse(&tm, "YYYY MM DD hh:mm:ss", "2020 04 10 23:14:30", us_eastern, nil) == nil)
		fail("failed parse: %r\n");
	testtm("parse-est", 2020, 3, 10, 23, 14, 30, 0, &tm);
	tmtime(&tm, tmnorm(&tm), nil);

	if(tmparse(&tm, "YYYY MM DD hh:mm:ss", "2020 04 10 20:14:30", us_arizona, nil) == nil)
		fail("failed parse: %r\n");
	testtm("parse-az", 2020, 3, 10, 20, 14, 30, 0, &tm);

	if(tmparse(&tm, "YYYY MM DD hh:mm:ss ZZZ", "2020 04 10 20:14:30 EST", us_arizona, nil) == nil)
		fail("failed parse: %r\n");
	testtm("parse-tz1", 2020, 3, 10, 17, 14, 30, 0, &tm);

	if(tmparse(&tm, "YYYY MM DD hh:mm:ss Z", "2020 04 10 20:14:30 -0400", nil, nil) == nil)
		fail("failed parse: %r\n");
	testtm("parse-tz2", 2020, 3, 10, 20, 14, 30, 0, &tm);
	snprint(buf, sizeof(buf), "%τ", tmfmt(&tm, "YYYY MM DD hh:mm:ss Z"));
	if(strcmp(buf, "2020 04 10 20:14:30 -0400") != 0)
		fail("failed format: %s != 2020 04 10 20:14:30 -0400", buf);

	if(tmparse(&tm, "YYYY MM DD hh:mm:ss.ttt", "2020 04 10 20:14:30.207", nil, nil) == nil)
		fail("failed parse: %r\n");
	testtm("parse-milliseconds", 2020, 3, 10, 20, 14, 30, 207*1000*1000, &tm);

	if(tmparse(&tm, "YYYY MM DD hh:mm:ss.nnn", "2020 04 10 20:14:30.207", nil, nil) == nil)
		fail("failed parse: %r\n");
	testtm("parse-nanoseconds", 2020, 3, 10, 20, 14, 30, 207, &tm);

	if(tmparse(&tm, "YYYY MM DD hh:mm:ss.nnn", "2020 04 10 20:14:30.999", nil, nil) == nil)
		fail("failed parse: %r\n");
	testtm("parse-nnn2", 2020, 3, 10, 20, 14, 30, 999, &tm);
	snprint(buf, sizeof(buf), "%τ", tmfmt(&tm, "YYYY MM DD hh:mm:ss.nnn"));
	if(strcmp(buf, "2020 04 10 20:14:30.999") != 0)
		fail("failed format: %s != 2020 04 10 20:14:30.999", buf);

	if(tmparse(&tm, "YYYY MM DD hh:mm:ss.ttt", "2020 04 10 20:14:30.999", nil, nil) == nil)
		fail("failed parse: %r\n");
	testtm("parse-nnn2", 2020, 3, 10, 20, 14, 30, 999*1000*1000, &tm);
	snprint(buf, sizeof(buf), "%τ", tmfmt(&tm, "YYYY MM DD hh:mm:ss.ttt"));
	if(strcmp(buf, "2020 04 10 20:14:30.999") != 0)
		fail("failed format: %s != 2020 04 10 20:14:30.999", buf);

	/* edge case: leap year feb 29 */
	if(tmparse(&tm, "YYYY MM DD hh:mm:ss Z", "2020 02 29 20:14:30 -0400", nil, nil) == nil)
		fail("failed leap year feb 29: %r\n");
	testtm("parse-leapfeb", 2020, 1, 29, 20, 14, 30, 0, &tm);
	if(tmparse(&tm, "YYYY MM DD hh:mm:ss Z", "2021 02 29 20:14:30 -0400", nil, nil) != nil)

		fail("incorrectly accepted non-leap year feb 29\n");
	/* stray spaces */
	if(tmparse(&tm, "YYYY MM DD hh:mm:ss Z", "   2020 02 29 20:14:30 -0400   ", nil, nil) == nil)
		fail("failed leap year feb 29: %r\n");

	/* lots of round trips: Jan 1960 => Jun 2020, in almost-11 day increments */
	for(i = -315619200; i < 1592179806; i += 23*3600 + 1732){
		if(tmtime(&tm, i, nil) == nil)
			fail("load time %d\n", i);
		if(tmnorm(&tm) != i)
			fail("wrong load time: %d\n", i);
		if(snprint(buf, sizeof(buf), "%τ", tmfmt(&tm, "WW MMM DD hh:mm:ss Z YYYY")) == -1)
			fail("format: %r\n");
		if(tmparse(&tt, "WW MMM DD hh:mm:ss Z YYYY", buf, nil, nil) == nil)
			fail("parse: %r\n");
		if(tmnorm(&tm) != tmnorm(&tt))
			fail("parse: wrong time (%lld != %lld)\n", tmnorm(&tm), tmnorm(&tt));
	}

	/* lots of round trips: Jan 1960 => Jun 2020, in almost-dailyincrements, now with timezone */
	for(i = -315619200; i < 1592179806; i += 23*3600 + 1732){
		if(tmtime(&tm, i, us_eastern) == nil)
			fail("load time %d\n", i);
		if(tmnorm(&tm) != i)
			fail("wrong load time: %d\n", i);
		if(snprint(buf, sizeof(buf), "%τ", tmfmt(&tm, "WW MMM DD hh:mm:ss Z YYYY")) == -1)
			fail("format: %r\n");
		if(tmparse(&tt, "WW MMM DD hh:mm:ss Z YYYY", buf, us_arizona, nil) == nil)
			fail("parse: %r\n");
		if(tmnorm(&tm) != tmnorm(&tt))
			fail("parse: wrong time (%lld != %lld)\n", tmnorm(&tm), tmnorm(&tt));
		tm = tt;
		tmnorm(&tm);
		testtm("norm-rt", tt.year + 1900, tt.mon, tt.mday, tt.hour, tt.min, tt.sec, 0, &tm);
	}

	if(tmtime(&tm, -624623143, nil) == nil)
		fail("tmtime: %r");
	if(snprint(buf, sizeof(buf), "%τ", tmfmt(&tm, "WW, DD MMM YYYY hh:mm:ss Z")) == -1)
		fail("format: %r");
	if(strcmp(buf, "Fri, 17 Mar 1950 13:34:17 +0000") != 0)
		fail("wrong output: %s\n", buf);
	if(tmtime(&tm, -624623143, us_eastern) == nil)
		fail("tmtime: %r");
	if(snprint(buf, sizeof(buf), "%τ", tmfmt(&tm, "WW, DD MMM YYYY hh:mm:ss Z")) == -1)
		fail("format: %r");
	if(strcmp(buf, "Fri, 17 Mar 1950 08:34:17 -0500") != 0)
		fail("wrong output: %s\n", buf);

	/* AM and PM parsing */
	for(i = 0; i < 24; i++){
		h = i % 12;
		if(h == 0)
			h = 12;
		snprint(buf, sizeof(buf), "2021 02 01 %d:14:30 -0400", i);
		snprint(buf1, sizeof(buf1), "2021 02 01 %d:14:30 -0400 %s", h, (i < 12) ? "AM" : "PM");
		if(tmparse(&tm, "YYYY MM DD hh:mm:ss Z", buf, nil, nil) == nil)
			fail("parse: %r\n");
		if(tmparse(&tt, "YYYY MM DD hh:mm:ss Z A", buf1, nil, nil) == nil)
			fail("parse: %r\n");
		if(tmnorm(&tm) != tmnorm(&tt))
			print("bad am/pm parsed: %s != %s (%lld != %lld)\n", buf, buf1, tmnorm(&tm), tmnorm(&tt));
	}

	/* ordinal day suffix parsing and formatting */
	for(i = 1; i < 32; i++){
		snprint(buf, sizeof(buf), "2023 08 %d", i);
		snprint(buf1, sizeof(buf1), "2023 08 %02d%s", i,
			i == 1 || i == 21 || i == 31? "st": i == 2 || i == 22? "nd": i == 3 || i == 23? "rd": "th");
		if(tmparse(&tm, "YYYY MM DD", buf, nil, nil) == nil)
			fail("parse: %r\n");
		if(tmparse(&tt, "YYYY MM DDo", buf1, nil, nil) == nil)
			fail("parse: %r\n");
		if(tmnorm(&tm) != tmnorm(&tt))
			print("bad ordinal day suffix parsed: %s != %s (%lld != %lld)\n", buf, buf1, tmnorm(&tm), tmnorm(&tt));
		if(tmparse(&tm, "YYYY MM DDo", buf, nil, nil) != nil)
			print("ordinal day suffix parsed when absent\n");
		if(snprint(buf, sizeof(buf), "%τ", tmfmt(&tm, "YYYY MM DDo")) == -1)
			fail("format: %r");
		if(strcmp(buf, buf1) != 0)
			print("bad ordinal day suffix formatted: %s != %s\n", buf, buf1);
	}

	/* Time zone boundaries: entering DST */
	if(tmtime(&tm, 1520733600, us_eastern) == nil)
		fail("tmtime: tz boundary");
	if(snprint(buf, sizeof(buf), "%τ", tmfmt(&tm, nil)) == -1)
		fail("format: %r");
	memset(&tm, 0, sizeof(tm));
	if(tmparse(&tm, "WW MMM D hh:mm:ss ZZZ YYYY", buf, nil, nil) == nil)
		fail("parse: %r\n");
	if(tmnorm(&tm) != 1520733600)
		fail("round trip timezone: %lld != 1520733600\n", tmnorm(&tm));

	/* Time zone boundaries: leaving DST */
	if(tmtime(&tm, 1541296800, us_eastern) == nil)
		fail("tmtime: tz boundary");
	if(snprint(buf, sizeof(buf), "%τ", tmfmt(&tm, nil)) == -1)
		fail("format: %r\n");
	memset(&tm, 0, sizeof(tm));
	if(tmparse(&tm, "WW MMM D hh:mm:ss ZZZ YYYY", buf, nil, nil) == nil)
		fail("parse: %r");
	if(tmnorm(&tm) != 1541296800)
		fail("round trip timezone: %lld != 1541296800\n", tmnorm(&tm));
	

	/* flexible date parsing */
	if(tmparse(&tm, "?YYYY ?MM DD hh:mm:ss ?ZZZ", "89 04 10 20:14:30 -0400", nil, nil) == nil)
		fail("failed parse: %r\n");
	testtm("flexdates", 1989, 3, 10, 20, 14, 30, 0, &tm);
	char **d, *flexdates[] = {
		"1920 4 10 20:14:30 -0400",
		"1920 04 10 20:14:30 -0400",
		"1920 Apr 10 20:14:30 -0400",
		"1920 Apr 10 20:14:30 -04:00",
		"1920 Apr 10 20:14:30 -04:00",
		"1920 Apr 10 20:14:30 -04:00",
		"1920 April 10 20:14:30 EDT",
		"20 April 10 20:14:30 EDT",
		nil,
	};
	for(d = flexdates; *d; d++){
		if(tmparse(&tm, "?YYYY ?MM DD hh:mm:ss ?ZZZ", *d, nil, nil) == nil)
			fail("failed parse: %r\n");
		testtm("flexdates", 1920, 3, 10, 20, 14, 30, 0, &tm);
	}

	/* Fuzzy zone */
	if(tmparse(&tm, "?YYYY ?MM DD hh:mm:ss ?ZZZ", "2020 04 10 20:14:30 NOPE", nil, nil) == nil)
		fail("failed parse: %r\n");
	testtm("fuzzy-nonzone", 2020, 3, 10, 20, 14, 30, 0, &tm);

	/* test tmnorm() offset */
	memset(&tm, 0, sizeof(Tm));
	tm.year = 120;
	tm.sec=0;
	tm.min=0;
	tm.hour=0;
	tm.mday=22;
	tm.mon=5;
	tm.tz=us_central;
	tmnorm(&tm);
	if(tmnorm(&tm) != 1592802000)
		fail("tmnorm is not using the daylight savings time offset. %lld != 1592809200\n", tmnorm(&tm));

	memset(&tm, 0, sizeof(Tm));
	if(tmnow(&tm, us_central) == nil)
		fail("tmnow(): %r");
	tm.year = 120;
	tm.sec=0;
	tm.min=0;
	tm.hour=0;
	tm.mday=22;
	tm.mon=5;
	tm.tz=us_central;
	s = tmnorm(&tm);
	if(s != 1592802000)
		fail("tmnorm is not using the daylight savings time offset. %lld != 1592809200\n", s);
	tm.year = 120;
	tm.sec=0;
	tm.min=0;
	tm.hour=0;
	tm.mday=22;
	tm.mon=0;
	s = tmnorm(&tm);
	if(s != 1579672800)
		fail("tmnorm is not using the daylight savings time offset. %lld != 1579672800\n", s);
	tm.tz=us_eastern;
	s = tmnorm(&tm);
	if(s != 1579669200)
		fail("tmnorm converted to us_eastern. %lld != 1579669200\n", s);
	tm.tz=us_central;
	s = tmnorm(&tm);
	if(s != 1579672800)
		fail("tmnorm converted back to us_central. %lld != 1579672800\n", s);

	if(failed)
		exits("test failed");
	exits(nil);
}
