#include <u.h>
#include <libc.h>
#include <ip.h>

int socksver;

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
	if(socksver == 4){
		a += 2;
		hnputs(a, atoi(p));
		a += 2;
		v6tov4(a, ip);
		a += 4;
	} else {
		a += 3;
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
	}
	return a - a0;
}

char*
addr2str(char *proto, uchar *a){
	static char s[128];
	uchar ip[16];
	int n, port;

	if(socksver == 4){
		a += 2;
		port = nhgets(a);
		a += 2;
		if((a[0] | a[1] | a[2]) == 0 && a[3]){
			a += 4;
			a += strlen((char*)a)+1;
			snprint(s, sizeof(s), "%s!%s!%d", proto, (char*)a, port);
			return s;
		}
		v4tov6(ip, a);
	} else {
		a += 3;
		switch(*a++){
		default:
			return nil;
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
	}
	snprint(s, sizeof(s), "%s!%I!%d", proto, ip, port);
	return s;
}

int
sockerr(int err)
{
	/* general error */
	if(socksver == 4)
		return err ? 0x5b : 0x5a;
	else
		return err != 0;
}

void
main(int argc, char *argv[])
{
	uchar buf[8*1024], *p;
	char dir[40], *s;
	NetConnInfo *nc;
	int fd, cfd, n;

	fmtinstall('I', eipfmt);

	ARGBEGIN {
	} ARGEND;

	/* ver+cmd or ver+nmethod */
	if(readn(0, buf, 2) != 2)
		return;
	socksver = buf[0];
	if(socksver < 4)
		return;
	if(socksver > 5)
		socksver = 5;

	if(socksver == 4){
		/* port+ip4 */
		if(readn(0, buf+2, 2+4) != 2+4)
			return;
		/* +user\0 */
		for(p = buf+2+2+4;; p++){
			if(p >= buf+sizeof(buf))
				return;
			if(read(0, p, 1) != 1)
				return;
			if(*p == 0)
				break;
		}
		/* socks 4a dom hack */
		if((buf[4] | buf[5] | buf[6]) == 0 && buf[7]){
			/* +dom\0 */
			for(++p;; p++){
				if(p >= buf+sizeof(buf))
					return;
				if(read(0, p, 1) != 1)
					return;
				if(*p == 0)
					break;
			}
		}
	} else {
		/* nmethod */
		if((n = buf[1]) > 0)
			if(readn(0, buf+2, n) != n)
				return;

		/* ver+method */
		buf[0] = socksver;
		buf[1] = 0x00;	/* no authentication required */
		if(write(1, buf, 2) != 2)
			return;

		/* ver+cmd+res+atyp */
		if(readn(0, buf, 4) != 4)
			return;
		switch(buf[3]){
		default:
			return;
		case 0x01:	/* +ipv4 */
			if(readn(0, buf+4, 4+2) != 4+2)
				return;
			break;
		case 0x03:	/* +len+dom[len] */
			if(readn(0, buf+4, 1) != 1)
				return;
			if((n = buf[4]) == 0)
				return;
			if(readn(0, buf+5, n+2) != n+2)
				return;
			break;
		case 0x04:	/* +ipv6 */
			if(readn(0, buf+4, 16+2) != 16+2)
				return;
			break;
		}
	}

	nc = nil;
	dir[0] = 0;
	fd = cfd = -1;
	switch(buf[1]){
	case 0x01:	/* CONNECT */
		if((s = addr2str("tcp", buf)) == nil)
			return;
		fd = dial(s, 0, dir, &cfd);
		break;
	}

	if(fd >= 0){
		if((nc = getnetconninfo(dir, -1)) == nil){
			if(cfd >= 0)
				close(cfd);
			close(fd);
			fd = cfd = -1;
		}
	}

	/* reply */
	buf[1] = sockerr(fd < 0);			/* status */
	if(socksver == 4){
		buf[0] = 0x00;				/* vc */
		if(fd < 0){
			memset(buf+2, 0, 2+4);
			write(1, buf, 2+2+4);
			return;
		}
	} else {
		buf[0] = socksver;			/* ver */
		buf[2] = 0x00;				/* res */
		if(fd < 0){
			buf[3] = 0x01;			/* atyp */
			memset(buf+4, 0, 4+2);
			write(1, buf, 4+4+2);
			return;
		}
	}
	if((n = str2addr(nc->laddr, buf)) <= 0)
		return;
	if(write(1, buf, n) != n)
		return;

	/* reley data */
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

