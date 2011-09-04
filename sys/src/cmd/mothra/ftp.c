#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include <bio.h>
#include <ndb.h>
#include <ctype.h>
#include <ip.h>
#include "mothra.h"

enum
{
	/* return codes */
	Extra=		1,
	Success=	2,
	Incomplete=	3,
	TempFail=	4,
	PermFail=	5,

	NAMELEN=	28,
	Nnetdir=	3*NAMELEN,	/* max length of network directory paths */
	Ndialstr=	64,		/* max length of dial strings */
};

typedef struct Ftp Ftp;
struct Ftp
{
	char	net[Nnetdir];
	Biobuf	*ftpctl;
	Url	*u;
};

static int ftpdebug;


/*
 *  read from biobuf turning cr/nl into nl
 */
char*
getcrnl(Biobuf *b)
{
	char *p, *ep;

	p = Brdline(b, '\n');
	if(p == nil)
		return nil;
	ep = p + Blinelen(b) - 1;
	if(*(ep-1) == '\r')
		ep--;
	*ep = 0;
	return p;
}

char*
readfile(char *file, char *buf, int len)
{
	int n, fd;

	fd = open(file, OREAD);
	if(fd < 0)
		return nil;
	n = read(fd, buf, len-1);
	close(fd);
	if(n <= 0)
		return nil;
	buf[n] = 0;
	return buf;
}

char*
sysname(void)
{
	static char sys[Ndbvlen];
	char *p;

	p = readfile("/dev/sysname", sys, sizeof(sys));
	if(p == nil)
		return "unknown";
	return p;
}

char*
domainname(void)
{
	static char domain[Ndbvlen];
	Ndbtuple *t;

	if(*domain)
		return domain;

	t = csgetval(0, "sys", sysname(), "dom", domain);
	if(t){
		ndbfree(t);
		return domain;
	} else
		return sysname();
}

static int
sendrequest(Biobuf *b, char *fmt, ...)
{
	char buf[2*1024], *s;
	va_list args;

	va_start(args, fmt);
	s = buf + vsnprint(buf, (sizeof(buf)-4) / sizeof(*buf), fmt, args);
	va_end(args);
	*s++ = '\r';
	*s++ = '\n';
	if(write(Bfildes(b), buf, s - buf) != s - buf)
		return -1;
	if(ftpdebug)
		write(2, buf, s - buf);
	return 0;
}

static int
getreply(Biobuf *b, char *msg, int len)
{
	char *line;
	int rv;
	int i, n;

	while(line = getcrnl(b)){
		/* add line to message buffer, strip off \r */
		n = Blinelen(b);
		if(ftpdebug)
			write(2, line, n);
		if(n > len - 1)
			i = len - 1;
		else
			i = n;
		if(i > 0){
			memmove(msg, line, i);
			msg += i;
			len -= i;
			*msg = 0;
		}

		/* stop if not a continuation */
		rv = atoi(line);
		if(rv >= 100 && rv < 600 && (n == 4 || (n > 4 && line[3] == ' ')))
			return rv/100;
	}

	return -1;
}

int
terminateftp(Ftp *d)
{
	if(d->ftpctl){
		close(Bfildes(d->ftpctl));
		Bterm(d->ftpctl);
		free(d->ftpctl);
		d->ftpctl = nil;
	}
	free(d);
	return -1;
}

Biobuf*
hello(Ftp *d)
{
	int fd;
	char *p;
	Biobuf *b;
	char msg[1024];
	char ndir[Nnetdir];

	snprint(msg, sizeof msg, "tcp!%s!%d", d->u->ipaddr, d->u->port);
	fd = dial(msg, 0, ndir, 0);
	if(fd < 0){
		d->ftpctl = nil;
		return nil;
	}
	b = emalloc(sizeof(Biobuf));
	Binit(b, fd, OREAD);
	d->ftpctl = b;

	/* remember network for the data connections */
	p = strrchr(ndir, '/');
	if(p == 0){
		fprint(2, "dial is out of date\n");
		return nil;
	}
	*p = 0;
	strcpy(d->net, ndir);

	/* wait for hello from other side */
	if(getreply(b, msg, sizeof(msg)) != Success){
		fprint(2, "instead of hello: %s\n", msg);
		return nil;
	}
	return b;
}

int
logon(Ftp *d)
{
	char msg[1024];

	/* login anonymous */
	sendrequest(d->ftpctl, "USER anonymous");
	switch(getreply(d->ftpctl, msg, sizeof(msg))){
	case Success:
		return 0;
	case Incomplete:
		break;	/* need password */
	default:
		fprint(2, "login failed: %s\n", msg);
		werrstr(msg);
		return -1;
	}

	/* send user id as password */
	sprint(msg, "%s@", getuser());
	sendrequest(d->ftpctl, "PASS %s", msg);
	if(getreply(d->ftpctl, msg, sizeof(msg)) != Success){
		fprint(2, "login failed: %s\n", msg);
		werrstr(msg);
		return -1;
	}

	return 0;
}

int
xfertype(Ftp *d, char *t)
{
	char msg[1024];

	sendrequest(d->ftpctl, "TYPE %s", t);
	if(getreply(d->ftpctl, msg, sizeof(msg)) != Success){
		fprint(2, "can't set type %s: %s\n", t, msg);
		werrstr(msg);
		return -1;
	}
	return 0;
}

