#include <u.h>
#include <libc.h>
#include <libsec.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

char Eio[] = "i/o error";
char Enotadir[] = "not a directory";
char Enoent[] = "not found";
char Einval[] = "invalid operation";
char Eperm[] = "permission denied";
char Eexists[] = "file exists";
char Elocked[] = "file locked";

void*
emalloc(int c)
{
	void *v;
	
	v = mallocz(c, 1);
	if(v == 0)
		sysfatal("malloc: %r");
	setmalloctag(v, getcallerpc(&c));
	return v;
}

ThrData *
getthrdata(void)
{
	ThrData **v;
	
	v = (ThrData **) threaddata();
	if(*v == nil){
		*v = emalloc(sizeof(**v));
		(*v)->resp = chancreate(sizeof(void *), 0);
	}
	return *v;
}

Fs *fsmain;

int
dprint(char *fmt, ...)
{
	va_list va;
	int rc;
	
	va_start(va, fmt);
	rc = vfprint(2, fmt, va);
	va_end(va);
	return rc;
}

static void
syncproc(void *)
{
	for(;;){
		sync(0);
		sleep(SYNCINTERVAL);
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-rsS] [-m mem] [-n service] -f dev\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	Dev *d;
	char *file, *service;
	int doream, flags, stdio, nbuf;

	doream = 0;
	stdio = 0;
	flags = FSNOAUTH;
	service = "fs";
	file = nil;
	nbuf = 10;
	ARGBEGIN {
	case 'A': flags &= ~FSNOAUTH; break;
	case 'r': doream++; break;
	case 'S': flags |= FSNOPERM | FSCHOWN; break;
	case 's': stdio++; break;
	case 'f': file = strdup(EARGF(usage())); break;
	case 'n': service = strdup(EARGF(usage())); break;
	case 'm':
		nbuf = muldiv(atoi(EARGF(usage())), 1048576, sizeof(Buf));
		if(nbuf < 10)
			nbuf = 10;
		break;
	default: usage();
	} ARGEND;
	rfork(RFNOTEG);
	bufinit(nbuf);
	if(file == nil)
		sysfatal("no default file");
	if(argc != 0)
		usage();
	d = newdev(file);
	if(d == nil)
		sysfatal("newdev: %r");
	fsmain = initfs(d, doream, flags);
	if(fsmain == nil)
		sysfatal("fsinit: %r");
	initcons(service);
	proccreate(syncproc, nil, mainstacksize);
	start9p(service, stdio);
	threadexits(nil);
}

void
shutdown(void)
{
	wlock(fsmain);
	sync(1);
	dprint("hjfs: ending\n");
	sleep(1000);
	sync(1);
	threadexitsall(nil);
}
