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

/* for malloc/free */
#include <stdlib.h>

void
freeaddrinfo(struct addrinfo *res)
{
	struct addrinfo *info;

	while((info = res) != 0){
		res = res->ai_next;
		free(info->ai_canonname);
		free(info->ai_addr);
		free(info);
	}
}

static int
sockfamily(char *addr)
{
	if(strchr(addr, ':') != 0)
		return AF_INET6;
	else
		return AF_INET;
}

static int
sockproto(char *proto)
{
	if(strcmp(proto, "tcp") == 0)
		return SOCK_STREAM;
	if(strcmp(proto, "udp") == 0)
		return SOCK_DGRAM;
	if(strcmp(proto, "il") == 0)
		return SOCK_RDM;
	return 0;
}

static int
filladdrinfo(char *s, struct addrinfo *a, struct addrinfo *h)
{
	struct sockaddr sa;
	char *p, *q;

	if(*s != '/')
		return 1;
	if((q = strchr(s, ' ')) == 0)
		return 1;
	while(*q == ' ')
		*q++ = 0;
	if((p = strrchr(s+1, '/')) == 0)
		return 1;
	*p = 0;
	if((p = strrchr(s+1, '/')) == 0)
		return 1;
	*p++ = 0;
	if(*p == 0)
		return 1;

	if((a->ai_socktype = sockproto(p)) == 0)
		return 1;
	if((p = strchr(q, '!')) != 0){
		*p++ = 0;
		a->ai_family = sockfamily(q);
	} else{
		p = q;
		q = 0;
		if((a->ai_family = h->ai_family) == 0)
			a->ai_family = AF_INET;
	}
	if((a->ai_socktype == SOCK_RDM || h->ai_socktype != 0)
	&& (a->ai_socktype != h->ai_socktype))
		return 1;
	if(h->ai_family != 0 && a->ai_family != h->ai_family)
		return 1;
	if(_sock_inaddr(a->ai_family, q, p, &sa, &a->ai_addrlen) <= 0)
		return 1;
	if((a->ai_addr = malloc(a->ai_addrlen)) == 0)
		return EAI_MEMORY;
	memmove(a->ai_addr, &sa, a->ai_addrlen);
	return 0;
}

int
getaddrinfo(char *node, char *serv, struct addrinfo *hints, struct addrinfo **res)
{
	static struct addrinfo nohints;
	struct addrinfo *a, *head, **tail;
	char buf[1024], *proto;
	int n, fd, err;

	if(res != 0)
		*res = 0;

	if(hints == 0)
		hints = &nohints;

	proto = "net";
	switch(hints->ai_family){
	default:
		return EAI_FAMILY;
	case AF_INET:
	case AF_INET6:
		switch(hints->ai_socktype){
		default:
			return EAI_SOCKTYPE;
		case SOCK_STREAM:
			proto = "tcp";
			break;
		case SOCK_DGRAM:
			proto = "udp";
			break;
		case SOCK_RDM:
			proto = "il";
			break;
		case 0:
			break;
		}
		break;
	case AF_UNSPEC:
		break;
	}
	if(serv == 0){
		if(node == 0)
			return EAI_NONAME;
		serv = "0";
	}
	if(node == 0){
		if(hints->ai_flags & AI_PASSIVE)
			node = "*";
		else if(hints->ai_family == AF_INET6)
			node = "::1";
		else
			node = "127.0.0.1";
	}

	if((fd = open("/net/cs", O_RDWR)) < 0)
		return EAI_SYSTEM;

	snprintf(buf, sizeof(buf), "%s!%s!%s", proto, node, serv);
	n = strlen(buf);
	if(write(fd, buf, n) != n){
		close(fd);
		return EAI_AGAIN;
	}
	lseek(fd, 0, 0);

	head = 0;
	tail = &head;
	for(;;){
		if((n = read(fd, buf, sizeof(buf)-1)) <= 0)
			break;
		buf[n] = '\0';
		if((a = malloc(sizeof(*a))) == 0){
			freeaddrinfo(head);
			close(fd);
			return EAI_MEMORY;
		}
		memset(a, 0, sizeof(*a));
		if((err = filladdrinfo(buf, a, hints)) != 0){
			freeaddrinfo(a);
			if(err < 0){
				freeaddrinfo(head);
				close(fd);
				return err;
			}
		} else {
			*tail = a;
			tail = &a->ai_next;
		}
	}
	close(fd);

	if(head == 0)
		return EAI_NODATA;

	if((hints->ai_flags & AI_CANONNAME) != 0 && (hints->ai_flags & AI_NUMERICHOST) == 0){
		n = _sock_ipattr(node);
		if(n != Tsys && n != Tdom){
			if(getnameinfo(head->ai_addr, head->ai_addrlen, buf, sizeof(buf), 0, 0, NI_NAMEREQD) == 0)
				node = buf;
			else
				node = 0;
		}
		if(node != 0){
			n = strlen(node)+1;
			if((head->ai_canonname = malloc(n)) == 0){
				freeaddrinfo(head);
				return EAI_MEMORY;
			}
			memmove(head->ai_canonname, node, n);
		}
	}

	if(res != 0)
		*res = head;
	else
		freeaddrinfo(head);

	return 0;
}
