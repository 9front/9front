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

void
usage(void)
{
	fprint(2,"usage: %s time command [ arg ... ]\n", argv0);
	exits("usage");
}

static void
catch(void *, char *msg)
{
	postnote(PNGROUP, getpid(), msg);
	noted(NDFLT);
}

void
main(int argc, char *argv[])
{
	char buf[1024], *p, *q;
	Waitmsg *w;
	long n, t;

	argv0 = argv[0];
	if(argc < 3)
		usage();
	n = strtol(argv[1], &p, 10);
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
	rfork(RFNOTEG);
	switch(rfork(RFFDG|RFREND|RFPROC|RFMEM)){
	case -1:
		sysfatal("%r");
	case 0: /* child */
		exec(argv[2], &argv[2]);
		if(argv[2][0] != '/' && strncmp(argv[2], "./", 2) &&
		   strncmp(argv[2], "../", 3)){
			snprint(buf, sizeof(buf), "/bin/%s", argv[2]);
			exec(argv[2] = buf, &argv[2]);
		}
		sysfatal("%s: %r", argv[2]);
	}
	notify(catch);
	alarm(t);
	if(w = wait())
		exits(w->msg);
	exits("alarm");
}
