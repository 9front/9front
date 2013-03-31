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

static int
netquery(char *buf, int nbuf)
{
	int fd, i, n;

	if((fd = open("/net/cs", O_RDWR)) < 0)
		return EAI_SYSTEM;
	n = strlen(buf);
	if(write(fd, buf, n) != n){
		close(fd);
		return EAI_NONAME;
	}
	lseek(fd, 0, 0);
	for(i = 0; i < nbuf-1; i += n){
		n = read(fd, buf+i, nbuf - 1 - i);
		if(n <= 0)
			break;
		buf[i+n++] = ' ';
	}
	close(fd);
	buf[i] = 0;
	return i;
}

int
getnameinfo(struct sockaddr *sa, int salen,
	char *host, int hostlen,
	char *serv, int servlen,
	unsigned int flags)
{
	char buf[8*1024], *b, *p;
	int err;

	if(sa->sa_family != AF_INET && sa->sa_family != AF_INET6)
		return EAI_FAMILY;

	if(host != 0 && hostlen > 0){
		if(inet_ntop(sa->sa_family, _sock_inip(sa), host, hostlen) == 0)
			return EAI_SYSTEM;

		if((flags & NI_NUMERICHOST) == 0){
			snprintf(buf, sizeof(buf), "!ip=%s", host);
			if((err = netquery(buf, sizeof(buf))) < 0){
				if((flags & NI_NAMEREQD) != 0)
					return err;
			} else {
				char *sys, *dom;

				sys = dom = 0;
				for(b = buf;;){
					if((p = strchr(b, '=')) == 0)
						break;
					*p++ = 0;
					if(strcmp(b, "sys") == 0)
						sys = p;
					else if(strcmp(b, "dom") == 0)
						dom = p;
					while(*p && *p != ' ')
						p++;
					while(*p == ' ')
						*p++ = 0;
					b = p;
				}
				if(sys == 0){
					if(dom == 0 && (flags & NI_NAMEREQD) != 0)
						return EAI_NONAME;
					if(dom != 0 && (flags & NI_NOFQDN) != 0){
						if((p = strchr(dom, '.')) != 0)
							*p = 0;
					}
					sys = dom;
				}
				snprintf(host, hostlen, "%s", sys);
			}
		}
	}

	if(serv != 0 && servlen > 0){
		snprintf(serv, servlen, "%d", _sock_inport(sa));
		if((flags & NI_NUMERICSERV) == 0){
			snprintf(buf, sizeof(buf), "!port=%s", serv);
			if(netquery(buf, sizeof(buf)) > 0){
				char *tcp, *udp;

				tcp = udp = 0;
				for(b = buf;;){
					if((p = strchr(b, '=')) == 0)
						break;
					*p++ = 0;
					if(strcmp(b, "tcp") == 0)
						tcp = p;
					else if(strcmp(b, "udp") == 0)
						udp = p;
					while(*p && *p != ' ')
						p++;
					while(*p == ' ')
						*p++ = 0;
					b = p;
				}
				if(udp != 0 && (flags & NI_DGRAM) != 0)
					snprintf(serv, servlen, "%s", udp);
				else if(tcp != 0)
					snprintf(serv, servlen, "%s", tcp);
			}
		}
	}

	return 0;
}
