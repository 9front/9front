#include <u.h>
#include <libc.h>

void
procsetname(char *fmt, ...)
{
	int fd, n;
	char buf[128];
	va_list arg;

	snprint(buf, sizeof buf, "/proc/%lud/args", (ulong)getpid());
	fd = open(buf, OWRITE|OCEXEC);
	if(fd < 0)
		return;
	va_start(arg, fmt);
	n = vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);
	write(fd, buf, n+1);
	close(fd);
}
