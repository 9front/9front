#include <u.h>
#include <libc.h>
#include <ctype.h>

char *knownfmt[] = {
	/* asctime */
	"WW MMM DD hh:mm:ss ?Z YYYY",
	/* RFC3339 */
	"YYYY-MM-DD[T]hh:mm:ss[Z]?Z",
	"YYYY-MM-DD[T]hh:mm:ss[Z]?Z",
	"YYYY-MM-DD[T]hh:mm:ss ?Z",
	"YYYY-MM-DD[T]hh:mm:ss?Z",
	nil,
};

char *datefmt[] = {
	/* RFC5322 */
	"?W ?DD ?MMM ?YYYY",
	"?W, DD-?MM-YY",
	/* RFC822/RFC2822 */
	"DD MMM YYYY",
	"DD MMM YY",
	/* RFC850 */
	"WW, DD-MMM-YY",
	/* RFC1123 */
	"WWW, DD MMM YYYY",
	/* RFC 3339 and human-readable variants */
	"YYYY-MM-DD",
	"YYYY-MM-DD [@] ",
	/* random formats */
	"?W ?MMM ?DD ?YYYY",
	"?MMM ?DD ?YYYY",
	"?DD ?MM ?YYYY",
	"MMM ?DD ?YYYY",
	"YYYY ?MM ?DD",
	"YYYY ?DD ?MM",
	"YYYY/MM?/DD?",
	"MMM YYYY ?DD",
	"?DD YYYY MMM",
	"MM/DD/YYYY",
	nil
};

char *timefmt[] = {
	" hh:mm:ss",
	" hh:mm",
	" hh",
	" hh:mm:ss ?A",
	" hh:mm ?A",
	" hh ?A",
	"",
	nil,
};

char *zonefmt[] = {
	" ?Z",
	"",
	nil,
};

static int
nojunk(char *p)
{
	while(isspace(*p))
		p++;
	if(*p == '\0')
		return 1;
	werrstr("trailing junk");
	return 0;
}

static void
usage(void)
{
	fprint(2, "usage: %s [-f fmt] date-time...\n", argv0);
	exits("usage");
}

/*
 * seconds absolute_date ... - convert absolute_date to seconds since epoch
 */
void
main(int argc, char **argv)
{
	char **f, **df, **tf, **zf, *fmt, *ep, buf[256];
	Tzone *tz;
	Tm tm;
	int i;

	fmt = nil;
	ARGBEGIN{
	case 'f':
		fmt = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;

	if((tz = tzload("local")) == nil)
		sysfatal("bad local time: %r");
	for(i = 0; i < argc; i++){
		if(fmt != nil){
			if(tmparse(&tm, fmt, argv[i], tz, &ep) && nojunk(ep))
				goto Found;
		}else{
			for(f = knownfmt; *f != nil; f++)
				if(tmparse(&tm, *f, argv[i], tz, &ep) != nil && nojunk(ep))
					goto Found;
			for(df = datefmt; *df; df++)
			for(tf = timefmt; *tf; tf++)
			for(zf = zonefmt; *zf; zf++){
				snprint(buf, sizeof(buf), "%s%s%s", *df, *tf, *zf);
				if(tmparse(&tm, buf, argv[i], tz, &ep) != nil && nojunk(ep))
					goto Found;
			}
		}
		sysfatal("tmparse: %r");
Found:
		print("%lld\n", tmnorm(&tm));
	}
	exits(nil);
}
