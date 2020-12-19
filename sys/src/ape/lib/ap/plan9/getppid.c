#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "sys9.h"

pid_t
getppid(void)
{
	char buf[32];
	int f;

	snprintf(buf, sizeof(buf), "/proc/%d/ppid", getpid());
	f = open(buf, 0);
	if(f < 0)
		return 0;
	memset(buf, 0, sizeof(buf));
	read(f, buf, sizeof(buf)-1);
	close(f);
	return atol(buf);
}
