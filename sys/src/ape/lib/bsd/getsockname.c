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
getsockname(int fd, struct sockaddr *addr, int *alen)
{
	Rock *r;
	int len, olen;
	struct sockaddr_un *lunix;

	r = _sock_findrock(fd, 0);
	if(r == 0){
		errno = ENOTSOCK;
		return -1;
	}

	len = 0;
	switch(r->domain){
	case PF_INET:
	case PF_INET6:
		_sock_ingetaddr(r, &r->addr, &len, "local");
		break;
	case PF_UNIX:
		lunix = (struct sockaddr_un*)&r->addr;
		len = &lunix->sun_path[strlen(lunix->sun_path)] - (char*)lunix;
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
		memmove(addr, &r->addr, len);

	return 0;
}
