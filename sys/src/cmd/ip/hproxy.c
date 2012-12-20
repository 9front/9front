#include <u.h>
#include <libc.h>

enum { bufsize = 8*1024 };
char buf[bufsize+1], addr[128], *proto, *host, *port, *path;

void
main(void)
{
	char *f[3], *p, *e;
	int con, fd, n, r;

	/* read all the headers */
	n = 0;
	do {
		if(n >= bufsize)
			return;
		if((r = read(0, buf+n, bufsize-n)) <= 0)
			return;
		n += r;
		buf[n] = 0;
	} while(strstr(buf, "\r\n\r\n") == nil);

	/* remove keep alive headers */
	if(p = cistrstr(buf, "\nConnection:"))
		if(e = strchr(p+1, '\n'))
			strcpy(p, e);
	if(p = cistrstr(buf, "\nProxy-Connection:"))
		if(e = strchr(p+1, '\n'))
			strcpy(p, e);

	/* crack first line of http request */
	if(e = strchr(buf, '\n'))
		*e++ = 0;
	r = tokenize(buf, f, 3);
	if(r < 2)
		return;
	if(r == 2)
		f[2] = "HTTP/1.0";
	proto = f[1];
	if(p = strstr(proto, "://")){
		*p = 0;
		host = p + 3;
	} else {
		host = proto;
		proto = "http";
	}
	port = proto;
	path = "";
	if(p = strchr(host, '/')){
		*p++ = 0;
		path = p;
	}
	if(*host == '['){
		host++;
		if(p = strrchr(host, ']')){
			*p++ = 0;
			if(p = strrchr(p, ':'))
				port = ++p;
		}
	} else if(p = strrchr(host, ':')){
		*p++ = 0;
		port = p;
	}

	snprint(addr, sizeof(addr), "tcp!%s!%s", host, port);

	alarm(30000);
	fd = dial(addr, 0, 0, 0);
	alarm(0);

	con = cistrcmp(f[0], "CONNECT") == 0;
	if(con){
		if(fd < 0)
			print("%s 500 Connection Failed\r\n\r\n%r\n", f[2]);
		else
			print("%s 200 Connection Established\r\n\r\n", f[2]);
	}
	if(fd < 0)
		return;

	switch(rfork(RFPROC|RFFDG|RFNOWAIT)){
	case -1:
		return;
	case 0:
		dup(fd, 0);
		break;
	default:
		dup(fd, 1);
		if(!con)
			print("%s /%s %s\r\nConnection: close\r\n%s", f[0], path, f[2], e);
	}

	while((r = read(0, buf, sizeof(buf))) > 0)
		if(write(1, buf, r) != r)
			break;

	postnote(PNGROUP, getpid(), "kill");
}
