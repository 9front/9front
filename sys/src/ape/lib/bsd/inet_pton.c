/* posix */
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <errno.h>

static int
ipcharok(int c)
{
	return c == ':' || isascii(c) && isxdigit(c);
}

static int
delimchar(int c)
{
	if(c == '\0')
		return 1;
	if(c == ':' || isascii(c) && isalnum(c))
		return 0;
	return 1;
}

int
inet_pton(int af, char *src, void *dst)
{
	int i, elipsis = 0;
	unsigned char *to;
	unsigned long x;
	char *p, *op;

	if(af == AF_INET)
		return inet_aton(src, (struct in_addr*)dst);

	if(af != AF_INET6){
		errno = EAFNOSUPPORT;
		return -1;
	}

	to = ((struct in6_addr*)dst)->s6_addr;
	memset(to, 0, 16);

	p = src;
	for(i = 0; i < 16 && ipcharok(*p); i+=2){
		op = p;
		x = strtoul(p, &p, 16);

		if(x != (unsigned short)x || *p != ':' && !delimchar(*p))
			return 0;			/* parse error */

		to[i] = x>>8;
		to[i+1] = x;
		if(*p == ':'){
			if(*++p == ':'){	/* :: is elided zero short(s) */
				if (elipsis)
					return 0;	/* second :: */
				elipsis = i+2;
				p++;
			}
		} else if (p == op)		/* strtoul made no progress? */
			break;
	}
	if (p == src || !delimchar(*p))
		return 0;				/* parse error */
	if(i < 16){
		memmove(&to[elipsis+16-i], &to[elipsis], i-elipsis);
		memset(&to[elipsis], 0, 16-i);
	}
	return 1;
}
