/* intel amt serial-over-lan console */

#include <u.h>
#include <libc.h>
#include <bio.h>
#include <libsec.h>
#include <auth.h>

enum {
	MAX_TRANSMIT_BUFFER = 15000,
	TRANSMIT_BUFFER_TIMEOUT = 100,
	TRANSMIT_OVERFLOW_TIMEOUT = 0,
	HOST_SESSION_RX_TIMEOUT = 60000,
	HOST_FIFO_RX_FLUSH_TIMEOUT = 0,
	HEATBEAT_INTERVAL = 5000,
};

char *user = "admin";
int authok = 0, pid = 0, raw = -1, kvm = 0, fd = -1, tls = 1;
int reply, ok, n;
char buf[MAX_TRANSMIT_BUFFER];
Biobuf bin, bout;
jmp_buf reconnect;

void
killpid(void)
{
	if(pid != 0){
		postnote(PNPROC, pid, "kill");
		pid = 0;
	}
}

void
eof(void)
{
	if(!authok) sysfatal("eof: %r");
	killpid();
	longjmp(reconnect, 1);
}

void
recv(char *fmt, ...)
{
	uchar buf[4];
	va_list a;
	void *s;
	int n;

	va_start(a, fmt);
	for(;;){
		switch(*fmt++){
		case '\0':
			va_end(a);
			return;
		case '*':
			Bflush(&bin);
			break;
		case '_':
			if(Bread(&bin, buf, 1) != 1) eof();
			break;
		case 'b':
			if(Bread(&bin, buf, 1) != 1) eof();
			*va_arg(a, int*) = (uint)buf[0];
			break;
		case 'w':
			if(Bread(&bin, buf, 2) != 2) eof();
			*va_arg(a, int*) = (uint)buf[0] | (uint)buf[1] << 8;
			break;
		case 'l':
			if(Bread(&bin, buf, 4) != 4) eof();
			*va_arg(a, int*) = (uint)buf[0] | (uint)buf[1] << 8 |  (uint)buf[2] << 16 |  (uint)buf[3] << 24; 
			break;
		case '[':
			s = va_arg(a, void*);
			n = va_arg(a, int);
			if(n && Bread(&bin, s, n) != n) eof();
			break;
		}
	}
}

void
send(char *fmt, ...)
{
	uchar buf[4];
	va_list a;
	void *s;
	int n;

	va_start(a, fmt);
	for(;;){
		switch(*fmt++){
		case '\0':
			Bflush(&bout);
			va_end(a);
			return;
		case '_':
			buf[0] = 0;
			Bwrite(&bout, buf, 1);
			break;
		case 'b':
			buf[0] = va_arg(a, int);
			Bwrite(&bout, buf, 1);
			break;
		case 'w':
			n = va_arg(a, int);
			buf[0] = n;
			buf[1] = n >> 8;
			Bwrite(&bout, buf, 2);
			break;
		case 'l':
			n = va_arg(a, int);
			buf[0] = n;
			buf[1] = n >> 8;
			buf[2] = n >> 16;
			buf[3] = n >> 24;
			Bwrite(&bout, buf, 4);
			break;
		case '[':
			s = va_arg(a, char*);
			n = va_arg(a, int);
			if(n) Bwrite(&bout, s, n);
			break;
		}
	}
}

int
digestauth(char *server, char *user, char *method, char *url)
{
	char realm[256+1], nonce[256+1], qop[256+1], nc[10], cnonce[32+1], chal[1024], resp[1024], ouser[256];
	static uint counter;
	
	send("lblb[__b[____", 0x13, 4,
		1+strlen(user) + 2 + 1+strlen(url) + 4,
		strlen(user), user, strlen(user),
		strlen(url), url, strlen(url));
	recv("lbl", &reply, &ok, &n);
	if(reply != 0x114 || ok != 4 || n == 0)
		return -1;	/* not supported */

	recv("b", &n);
	recv("[", realm, n);
	realm[n] = '\0';

	recv("b", &n);
	recv("[", nonce, n);
	nonce[n] = '\0';

	recv("b", &n);
	recv("[", qop, n);
	qop[n] = '\0';

	genrandom((uchar*)resp, 32);
	snprint(cnonce, sizeof(cnonce), "%.32H", resp);
	snprint(nc, sizeof(nc), "%8.8ud", ++counter);

	n = snprint(chal, sizeof(chal), "%s:%s:%s:%s %s %s", nonce, nc, cnonce, qop, method, url);
	n = auth_respond(chal, n, ouser, sizeof(ouser), resp, sizeof(resp), auth_getkey,
		"proto=httpdigest role=client server=%q realm=%q user=%q", server, realm, user);
	if(n < 0)
		sysfatal("auth_respond: %r");

	send("lblb[b[b[b[b[b[b[b[", 0x13, 4, 
		1+strlen(ouser)+1+strlen(realm)+1+strlen(nonce)+1+strlen(url)+
		1+strlen(cnonce)+1+strlen(nc)+1+n+1+strlen(qop),
		strlen(ouser), ouser, strlen(ouser),
		strlen(realm), realm, strlen(realm),
		strlen(nonce), nonce, strlen(nonce),
		strlen(url), url, strlen(url),
		strlen(cnonce), cnonce, strlen(cnonce),
		strlen(nc), nc, strlen(nc),
		n, resp, n,
		strlen(qop), qop, strlen(qop));

	/* can get timeout/tls error here, so enable restart once we have the key */
	authok = 1;
	recv("lb*", &reply, &ok);
	if(reply != 0x14 && ok != 4){
		authok = 0;
		sysfatal("bad digest auth reply: %x %x", reply, ok);
	}

	return 0;
}

