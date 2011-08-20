#include	<u.h>
#include	<libc.h>
#include	<tos.h>

int
getpid(void)
{
	return _tos->pid;
}
