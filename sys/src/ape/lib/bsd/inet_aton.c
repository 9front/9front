/* posix */
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define CLASS(x)	(x[0]>>6)

int
inet_aton(char *from, struct in_addr *in)
{
	unsigned char *to;
	unsigned long x;
	char *p;
	int i;

	in->s_addr = 0;
	to = (unsigned char*)&in->s_addr;
	if(*from == 0)
		return 0;
	for(i = 0; i < 4 && *from; i++, from = p){
		x = strtoul(from, &p, 0);
		if(x != (unsigned char)x || p == from)
			return 0;	/* parse error */
		to[i] = x;
		if(*p == '.')
			p++;
		else if(*p != 0)
			return 0;	/* parse error */
	}

	switch(CLASS(to)){
	case 0:	/* class A - 1 byte net */
	case 1:
		if(i == 3){
			to[3] = to[2];
			to[2] = to[1];
			to[1] = 0;
		} else if (i == 2){
			to[3] = to[1];
			to[1] = 0;
		}
		break;
	case 2:	/* class B - 2 byte net */
		if(i == 3){
			to[3] = to[2];
			to[2] = 0;
		}
		break;
	}
	return 1;
}
