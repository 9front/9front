#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

Process **PP;

static int nflag;

void
dump(void)
{
	int i;
	
	for(i = 0; i < 16; i++) {
		print("R%2d %.8ux", i, P->R[i]);
		if((i % 4) == 3) print("\n");
		else print("\t");
	}
}

static void
adjustns(void)
{
	if(bind("/arm/bin", "/bin", MREPL) < 0)
		sysfatal("bind: %r");
	if(bind("/rc/bin", "/bin", MAFTER) < 0)
		sysfatal("bind: %r");
	putenv("cputype", "arm");
	putenv("objtype", "arm");
}

void
cleanup(void)
{
	if(P == nil)
		return;

	freesegs();
	fddecref(P->fd);
	free(P);
}

static void
usage(void)
{
	fprint(2, "usage: 5e [ -n ] text [ args ]\n");
	exits(nil);
}

void
main(int argc, char **argv)
{
	ARGBEGIN {
	case 'n': nflag++; break;
	default: usage();
	} ARGEND;
	if(argc < 1)
		usage();
	if(_nprivates < 1)
		sysfatal("we don't have privates");
	if(rfork(RFREND | RFNAMEG | RFENVG) < 0)
		sysfatal("rfork: %r");
	atexit(cleanup);
	if(nflag)
		adjustns();
	initproc();
	if(loadtext(argv[0], argc, argv) < 0)
		sysfatal("%r");
	for(;;) {
		if(ultraverbose)
			dump();
		step();
	}
}
