#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "sys9.h"

pid_t
getppid(void)
{
	char b[20];
	int f;

	memset(b, 0, sizeof(b));
	f = _OPEN("/dev/ppid", OREAD);
	if(f >= 0) {
		_PREAD(f, b, sizeof(b), 0);
		_CLOSE(f);
	}
	return atol(b);
}
