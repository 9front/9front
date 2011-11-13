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
notefun(void *a, char *msg)
{
	USED(a);
 	postnote(PNGROUP, cpid, msg);
	if(strcmp(msg, "alarm") == 0){
		return 1;
	}
	return 0;
}

void
main(int argc, char *argv[])
{
	char *cmd;
	int t;
	ARGBEGIN{
	default: usage();
	}ARGEND

	if(*argv == nil)
		usage();

	t = atoi(argv[0]);
	argv++;
	if(*argv ==nil)
		usage();
	cmd = argv[0];
	/* cmd must be a path, absolute or relative */
	if(*cmd != '/' && strcmp(cmd, "./") != 0 && strcmp(cmd, "../") != 0)
		usage();
	argv[0] = strrchr(cmd,'/');
	atnotify(notefun,1);
	alarm(t*1000);

	switch((cpid = rfork(RFFDG|RFREND|RFPROC|RFMEM|RFNOTEG))){
	case -1:
		sysfatal("rfork: %r");
	case 0: /* child */
		exec(cmd,argv);
	default: /* parent */
		break;
	}
	waitpid();
}