void
plainauth(char *user, char *pass)
{
	send("lblb[b[", 0x13, 1,
		strlen(user)+1+strlen(pass)+1, 
		strlen(user), user, strlen(user),
		strlen(pass), pass, strlen(pass));
	recv("lb*", &reply, &ok);
	if(reply != 0x14 || ok != 1)
		sysfatal("bad password auth reply: %x %x", reply, ok);
}

void
auth(char *server, char *user)
{
	static UserPasswd *up = nil;

	if(up == nil){
		if(digestauth(server, user, "POST", "/RedirectionService") == 0)
			return;

		/* if digest auth not supported, get plaintext password */
		up = auth_getuserpasswd(auth_getkey,
			"proto=pass service=sol user=%q server=%q",
			user, server);
		if(up == nil)
			sysfatal("auth_getuserpasswd: %r");
		longjmp(reconnect, 1);
	}	
	plainauth(up->user, up->passwd);
}

void
usage(void)
{
	fprint(2, "usage: %s [-TRrk] [-u user] host\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	fmtinstall('[', encodefmt);
	fmtinstall('H', encodefmt);

	ARGBEGIN {		
	case 'u':
		user = EARGF(usage());
		break;
	case 'T':
		tls = 0;
		break;
	case 'R':
		raw = 0;
		break;
	case 'r':
		raw = 1;
		break;
	case 'k':
		kvm = 1;
		break;
	default:
		usage();
	} ARGEND;

	if(argc != 1)
		usage();

	if(kvm)
		goto Connect;

	if(raw < 0) {
		char *term = getenv("TERM");
		raw = term && *term;
		free(term);
	}

	if(raw){
		close(0);
		if(open("/dev/cons", OREAD) != 0)
			sysfatal("open: %r");
		close(1);
		if(open("/dev/cons", OWRITE) != 1)
			sysfatal("open: %r");
		dup(1, 2);
		fd = open("/dev/consctl", OWRITE);
		if(fd >= 0)
			write(fd, "rawon", 5);
	}

Connect:
	fd = dial(netmkaddr(argv[0], "tcp", tls ? "16995" : "16994"), nil, nil, nil);
	if(fd < 0)
		sysfatal("dial: %r");

	if(tls){
		TLSconn conn;

		memset(&conn, 0, sizeof(conn));
		fd = tlsClient(fd, &conn);
		if(fd < 0)
			sysfatal("tls: %r");
		free(conn.cert);
		free(conn.sessionID);
		memset(&conn, 0, sizeof(conn));
	}

	authok = 0;
	Binit(&bin, fd, OREAD);
	Binit(&bout, fd, OWRITE);
	if(setjmp(reconnect)){
		Bterm(&bin);
		Bterm(&bout);
		close(fd);
		goto Connect;
	}

	if(kvm)
		send("l[", 0x110, "KVMR", 4);
	else
		send("l[", 0x10, "SOL ", 4);

	recv("lb*", &reply, &ok);
	if(reply != 0x11 || ok != 1)
		sysfatal("bad session reply: %x %x", reply, ok);

	auth(argv[0], user);
	authok = 1;

	/* kvm port redirect */
	if(kvm){
		int from, to;

		send("b_______", 0x40);
		if(read(fd, buf, 8) != 8 || buf[0] != 0x41)
			sysfatal("bad redirection reply: %x %x", reply, ok);

		pid = fork();
		if(pid == 0){
			pid = getppid();
			from = 0;
			to = fd;
		} else {
			from = fd;
			to = 1;
		}
		atexit(killpid);
		for(;;){
			n = read(from, buf, sizeof(buf));
			if(n < 0)
				sysfatal("read: %r");
			if(n == 0)
				break;
			if(write(to, buf, n) != n)
				sysfatal("write: %r");
		}
		exits(nil);
	}

	/* serial over lan */
	send("l____wwwwww____", 0x20,
		MAX_TRANSMIT_BUFFER,
		TRANSMIT_BUFFER_TIMEOUT,
		TRANSMIT_OVERFLOW_TIMEOUT,
		HOST_SESSION_RX_TIMEOUT,
		HOST_FIFO_RX_FLUSH_TIMEOUT,
		HEATBEAT_INTERVAL);
	recv("lb*", &reply, &ok);
	if(reply != 0x21 || ok != 1)
		sysfatal("bad sol reply: %x %x", reply, ok);

	pid = fork();
	if(pid == 0){
		pid = getppid();
		atexit(killpid);
		for(;;){
			n = read(0, buf, sizeof(buf));
			if(n < 0)
				sysfatal("read: %r");
			if(n == 0)
				break;
			send("l____w[", 0x28, n, buf, n);
		}
	} else {
		atexit(killpid);
		for(;;){
			recv("l", &reply);
			switch(reply){
			default:
				sysfatal("unknown reply %x\n", reply);
				break;
			case 0x2a:
				recv("____w", &n);
				recv("[", buf, n);
				if(write(1, buf, n) != n)
					break;
				break;
			case 0x24:
			case 0x2b:
				recv("*");
				break;
			}
		}
	}

	exits(nil);
}
