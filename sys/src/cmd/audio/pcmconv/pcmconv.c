#include <u.h>
#include <libc.h>
#include <pcm.h>

static void
usage(void)
{
	fprint(2, "usage: %s [-i fmt] [-o fmt] [-l length]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	uchar ibuf[8*1024], *obuf;
	Pcmdesc i, o;
	Pcmconv *c;
	vlong l;
	int n;

	i = pcmdescdef;
	o = pcmdescdef;
	l = -1LL;
	ARGBEGIN {
	case 'i':
		if(mkpcmdesc(EARGF(usage()), &i) < 0)
			sysfatal("%r");
		break;
	case 'o':
		if(mkpcmdesc(EARGF(usage()), &o) < 0)
			sysfatal("%r");
		break;
	case 'l':
		l = atoll(EARGF(usage()));
		break;
	default:
		usage();
	} ARGEND;

	if((c = allocpcmconv(&i, &o)) == nil)
		sysfatal("%r");
	if((n = pcmratio(c, sizeof(ibuf))) < 0)
		sysfatal("%r");
	obuf = malloc(n);

	for(;;){
		n = sizeof(ibuf);
		if(l >= 0 && l < n)
			n = l;
		n = read(0, ibuf, n);
		if(n < 0)
			sysfatal("read: %r");
		if(l > 0)
			l -= n;
		n = pcmconv(c, ibuf, obuf, n);
		if(n > 0){
			if(write(1, obuf, n) != n)
				sysfatal("write: %r");
		}
		if(n == 0)
			break;
	}
	exits(nil);
}