int
passive(Ftp *d)
{
	char msg[1024];
	char dialstr[Ndialstr];
	char *f[6];
	char *p;
	int fd;

	sendrequest(d->ftpctl, "PASV");
	if(getreply(d->ftpctl, msg, sizeof(msg)) != Success)
		return -1;

	/* get address and port number from reply, this is AI */
	p = strchr(msg, '(');
	if(p == nil){
		for(p = msg+3; *p; p++)
			if(isdigit(*p))
				break;
	} else
		p++;
	if(getfields(p, f, 6, 0, ",") < 6){
		fprint(2, "passive mode protocol botch: %s\n", msg);
		werrstr("ftp protocol botch");
		return -1;
	}
	snprint(dialstr, sizeof(dialstr), "%s!%s.%s.%s.%s!%d", d->net,
		f[0], f[1], f[2], f[3],
		((atoi(f[4])&0xff)<<8) + (atoi(f[5])&0xff));


	/* open data connection */
	fd = dial(dialstr, 0, 0, 0);
	if(fd < 0){
		fprint(2, "passive mode connect to %s failed: %r\n", dialstr);
		return -1;
	}

	/* tell remote to send a file */
	sendrequest(d->ftpctl, "RETR %s", d->u->reltext);
	if(getreply(d->ftpctl, msg, sizeof(msg)) != Extra){
		fprint(2, "passive mode retrieve failed: %s\n", msg);
		werrstr(msg);
		return -1;
	}
	return fd;
}

int
active(Ftp *d)
{
	char msg[1024];
	char buf[Ndialstr];
	char netdir[Nnetdir];
	char newdir[Nnetdir];
	uchar ipaddr[4];
	int dfd, cfd, listenfd;
	char *p;
	int port;

	/* get a channel to listen on, let kernel pick the port number */
	sprint(buf, "%s!*!0", d->net);
	listenfd = announce(buf, netdir);
	if(listenfd < 0){
		fprint(2, "can't listen for ftp callback: %r\n", buf);
		return -1;
	}

	/* get the local address and port number */
	sprint(newdir, "%s/local", netdir);
	readfile(newdir, buf, sizeof buf);
	p = strchr(buf, '!')+1;
	parseip(ipaddr, buf);
	port = atoi(p);

	/* tell remote side address and port*/
	sendrequest(d->ftpctl, "PORT %d,%d,%d,%d,%d,%d", ipaddr[0], ipaddr[1], ipaddr[2],
		ipaddr[3], port>>8, port&0xff);
	if(getreply(d->ftpctl, msg, sizeof(msg)) != Success){
		close(listenfd);
		werrstr("ftp protocol botch");
		fprint(2, "active mode connect failed %s\n", msg);
		return -1;
	}

	/* tell remote to send a file */
	sendrequest(d->ftpctl, "RETR %s", d->u->reltext);
	if(getreply(d->ftpctl, msg, sizeof(msg)) != Extra){
		close(listenfd);
		fprint(2, "active mode connect failed: %s\n", msg);
		werrstr(msg);
		return -1;
	}

	/* wait for a new call */
	cfd = listen(netdir, newdir);
	close(listenfd);
	if(cfd < 0){
		fprint(2, "active mode connect failed: %r\n");
		return -1;
	}

	/* open the data connection and close the control connection */
	dfd = accept(cfd, newdir);
	close(cfd);
	if(dfd < 0){
		fprint(2, "active mode connect failed: %r\n");
		werrstr("ftp protocol botch");
		return -1;
	}

	return dfd;
}

/*
 * Given a url, return a file descriptor on which caller can
 * read an ftp document.
 * The caller is responsible for processing redirection loops.
 */
int
ftp(Url *url)
{
	int n;
	int data;
	Ftp *d;
	int pfd[2];
	char buf[2048];

	if(url->type == 0)
		url->type = PLAIN;

	d = (Ftp*)emalloc(sizeof(Ftp));
	d->u = url;
	d->ftpctl = nil;

	if(hello(d) == nil)
		return terminateftp(d);
	if(logon(d) < 0)
		return terminateftp(d);

	switch(url->type){
	case PLAIN:
	case HTML:
		if(xfertype(d, "A") < 0)
			return terminateftp(d);
		break;
	default:
		if(xfertype(d, "I") < 0)
			return terminateftp(d);
		break;
	}

	/* first try passive mode, then active */
	data = passive(d);
	if(data < 0){
		if(d->ftpctl == nil)
			return -1;
		data = active(d);
		if(data < 0)
			return -1;
	}

	if(pipe(pfd) < 0)
		return -1;

	switch(rfork(RFFDG|RFPROC|RFNOWAIT)){
	case -1:
		werrstr("Can't fork");
		close(pfd[0]);
		close(pfd[1]);
		return terminateftp(d);
	case 0:
		close(pfd[0]);
		while((n=read(data, buf, sizeof(buf)))>0)
			write(pfd[1], buf, n);
		if(n<0)
			fprint(2, "ftp: %s: %r\n", url->fullname);
		_exits(0);
	default:
		close(pfd[1]);
		close(data);
		terminateftp(d);
		return pfd[0];
	}
	return -1;
}
