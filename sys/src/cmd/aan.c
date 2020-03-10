#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>
#include <thread.h>

#define NS(x)	((vlong)x)
#define US(x)	(NS(x) * 1000LL)
#define MS(x)	(US(x) * 1000LL)
#define S(x)	(MS(x) * 1000LL)

enum {
	Synctime = S(8),
	Nbuf = 10,
	K = 1024,
	Bufsize = 8 * K,
	Stacksize = 8 * K,
	Timer = 0,				// Alt channels.
	Unsent = 1,
	Maxto = 24 * 3600,			// A full day to reconnect.
	Hdrsz = 3*4,
};

typedef struct {
	uchar	nb[4];		// Number of data bytes in this message
	uchar	msg[4];		// Message number
	uchar	acked[4];	// Number of messages acked
} Hdr;

typedef struct {
	Hdr	hdr;
	uchar	buf[Bufsize];
} Buf;

static Channel	*unsent;
static Channel	*unacked;
static Channel	*empty;
static int	netfd;
static ulong	inmsg;
static ulong	outmsg;
static char	*devdir;
static int	debug;
static int	done;
static char	*dialstring;
static int	maxto = Maxto;
static char	*Logname = "aan";
static int	client;
static int	reader = -1;
static int	lostsync;

static Alt a[] = {
	/*	c	v	 op   */
	{ 	nil,	nil,	CHANRCV			},	// timer
	{	nil,	nil,	CHANRCV			},	// unsent
	{ 	nil,	nil,	CHANEND		},
};

static void		fromnet(void*);
static void		fromclient(void*);
static int		reconnect(int);
static void		synchronize(void);
static int		writen(int, uchar *, int);
static void		timerproc(void *);

static void
usage(void)
{
	fprint(2, "usage: %s [-cd] [-m maxto] dialstring|netdir\n", argv0);
	exits("usage");
}

 
static int
catch(void *, char *s)
{
	if (!strcmp(s, "alarm")) {
		syslog(0, Logname, "Timed out while waiting for reconnect, exiting...");
		threadexitsall(nil);
	}
	return 0;
}

static void*
emalloc(int n)
{
	uintptr pc;
	void *v;

	pc = getcallerpc(&n);
	v = malloc(n);
	if(v == nil)
		sysfatal("Cannot allocate memory; pc=%#p", pc);
	setmalloctag(v, pc);
	return v;
}

void
threadmain(int argc, char **argv)
{
	vlong synctime;
	int i, n, failed;
	Channel *timer;
	Hdr hdr;
	Buf *b;

	ARGBEGIN {
	case 'c':
		client++;
		break;
	case 'd':
		debug++;
		break;
	case 'm':
		maxto = (int)strtol(EARGF(usage()), nil, 0);
		break;
	default:
		usage();
	} ARGEND;

	if (argc != 1)
		usage();

	if (!client) {
		char *p;

		devdir = argv[0];
		if ((p = strstr(devdir, "/local")) != nil)
			*p = '\0';
	}
	else
		dialstring = argv[0];

	if (debug > 0) {
		int fd = open("#c/cons", OWRITE|OCEXEC);	
		dup(fd, 2);
	}

	atnotify(catch, 1);

	/*
	 * Set up initial connection. use short timeout
	 * of 60 seconds so we wont hang arround for too
	 * long if there is some general connection problem
	 * (like NAT).
	 */
	netfd = reconnect(60);

	unsent = chancreate(sizeof(Buf *), Nbuf);
	unacked = chancreate(sizeof(Buf *), Nbuf);
	empty = chancreate(sizeof(Buf *), Nbuf);
	timer = chancreate(sizeof(uchar *), 1);
	if(unsent == nil || unacked == nil || empty == nil || timer == nil)
		sysfatal("Cannot allocate channels");

	for (i = 0; i < Nbuf; i++)
		sendp(empty, emalloc(sizeof(Buf)));

	reader = proccreate(fromnet, nil, Stacksize);
	if (reader < 0)
		sysfatal("Cannot start fromnet; %r");

	if (proccreate(fromclient, nil, Stacksize) < 0)
		sysfatal("Cannot start fromclient; %r");

	if (proccreate(timerproc, timer, Stacksize) < 0)
		sysfatal("Cannot start timerproc; %r");

	a[Timer].c = timer;
	a[Unsent].c = unsent;
	a[Unsent].v = &b;

Restart:
	synctime = nsec() + Synctime;
	failed = 0;
	lostsync = 0;
	while (!done) {
		if (netfd < 0 || failed) {
			// Wait for the netreader to die.
			while (netfd >= 0) {
				if(debug) fprint(2, "main; waiting for netreader to die\n");
				threadint(reader);
				sleep(1000);
			}

			// the reader died; reestablish the world.
			netfd = reconnect(maxto);
			synchronize();
			goto Restart;
		}

		switch (alt(a)) {
		case Timer:
			if (netfd < 0 || nsec() < synctime)
				break;

			PBIT32(hdr.nb, 0);
			PBIT32(hdr.acked, inmsg);
			PBIT32(hdr.msg, -1);

			if (writen(netfd, (uchar *)&hdr, Hdrsz) < 0) {
				failed = 1;
				continue;
			}

			if(++lostsync > 2){
				syslog(0, Logname, "connection seems hung up...");
				failed = 1;
				continue;
			}
			synctime = nsec() + Synctime;
			break;

		case Unsent:
			sendp(unacked, b);

			if (netfd < 0)
				break;

			PBIT32(b->hdr.acked, inmsg);

			if (writen(netfd, (uchar *)&b->hdr, Hdrsz) < 0)
				failed = 1;
			else {
				n = GBIT32(b->hdr.nb);
				if (writen(netfd, b->buf, n) < 0)
					failed = 1;
				if (n == 0)
					done = 1;
			}
			break;
		}
	}
	syslog(0, Logname, "exiting...");
	threadexitsall(nil);
}


