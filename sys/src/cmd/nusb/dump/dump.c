#include <u.h>
#include <libc.h>
#include <thread.h>
#include "usb.h"

static void
usage(void)
{
	fprint(2, "usage: %s devid\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	Dev *d;

	fmtinstall('H', encodefmt);
	fmtinstall('U', Ufmt);
	ARGBEGIN{
	}ARGEND
	if(*argv == nil)
		usage();
	if((d = getdev(*argv)) == nil)
		sysfatal("getdev: %r");
	print("%U", d);
	exits(nil);
}
