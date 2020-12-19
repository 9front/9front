#include	<u.h>
#include	<libc.h>

int
getppid(void)
{
	char buf[32];
	int f;

	snprint(buf, sizeof(buf), "/proc/%lud/ppid", (ulong)getpid());
	f = open(buf, OREAD|OCEXEC);
	if(f < 0)
		return 0;
	memset(buf, 0, sizeof(buf));
	read(f, buf, sizeof(buf)-1);
	close(f);
	return atol(buf);
}
