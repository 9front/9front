#include "all.h"
#include "io.h"

/*
 * based on libthread's threadsetname, but drags in less library code.
 * actually just sets the arguments displayed.
 */
void
procsetname(char *fmt, ...)
{
	int fd;
	char *cmdname;
	char buf[128];
	va_list arg;

	va_start(arg, fmt);
	cmdname = vsmprint(fmt, arg);
	va_end(arg);
	if (cmdname == nil)
		return;
	snprint(buf, sizeof buf, "#p/%d/args", getpid());
	if((fd = open(buf, OWRITE)) >= 0){
		write(fd, cmdname, strlen(cmdname)+1);
		close(fd);
	}
	free(cmdname);
}

void
newproc(void (*f)(void *), void *arg, char *text)
{
	int kid = rfork(RFPROC|RFMEM|RFNOWAIT);

	if (kid < 0)
		sysfatal("can't fork: %r");
	if (kid == 0) {
		procsetname("%s", text);
		(*f)(arg);
		exits("child returned");
	}
}
