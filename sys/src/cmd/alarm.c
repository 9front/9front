/*
*	alarm
*
*	you may think some rc script can be same effect
*	I did but I realized rc script does not work cleanly.
*	The bellowing has a problem. so I wrote in C
*	-Kenar-
*	
*	#!/bin/rc
*	if(~ $* 0 1)
*		echo usage: alarm time command arg ...
*	rfork e
*	t=$1
*	shift
*	c=$*
*	{	sleep $t;
*		if(test -e /proc/$pid)
*			echo alarm >/proc/$pid/note
*	}&
*	exec $c
*
*/

#include <u.h>
#include <libc.h>

int cpid;

void
usage(void)
{
	fprint(2,"usage: alarm time path arg ...\n");
	exits("usage");
}

static int
notefun(void *, char *msg)
{
 	postnote(PNGROUP, cpid, msg);
	return 1;
}

void
main(int argc, char *argv[])
{
	char *path, *p, *q;
	Waitmsg *w;
	long n, t;

	ARGBEGIN{
	default: usage();
	}ARGEND

	if(argc < 2)
		usage();
	n = strtol(*argv++, &p, 10);
	if(n < 0)
		usage();
	t = n * 1000;
	if(*p++ == '.' && (n = strtol(p, &q, 10)) > 0){
		switch(q - p){
		case 0:
			break;
		case 1:
			n *= 100;
			break;
		case 2:
			n *= 10;
			break;
		default:
			p[3] = 0;
			n = strtol(p, 0, 10);
			break;
		}
		t += n;
	}
	path = *argv;
	if(p = strrchr(path, '/'))
		if(p[1])
			*argv = p+1;
	atnotify(notefun,1);
	switch((cpid = rfork(RFFDG|RFREND|RFPROC|RFMEM|RFNOTEG))){
	case -1:
		sysfatal("%r");
	case 0: /* child */
		exec(path, argv);
		sysfatal("%s: %r", *argv);
	}
	alarm(t);
	if(w = wait())
		exits(w->msg);
	exits("alarm");
}
