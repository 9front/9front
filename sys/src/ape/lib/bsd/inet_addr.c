/* posix */
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>

unsigned long
inet_addr(char *from)
{
	struct in_addr in;

	if(inet_aton(from, &in) == 0)
		return INADDR_NONE;
	return in.s_addr;
}
