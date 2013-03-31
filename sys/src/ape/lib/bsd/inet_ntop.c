/* posix */
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <errno.h>

char*
inet_ntop(int af, void *src, char *dst, int size)
{
	unsigned char *p;
	char *t, *e;
	int i;

	if(af == AF_INET){
		if(size < INET_ADDRSTRLEN){
			errno = ENOSPC;
			return 0;
		}
		p = (unsigned char*)&(((struct in_addr*)src)->s_addr);
		snprintf(dst, size, "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
		return dst;
	}

	if(af != AF_INET6){
		errno = EAFNOSUPPORT;
		return 0;
	}
	if(size < INET6_ADDRSTRLEN){
		errno = ENOSPC;
		return 0;
	}

	p = (unsigned char*)((struct in6_addr*)src)->s6_addr;
	t = dst;
	e = t + size;
	for(i=0; i<16; i += 2){
		unsigned int w;

		if(i > 0)
			*t++ = ':';
		w = p[i]<<8 | p[i+1];
		snprintf(t, e - t, "%x", w);
		t += strlen(t);
	}
	return dst;
}
