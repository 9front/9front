/* posix */
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>

#include "priv.h"

void*
_sock_inip(struct sockaddr *a)
{
	switch(a->sa_family){
	case AF_INET:
		return &((struct sockaddr_in*)a)->sin_addr;
	case AF_INET6:
		return &((struct sockaddr_in6*)a)->sin6_addr;
	}
	return 0;
}

int
_sock_inport(struct sockaddr *a)
{
	switch(a->sa_family){
	case AF_INET:
		return ntohs(((struct sockaddr_in*)a)->sin_port);
	case AF_INET6:
		return ntohs(((struct sockaddr_in6*)a)->sin6_port);
	}
	return 0;
}

int
_sock_inaddr(int af, char *ip, char *port, void *a, int *alen)
{
	int len;

	len = 0;
	if(af == AF_INET){
		struct sockaddr_in *in = a;

		len = sizeof(*in);
		memset(in, 0, len);
		in->sin_family = af;
		if(port != 0 && *port != 0)
			in->sin_port = htons(atoi(port));
		if(ip != 0 && *ip != 0)
			inet_pton(af, ip, &in->sin_addr);
	} else if(af == AF_INET6){
		struct sockaddr_in6 *in = a;

		len = sizeof(*in);
		memset(in, 0, len);
		in->sin6_family = af;
		if(port != 0 && *port != 0)
			in->sin6_port = htons(atoi(port));
		if(ip != 0 && *ip != 0)
			inet_pton(af, ip, &in->sin6_addr);
	}
	if(alen != 0)
		*alen = len;
	return len;
}

void
_sock_ingetaddr(Rock *r, void *a, int *alen, char *file)
{
	char name[Ctlsize], *p;
	int n, fd;

	if(r->domain != PF_INET && r->domain != PF_INET6)
		return;
	/* get remote address */
	strcpy(name, r->ctl);
	p = strrchr(name, '/');
	strcpy(p+1, file);
	fd = open(name, O_RDONLY);
	if(fd >= 0){
		n = read(fd, name, sizeof(name)-1);
		if(n > 0){
			name[n] = 0;
			p = strchr(name, '!');
			if(p){
				*p++ = 0;
				_sock_inaddr(r->domain, name, p, a, alen);
			}
		}
		close(fd);
	}
}
