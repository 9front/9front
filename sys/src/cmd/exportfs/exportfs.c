#include <u.h>
#include <libc.h>
#include <fcall.h>
#define Extern
#include "exportfs.h"

int	srvfd = -1;
int	readonly;

void
usage(void)
{
	fprint(2, "usage: %s [-dsR] [-m msize] [-r root] "
		"[-P patternfile] [-S srvfile]\n", argv0);
	fatal("usage");
}

void
main(int argc, char **argv)
{
	char *srv, *srvfdfile;

	srv = nil;
	srvfd = -1;
	srvfdfile = nil;

	ARGBEGIN{
	case 'd':
		dbg++;
		break;

	case 'm':
		messagesize = strtoul(EARGF(usage()), nil, 0);
		break;

	case 'r':
		srv = EARGF(usage());
		break;

	case 's':
		srv = "/";
		break;

	case 'F':
		/* accepted but ignored, for backwards compatibility */
		break;

	case 'P':
		patternfile = EARGF(usage());
		break;

	case 'R':
		readonly = 1;
		break;

	case 'S':
		if(srvfdfile != nil)
			usage();
		srvfdfile = EARGF(usage());
		break;

	default:
		usage();
	}ARGEND
	USED(argc, argv);

	if(srvfdfile != nil){
		if(srv != nil){
			fprint(2, "exportfs: -S cannot be used with -r or -s\n");
			usage();
		}
		if((srvfd = open(srvfdfile, ORDWR)) < 0)
			fatal("open %s: %r", srvfdfile);
	} else if(srv == nil)
		usage();

	exclusions();

	DEBUG(2, "exportfs: started\n");

	rfork(RFNOTEG|RFREND);

	if(messagesize == 0){
		messagesize = iounit(0);
		if(messagesize == 0)
			messagesize = IOUNIT+IOHDRSZ;
	}
	fhash = emallocz(sizeof(Fid*)*FHASHSIZE);

	fmtinstall('F', fcallfmt);

	if(srvfd == -1) {
		if(chdir(srv) < 0) {
			char ebuf[ERRMAX];
			ebuf[0] = '\0';
			errstr(ebuf, sizeof ebuf);
			DEBUG(2, "chdir(\"%s\"): %s\n", srv, ebuf);
			mounterror(ebuf);
		}
		DEBUG(2, "invoked as server for %s", srv);
	}

	DEBUG(2, "\niniting root\n");
	initroot();
	io();
}
