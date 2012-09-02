#include <u.h>
#include <libc.h>

int uflg, nflg, iflg, tflg;

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
