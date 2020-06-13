#include <u.h>
#include <libc.h>

/*
 * seconds absolute_date ... - convert absolute_date to seconds since epoch
 */
char *formats[] = {
	/* asctime */
	"W MMM DD hh:mm:ss ?Z YYYY",
	/* RFC5322 */
	"?W ?DD ?MMM ?YYYY hh:mm:ss ?Z",
	"?W, DD-?MM-YY hh:mm:ss ?Z",
	/* RFC822/RFC8222 */
	"DD MMM YY hh:mm ZZZ",
	"DD MMM YY hh:mm Z",
	/* RFC850 */
	"W, DD-MMM-YY hh:mm:ss MST",
	/* RFC1123 */
	"WW, DD MMM YYYY hh:mm:ss ZZZ",
	/* RFC1123Z */
	"WW, DD MMM YYYY hh:mm:ss ZZ",
	/* RFC3339 */
	"YYYY-MM-DD[T]hh:mm:ss[Z]ZZ",
	"YYYY-MM-DD[T]hh:mm:ss[Z]Z",
	"YYYY-MM-DD[T]hh:mm:ss ZZ",
	"YYYY-MM-DD[T]hh:mm:ss Z",
	/* RFC 3339 and human-readable variants */
	"YYYY-MM-DD hh:mm:ss",
	"YYYY-MM-DD hh:mm:ss ?Z",
	"YYYY-MM-DD [@] hh:mm:ss",
	"YYYY-MM-DD [@] hh:mm:ss ?Z",
	nil
};

static void
usage(void)
{
	fprint(2, "usage: %s date-time ...\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	Tm tm;
	char **f, *fmt;
	int i;

	fmt = nil;
	ARGBEGIN{
	case 'f':
		fmt = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;

	for(i = 0; i < argc; i++){
		if(fmt != nil){
			if(tmparse(&tm, fmt, argv[i], nil) != nil)
				goto Found;
		}else{
			for(f = formats; *f != nil; f++)
				if(tmparse(&tm, *f, argv[i], nil) != nil)
					goto Found;
		}
		sysfatal("tmparse: %r");
Found:
		print("%lld\n", tm.abs);
	}
	exits(nil);
}
