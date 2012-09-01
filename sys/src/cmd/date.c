#include <u.h>
#include <libc.h>

int uflg, nflg, iflg, tflg;

char*
isodate(Tm *t)
{
	static char c[10+14+1]; /* leave room to append isotime */

	ct_numb(c, t->year / 100 + 119);
	ct_numb(c+2, t->year % 100 + 100);
	c[4] = '-';
	ct_numb(c+5, t->mon + 101);
	c[7] = '-';
	ct_numb(c+8, t->mday + 100);
	c[10] = 0;
	return c;
}

char*
isotime(Tm *t)
{
	int tz;
	char *c, *d;
	d = isodate(t);
	c = d + 10; /* append to isodate */
	c[0] = 'T';
	ct_numb(c+1, t->hour+100);
	c[3] = ':';
	ct_numb(c+4, t->min+100);
	c[6] = ':';
	ct_numb(c+7, t->sec+100);
	tz = t->tzoff / 60;
	if(t->tzoff) {
		/* localtime */
		if (t->tzoff > 0) {
			c[9] = '+';
		} else {
			c[9] = '-';
			tz = -tz;
		}
		ct_numb(c+10, tz / 60 + 100);
		ct_numb(c+12, tz % 60 + 100);
		c[14] = 0;
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
	else if(iflg) {
		tm = uflg ? gmtime(now) : localtime(now);
		if(tflg)
			print("%s\n", isotime(tm));
		else
			print("%s\n", isodate(tm));
	} else {
		if(uflg)
			print("%s", asctime(gmtime(now)));
		else
			print("%s", ctime(now));
	}
	exits(0);
}
