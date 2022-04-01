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

int pid = 0, raw = -1, fd = -1;
char thumbfile[] = "/sys/lib/tls/amt";

Biobuf bin, bout;
jmp_buf reconnect;

void
killpid(void)
{
	postnote(PNPROC, pid, "kill");
}

void
eof(void)
{
	if(pid == 0) sysfatal("eof: %r");
	killpid();
	pid = 0;
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

void
usage(void)
{
	fprint(2, "usage: %s [-Rr] [-u user] host\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	uchar thumb[SHA2_256dlen], buf[MAX_TRANSMIT_BUFFER];
	TLSconn tls;
	char  *user = "admin";
	UserPasswd *up = nil;
	int reply, ok, n;

	fmtinstall('[', encodefmt);
	fmtinstall('H', encodefmt);

	ARGBEGIN {		
	case 'u':
		user = EARGF(usage());
		break;
	case 'R':
		raw = 0;
		break;
	case 'r':
		raw = 1;
		break;
	default:
		usage();
	} ARGEND;

	if(argc != 1)
		usage();

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

Reconnect:
	fd = dial(netmkaddr(argv[0], "tcp", "16995"), nil, nil, nil);
	if(fd < 0)
		sysfatal("dial: %r");

	memset(&tls, 0, sizeof(tls));
	fd = tlsClient(fd, &tls);
	if(fd < 0)
		sysfatal("tls client: %r");
	sha2_256(tls.cert, tls.certlen, buf, nil);
	free(tls.cert);
	free(tls.sessionID);
	memset(&tls, 0, sizeof(tls));

	if(up == nil){
		memmove(thumb, buf, sizeof(thumb));
		up = auth_getuserpasswd(auth_getkey,
			"proto=pass service=sol user=%q server=%q thumb='%.*['",
			user, argv[0], sizeof(thumb), thumb);
		if(up == nil)
			sysfatal("auth_getuserpasswd: %r");
		close(fd);
		goto Reconnect;
	}

	if(memcmp(buf, thumb, sizeof(thumb)) != 0)
		sysfatal("certificate thumb changed: %.*[, expected %.*[",
			sizeof(thumb), buf, sizeof(thumb), thumb);

	Binit(&bin, fd, OREAD);
	Binit(&bout, fd, OWRITE);
	if(setjmp(reconnect)){
		Bterm(&bin);
		Bterm(&bout);
		close(fd);
		goto Reconnect;
	}

	send("l[", 0x10, "SOL ", 4);
	recv("lb*", &reply, &ok);
	if(reply != 0x11 || ok != 1)
		sysfatal("bad session reply: %x %x", reply, ok);

	send("lblb[b[", 0x13, 1,
		strlen(up->user)+1+strlen(up->passwd)+1, 
		strlen(up->user), up->user, strlen(up->user),
		strlen(up->passwd), up->passwd, strlen(up->passwd));
	recv("lb*", &reply, &ok);
	if(reply != 0x14 || ok != 1)
		sysfatal("bad auth reply: %x %x", reply, ok);

	send("l____wwwwww____", 0x20,
		MAX_TRANSMIT_BUFFER,
		TRANSMIT_BUFFER_TIMEOUT,
		TRANSMIT_OVERFLOW_TIMEOUT,
		HOST_SESSION_RX_TIMEOUT,
		HOST_FIFO_RX_FLUSH_TIMEOUT,
		HEATBEAT_INTERVAL);
	recv("lb*", &reply, &ok);
	if(reply != 0x21 || ok != 1)
		sysfatal("bad redirection reply: %x %x", reply, ok);

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
