#include	<u.h>
#include	<libc.h>

int
getppid(void)
{
	char b[20];
	int f;

	memset(b, 0, sizeof(b));
	f = open("/dev/ppid", OREAD|OCEXEC);
	if(f >= 0) {
		read(f, b, sizeof(b));
		close(f);
	}
	return atol(b);
}
