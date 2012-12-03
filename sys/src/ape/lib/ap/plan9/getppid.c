#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "sys9.h"

pid_t
getppid(void)
{
	char b[20];
	int f;

	memset(b, 0, sizeof(b));
	f = open("/dev/ppid", 0);
	if(f >= 0) {
		read(f, b, sizeof(b));
		close(f);
	}
	return atol(b);
}
