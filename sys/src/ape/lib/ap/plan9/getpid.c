#include "lib.h"
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "sys9.h"

typedef long long vlong;
typedef unsigned long ulong;
typedef unsigned long long uvlong;
#include	"/sys/include/tos.h"

pid_t
getpid(void)
{
	return _tos->pid;
}
