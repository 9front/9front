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
#include <netdb.h>

#include "priv.h"

int h_errno;

enum
{
	Nname= 6,
};

/*
 *  for inet addresses only
 */
struct hostent*
gethostbyname(char *name)
{
	int i, t, fd, m;
	char *p, *k, *bp;
	int nn, na;
	struct in_addr in;
	static struct hostent h;
	static char buf[1024];
	static char *nptr[Nname+1];
	static char *aptr[Nname+1];
	static char addr[Nname][4];

	h.h_name = 0;
	t = _sock_ipattr(name);

	/* connect to server */
	fd = open("/net/cs", O_RDWR);
	if(fd < 0){
		h_errno = NO_RECOVERY;
		return 0;
	}

	/* construct the query, always expect an ip# back */
	switch(t){
	case Tsys:
		snprintf(buf, sizeof buf, "!sys=%s ip=*", name);
		break;
	case Tdom:
		snprintf(buf, sizeof buf, "!dom=%s ip=*", name);
		break;
	case Tip:
		snprintf(buf, sizeof buf, "!ip=%s", name);
		break;
	}

	/* query the server */
	if(write(fd, buf, strlen(buf)) < 0){
		h_errno = TRY_AGAIN;
		close(fd);
		return 0;
	}
	lseek(fd, 0, 0);
	for(i = 0; i < sizeof(buf)-1; i += m){
		m = read(fd, buf+i, sizeof(buf) - 1 - i);
		if(m <= 0)
			break;
		buf[i+m++] = ' ';
	}
	close(fd);
	buf[i] = 0;

	/* parse the reply */
	nn = na = 0;
	for(bp = buf;;){
		k = bp;
		p = strchr(k, '=');
		if(p == 0)
			break;
		*p++ = 0;
		for(bp = p; *bp && *bp != ' '; bp++)
			;
		if(*bp)
			*bp++ = 0;
		if(strcmp(k, "dom") == 0){
			if(h.h_name == 0)
				h.h_name = p;
			if(nn < Nname)
				nptr[nn++] = p;
		} else if(strcmp(k, "sys") == 0){
			if(nn < Nname)
				nptr[nn++] = p;
		} else if(strcmp(k, "ip") == 0){
			if(inet_aton(p, &in) == 0)
				continue;
			if(na < Nname){
				memmove(addr[na], (unsigned char*)&in.s_addr, 4);
				aptr[na] = addr[na];
				na++;
			}
		}
	}
	if(nn+na == 0){
		h_errno = HOST_NOT_FOUND;
		return 0;
	}

	nptr[nn] = 0;
	aptr[na] = 0;
	h.h_aliases = nptr;
	h.h_addr_list = aptr;
	h.h_length = 4;
	h.h_addrtype = AF_INET;
	if(h.h_name == 0)
		h.h_name = nptr[0];
	if(h.h_name == 0)
		h.h_name = aptr[0];

	return &h;
}
