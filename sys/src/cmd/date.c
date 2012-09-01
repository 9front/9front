#include <u.h>
#include <libc.h>

int uflg, nflg, iflg, tflg;

char*
isodate(Tm *t)
{
	static char c[25]; /* leave room to append isotime */
	snprint(c, 11, "%04d-%02d-%02d", 
		t->year + 1900, t->mon + 1, t->mday);
	return c;
}

char*
isotime(Tm *t)
{
	int tz;
	char *c, *d;
	d = isodate(t);
	c = d+10;
	snprint(c, 10, "T%02d:%02d:%02d",
		t->hour, t->min, t->sec); /* append to isodate */
	tz = t->tzoff / 60;
	if(t->tzoff) {
		/* localtime */
		if (t->tzoff > 0) {
			c[9] = '+';
		} else {
			c[9] = '-';
			tz = -tz;
		}
		snprint(c+10, 5, "%02d%02d", tz / 60, tz % 60);
	} else {
		c[9] = 'Z';
		c[10] = 0;
	}
	return d;
}

void
main(int argc, char *argv[])
{
	ulong now;
	Tm *tm;
	ARGBEGIN{
	case 'n':	nflg = 1; break;
	case 'u':	uflg = 1; break;
	case 't':	tflg = 1; /* implies -i */
	case 'i':	iflg = 1; break;
	default:	fprint(2, "usage: date [-itun] [seconds]\n"); exits("usage");
	}ARGEND

	if(argc == 1)
		now = strtoul(*argv, 0, 0);
	else
		now = time(0);

	if(nflg)
		print("%ld\n", now);
	else {
		tm = uflg ? gmtime(now) : localtime(now);
		if(iflg) {
			if(tflg)
				print("%s\n", isotime(tm));
			else
				print("%s\n", isodate(tm));
		} else
			print("%s", asctime(tm));
	}
	exits(0);
}
