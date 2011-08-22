#include <u.h>
#include <libc.h>

void
main(int argc, char **argv)
{
	int swapfd, cswfd;
	char buf[1024], *p;
	Dir *d;

	ARGBEGIN {
	} ARGEND;

	if(argc != 1){
		fprint(2, "Usage: swap file\n");
		exits("usage");
	}

	swapfd = -1;
	if(d = dirstat(p = *argv)){
		if(d->mode & DMDIR){
			p = getenv("sysname");
			if(p == 0)
				p = "swap";
			snprint(buf, sizeof buf, "%s/%sXXXXXXX", *argv, p);
			p = mktemp(buf);
		} else
			swapfd = open(p, ORDWR);
	}
	if(d == nil || (d->mode & DMDIR)){
		if((swapfd = create(p, ORDWR|ORCLOSE, 0600)) >= 0){
			Dir nd;

			nulldir(&nd);
			nd.mode = DMTMP|0600;
			dirfwstat(swapfd, &nd);
		}
	}
	if(swapfd < 0)
		sysfatal("%r");
	if(fd2path(swapfd, p = buf, sizeof buf))
		sysfatal("fd2path: %r");
	if(putenv("swap", p) < 0)
		sysfatal("putenv: %r");

	print("swap: %s\n", p);

	if((cswfd = open("/dev/swap", OWRITE)) < 0)
		sysfatal("open: %r");
	if(fprint(cswfd, "%d", swapfd) < 0)
		sysfatal("write: %r");

	exits(0);
}
