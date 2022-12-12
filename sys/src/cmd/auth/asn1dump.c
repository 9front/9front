#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>

void
usage(void)
{
	fprint(2, "auth/asn1dump [file]\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{
	int fd, n, tot;
	uchar *buf;
	char *file;

	fmtinstall('B', mpfmt);
	fmtinstall('H', encodefmt);
	fmtinstall('[', encodefmt);

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(argc != 0 && argc != 1)
		usage();

	if(argc == 1)
		file = argv[0];
	else
		file = "/fd/0";

	if((fd = open(file, OREAD)) < 0)
		sysfatal("open %s: %r", file);

	buf = nil;
	tot = 0;
	for(;;){
		buf = realloc(buf, tot+8192);
		if(buf == nil)
			sysfatal("realloc: %r");
		if((n = read(fd, buf+tot, 8192)) < 0)
			sysfatal("read: %r");
		if(n == 0)
			break;
		tot += n;
	}

	asn1dump(buf, tot);

	X509dump(buf, tot);

	exits(nil);
}
