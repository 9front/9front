#include <u.h>
#include <libc.h>
#include <ip.h>

int
str2addr(char *s, uchar *a)
{
	uchar *a0, ip[16];
	char *p;

	if((s = strchr(s, '!')) == nil)
		return 0;
	if((p = strchr(++s, '!')) == nil)
		return 0;
	if(strchr(++p, '!') != nil)
		return 0;
	if(parseip(ip, s) == -1)
		return 0;
	a0 = a;
	if(isv4(ip)){
		*a++ = 0x01;
		v6tov4(a, ip);
		a += 4;
	} else {
		*a++ = 0x04;
		memmove(a, ip, 16);
		a += 16;
	}
	hnputs(a, atoi(p));
	a += 2;
	return a - a0;
}

char*
addr2str(char *proto, uchar *a){
	static char s[128];
	uchar ip[16];
	int n, port;

	switch(*a++){
	default:
		abort();
		return 0;
	case 0x01:
		v4tov6(ip, a);
		port = nhgets(a+4);
		break;
	case 0x04:
		memmove(ip, a, 16);
		port = nhgets(a+16);
		break;
	case 0x03:
		n = *a++;
		port = nhgets(a+n);
		snprint(s, sizeof(s), "%s!%.*s!%d", proto, n, (char*)a, port);
		return s;
	}
	snprint(s, sizeof(s), "%s!%I!%d", proto, ip, port);
	return s;
}

int
sockerr(void)
{
	return 1;	/* general error */
}

void
main(int argc, char *argv[])
{
	uchar buf[8*1024];
	NetConnInfo *nc;
	int fd, cfd, v, n;

	fmtinstall('I', eipfmt);

	ARGBEGIN {
	} ARGEND;

	nc = nil;
	fd = cfd = -1;

	/* negotiation */
	if(readn(0, buf, 2) != 2)
		return;
	v = buf[0];
	n = buf[1];
	if(n > 0)
		if(readn(0, buf, n) != n)
			return;
	if(v > 5)
		v = 5;
	buf[0] = v;
	buf[1] = 0x00;	/* no authentication required */
	if(write(1, buf, 2) != 2)
		return;
Loop:
	/* request */
	if(readn(0, buf, 4) != 4)
		return;
	switch(buf[3]){
	default:
		return;
	case 0x01:	/* ipv4 */
		if(readn(0, buf+4, 4+2) != 4+2)
			return;
		break;
	case 0x03:	/* domain name */
		if(readn(0, buf+4, 1) != 1)
			return;
		if((n = buf[4]) == 0)
			return;
		if(readn(0, buf+5, n+2) != n+2)
			return;
		break;
	case 0x04:	/* ipv6 */
		if(readn(0, buf+4, 16+2) != 16+2)
			return;
		break;
	}

	/* cmd */
	switch(buf[1]){
	case 0x01:	/* CONNECT */
		fd = dial(addr2str("tcp", buf+3), 0, 0, &cfd);
		break;
	}
	if(fd >= 0){
		if((nc = getnetconninfo(nil, fd)) == nil){
			if(cfd >= 0)
				close(cfd);
			close(fd);
			fd = cfd = -1;
		}
	}

	/* response */
	buf[0] = v;
	buf[1] = (fd < 0) ? sockerr() : 0;
	buf[2] = 0x00;				/* res */
	if(fd < 0){
		buf[3] = 0x01;			/* atype */
		memset(buf+4, 0, 4+2);
		if(write(1, buf, 4+4+2) != 4+4+2)
			return;
		goto Loop;
	}
	if((n = str2addr(nc->laddr, buf+3)) <= 2)
		return;
	if(write(1, buf, 3+n) != 3+n)
		return;

	/* rely data */
	switch(rfork(RFMEM|RFPROC|RFFDG|RFNOWAIT)){
	case -1:
		return;
	case 0:
		dup(fd, 0);
		break;
	default:
		dup(fd, 1);
	}
	close(fd);
	while((n = read(0, buf, sizeof(buf))) > 0)
		if(write(1, buf, n) != n)
			break;
	if(cfd >= 0)
		hangup(cfd);
	exits(0);
}

