/* posix */
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>

#include "priv.h"

int
getpeername(int fd, struct sockaddr *addr, int *alen)
{
	Rock *r;
	int olen, len;
	struct sockaddr_un *runix;

	r = _sock_findrock(fd, 0);
	if(r == 0){
		errno = ENOTSOCK;
		return -1;
	}

	switch(r->domain){
	case PF_INET:
		len = sizeof(struct sockaddr_in);
		break;
	case PF_INET6:
		len = sizeof(struct sockaddr_in6);
		break;
	case PF_UNIX:
		runix = (struct sockaddr_un*)&r->raddr;
		len = &runix->sun_path[strlen(runix->sun_path)] - (char*)runix;
		break;
	default:
		errno = EAFNOSUPPORT;
		return -1;
	}

	if(alen != 0){
		olen = *alen;
		*alen = len;
		if(olen < len)
			len = olen;
	}
	if(addr != 0 && len > 0)
		memmove(addr, &r->raddr, len);

	return 0;
}
