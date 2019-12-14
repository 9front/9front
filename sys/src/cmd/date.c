#include <u.h>
#include <libc.h>

static char *day[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};

static char *mon[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul",
	"Aug", "Sep", "Oct", "Nov", "Dec"
};

int uflg, nflg, iflg, tflg, mflg;

char*
isodate(Tm *t)
{
	static char c[26]; /* leave room to append isotime */
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
		snprint(c+10, 6, "%02d:%02d", tz / 60, tz % 60);
	} else {
		c[9] = 'Z';
		c[10] = 0;
	}
	return d;
}

char *
mailtime(Tm *t)
{
	static char c[64];
	char *sgn;
	int off;

	sgn = "+";
	if(t->tzoff < 0)
		sgn = "";
	off = (t->tzoff/3600)*100 + (t->tzoff/60)%60;
	snprint(c, sizeof(c), "%s, %.2d %s %.4d %.2d:%.2d:%.2d %s%.4d",
		day[t->wday], t->mday, mon[t->mon], t->year + 1900,
		t->hour, t->min, t->sec, sgn, off);
	return c;
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
	case 'm':	mflg = 1; break;
	default:	fprint(2, "usage: date [-itunm] [seconds]\n"); exits("usage");
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
		} else if(mflg)
			print("%s\n", mailtime(tm));
		else
			print("%s", asctime(tm));
	}
	exits(0);
}
