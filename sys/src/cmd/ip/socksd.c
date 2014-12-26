#include <u.h>
#include <libc.h>
#include <ip.h>

int socksver;
char inside[128];
char outside[128];

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
udprelay(int fd, char *dir)
{
	struct {
		Udphdr;
		uchar data[8*1024];
	} msg;
	char addr[128], ldir[40];
	int r, n, rfd, cfd;
	uchar *p;

	snprint(addr, sizeof(addr), "%s/udp!*!0", outside);
	if((cfd = announce(addr, ldir)) < 0)
		return -1;
	if(write(cfd, "headers", 7) != 7)
		return -1;
	strcat(ldir, "/data");
	if((rfd = open(ldir, ORDWR)) < 0)
		return -1;
	close(cfd);
	
	if((r = rfork(RFMEM|RFPROC|RFNOWAIT)) <= 0)
		return r;

	if((cfd = listen(dir, ldir)) < 0)
		return -1;
	close(fd);	/* close inside udp server */
	if((fd = accept(cfd, ldir)) < 0)
		return -1;

	switch(rfork(RFMEM|RFPROC|RFNOWAIT)){
	case -1:
		return -1;
	case 0:
		while((r = read(fd, msg.data, sizeof(msg.data))) > 0){
			if(r < 4)
				continue;
			p = msg.data;
			if(p[0] | p[1] | p[2])
				continue;
			p += 3;
			switch(*p++){
			default:
				continue;
			case 0x01:
				r -= 2+1+1+4+2;
				if(r < 0)
					continue;
				v4tov6(msg.raddr, p);
				p += 4;
				break;
			case 0x04:
				r -= 2+1+1+16+2;
				if(r < 0)
					continue;
				memmove(msg.raddr, p, 16);
				p += 16;
				break;
			}
			memmove(msg.rport, p, 2);
			p += 2;
			memmove(msg.data, p, r);
			write(rfd, &msg, sizeof(Udphdr)+r);
		}
		break;
	default:
		while((r = read(rfd, &msg, sizeof(msg))) > 0){
			r -= sizeof(Udphdr);
			if(r < 0)
				continue;
			p = msg.data;
			if(isv4(msg.raddr))
				n = 2+1+1+4+2;
			else
				n = 2+1+1+16+2;
			if(r+n > sizeof(msg.data))
				r = sizeof(msg.data)-n;
			memmove(p+n, p, r);
			*p++ = 0;
			*p++ = 0;
			*p++ = 0;
			if(isv4(msg.raddr)){
				*p++ = 0x01;
				v6tov4(p, msg.raddr);
				p += 4;
			} else {
				*p++ = 0x04;
				memmove(p, msg.raddr, 16);
				p += 16;
			}
			memmove(p, msg.rport, 2);
			r += n;
			write(fd, msg.data, r);
		}
	}
	return -1;
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
	char addr[128], dir[40], ldir[40], *s;
	int cmd, fd, cfd, n;
	NetConnInfo *nc;

	fmtinstall('I', eipfmt);

	setnetmtpt(inside, sizeof(inside), 0);
	setnetmtpt(outside, sizeof(outside), 0);
	ARGBEGIN {
	case 'x':
		setnetmtpt(inside, sizeof(inside), ARGF());
		break;
	case 'o':
		setnetmtpt(outside, sizeof(outside), ARGF());
		break;
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

	dir[0] = 0;
	fd = cfd = -1;
	cmd = buf[1];
	switch(cmd){
	case 0x01:	/* CONNECT */
		snprint(addr, sizeof(addr), "%s/tcp", outside);
		if((s = addr2str(addr, buf)) == nil)
			break;
		alarm(30000);
		fd = dial(s, 0, dir, &cfd);
		alarm(0);
		break;
	case 0x02:	/* BIND */
		if(myipaddr(buf, outside) < 0)
			break;
		snprint(addr, sizeof(addr), "%s/tcp!%I!0", outside, buf);
		fd = announce(addr, dir);
		break;
	case 0x03:	/* UDP */
		if(myipaddr(buf, inside) < 0)
			break;
		snprint(addr, sizeof(addr), "%s/udp!%I!0", inside, buf);
		fd = announce(addr, dir);
		break;
	}

Reply:
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
	if((nc = getnetconninfo(dir, cfd)) == nil)
		return;
	if((n = str2addr((cmd & 0x100) ? nc->raddr : nc->laddr, buf)) <= 0)
		return;
	if(write(1, buf, n) != n)
		return;

	switch(cmd){
	default:
		return;
	case 0x01:	/* CONNECT */
		break;
	case 0x02:	/* BIND */
		cfd = listen(dir, ldir);
		close(fd);
		fd = -1;
		if(cfd >= 0){
			strcpy(dir, ldir);
			fd = accept(cfd, dir);
		}
		cmd |= 0x100;
		goto Reply;
	case 0x102:
		break;		
	case 0x03:	/* UDP */
		if(udprelay(fd, dir) == 0)
			while(read(0, buf, sizeof(buf)) > 0)
				;
		goto Hangup;
	}
	
	/* relay data */
	switch(rfork(RFMEM|RFPROC|RFFDG|RFNOWAIT)){
	case -1:
		return;
	case 0:
		dup(fd, 0);
		break;
	default:
		dup(fd, 1);
	}
	while((n = read(0, buf, sizeof(buf))) > 0)
		if(write(1, buf, n) != n)
			break;
Hangup:
	if(cfd >= 0)
		hangup(cfd);
	postnote(PNGROUP, getpid(), "kill");
}