static void
fromclient(void*)
{
	int n;
	Buf *b;

	threadsetname("fromclient");

	do {
		b = recvp(empty);
		n = read(0, b->buf, Bufsize);
		if (n < 0)
			n = 0;
		PBIT32(b->hdr.nb, n);
		PBIT32(b->hdr.msg, outmsg);
		sendp(unsent, b);
		outmsg++;
	} while(n > 0);
}

static void
fromnet(void*)
{
	extern void _threadnote(void *, char *);
	ulong m, acked, lastacked = 0;
	int n, len;
	Buf *b;

	notify(_threadnote);

	threadsetname("fromnet");

	b = emalloc(sizeof(Buf));
	while (!done) {
		while (netfd < 0) {
			if(done)
				return;
			if(debug) fprint(2, "fromnet; waiting for connection... (inmsg %lud)\n", inmsg);
			sleep(1000);
		}

		// Read the header.
		len = readn(netfd, (uchar *)&b->hdr, Hdrsz);
		if (len <= 0) {
			if (debug) {
				if (len < 0)
					fprint(2, "fromnet; (hdr) network failure; %r\n");
				else
					fprint(2, "fromnet; (hdr) network closed\n");
			}
			close(netfd);
			netfd = -1;
			continue;
		}
		lostsync = 0;	// reset timeout
		n = GBIT32(b->hdr.nb);
		m = GBIT32(b->hdr.msg);
		acked = GBIT32(b->hdr.acked);
		if (n == 0) {
			if (m == (ulong)-1)
				continue;
			if(debug) fprint(2, "fromnet; network closed\n");
			break;
		} else if (n < 0 || n > Bufsize) {
			if(debug) fprint(2, "fromnet; message too big %d > %d\n", n, Bufsize);
			break;
		}

		len = readn(netfd, b->buf, n);
		if (len <= 0 || len != n) {
			if (len == 0)
				if(debug) fprint(2, "fromnet; network closed\n");
			else
				if(debug) fprint(2, "fromnet; network failure; %r\n");
			close(netfd);
			netfd = -1;
			continue;
		}

		if (m != inmsg) {
			if(debug) fprint(2, "fromnet; skipping message %lud, currently at %lud\n", m, inmsg);
			continue;
		}			
		inmsg++;

		// Process the acked list.
		while((long)(acked - lastacked) > 0) {
			Buf *rb;

			if((rb = recvp(unacked)) == nil)
				break;
			m = GBIT32(rb->hdr.msg);
			if (m != lastacked) {
				if(debug) fprint(2, "fromnet; rb %p, msg %lud, lastacked %lud\n", rb, m, lastacked);
				sysfatal("fromnet; bug");
			}
			PBIT32(rb->hdr.msg, -1);
			sendp(empty, rb);
			lastacked++;
		} 

		if (writen(1, b->buf, len) < 0) 
			sysfatal("fromnet; cannot write to client; %r");
	}
	done = 1;
}

static int
reconnect(int secs)
{
	NetConnInfo *nci;
	char ldir[40];
	int lcfd, fd;

	if (dialstring) {
		syslog(0, Logname, "dialing %s", dialstring);
		alarm(secs*1000);
  		while ((fd = dial(dialstring, nil, ldir, nil)) < 0) {
			char err[32];

			err[0] = '\0';
			errstr(err, sizeof err);
			if (strstr(err, "connection refused")) {
				if(debug) fprint(2, "reconnect; server died...\n");
				threadexitsall("server died...");
			}
			if(debug) fprint(2, "reconnect: dialed %s; %s\n", dialstring, err);
			sleep(1000);
		}
		alarm(0);
		syslog(0, Logname, "reconnected to %s", dialstring);
	} 
	else {
		syslog(0, Logname, "waiting for connection on %s", devdir);
		alarm(secs*1000);
 		if ((lcfd = listen(devdir, ldir)) < 0) 
			sysfatal("reconnect; cannot listen; %r");
		if ((fd = accept(lcfd, ldir)) < 0)
			sysfatal("reconnect; cannot accept; %r");
		alarm(0);
		close(lcfd);
	}

	if(nci = getnetconninfo(ldir, fd)){
		syslog(0, Logname, "connected from %s", nci->rsys);
		threadsetname(client? "client %s %s" : "server %s %s", ldir, nci->rsys);
		freenetconninfo(nci);
	} else
		syslog(0, Logname, "connected");

	return fd;
}

static void
synchronize(void)
{
	Channel *tmp;
	Buf *b;
	int n;

	// Ignore network errors here.  If we fail during 
	// synchronization, the next alarm will pick up 
	// the error.

	tmp = chancreate(sizeof(Buf *), Nbuf);
	while ((b = nbrecvp(unacked)) != nil) {
		n = GBIT32(b->hdr.nb);
		writen(netfd, (uchar *)&b->hdr, Hdrsz);
		writen(netfd, b->buf, n);
		sendp(tmp, b);
	}
	chanfree(unacked);
	unacked = tmp;
}

static int
writen(int fd, uchar *buf, int nb)
{
	int len = nb;

	while (nb > 0) {
		int n;

		if (fd < 0) 
			return -1;

		if ((n = write(fd, buf, nb)) < 0) {
			if(debug) fprint(2, "writen; Write failed; %r\n");
			return -1;
		}

		buf += n;
		nb -= n;
	}
	return len;
}

static void
timerproc(void *x)
{
	Channel *timer = x;

	threadsetname("timer");

	while (!done) {
		sleep((Synctime / MS(1)) >> 1);
		sendp(timer, "timer");
	}
}
