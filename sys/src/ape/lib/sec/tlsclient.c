#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <lib9.h>

#include <libsec.h>
#include <libnet.h>

#include <auth.h>

int debug, auth, dialfile;
char *keyspec = "";
char *servername, *file, *filex, *ccert;

void
sysfatal(char *fmt, ...)
{
	va_list a;

	va_start(a, fmt);
	vfprintf(stderr, fmt, a);
	va_end(a);
	fprintf(stderr, "\n");
	exit(1);
}

void
usage(void)
{
	fprint(2, "usage: tlsclient [-D] [-a [-k keyspec] ] [-c lib/tls/clientcert] [-t /sys/lib/tls/xxx] [-x /sys/lib/tls/xxx.exclude] [-n servername] [-o] dialstring [cmd [args...]]\n");
	exit(1);
}

void
xfer(int from, int to)
{
	char buf[12*1024];
	int n;

	while((n = read(from, buf, sizeof buf)) > 0)
		if(write(to, buf, n) < 0)
			break;
}

static int
reporter(char *fmt, ...)
{
	va_list ap;
	
	va_start(ap, fmt);
	fprint(2, "%s:  tls reports ", argv0);
	vfprint(2, fmt, ap);
	fprint(2, "\n");

	va_end(ap);
	return 0;
}

int
main(int argc, char **argv)
{
	int fd, pid;
	char *addr;
	TLSconn *conn;
	Thumbprint *thumb;
	AuthInfo *ai = nil;

//	fmtinstall('H', encodefmt);

	ARGBEGIN{
	case 'D':
		debug++;
		break;
	case 'a':
		auth++;
		break;
	case 'k':
		keyspec = EARGF(usage());
		break;
	case 't':
		file = EARGF(usage());
		break;
	case 'x':
		filex = EARGF(usage());
		break;
	case 'c':
		ccert = EARGF(usage());
		break;
	case 'n':
		servername = EARGF(usage());
		break;
	case 'o':
		dialfile = 1;
		break;
	default:
		usage();
	}ARGEND

	if(argc < 1)
		usage();

	if(filex && !file)	
		sysfatal("specifying -x without -t is useless");

	if(file){
		thumb = initThumbprints(file, filex, "x509");
		if(thumb == nil)
			sysfatal("initThumbprints: %r");
	} else
		thumb = nil;

	addr = *argv++;
	if((fd = dial(addr, 0, 0, 0)) < 0)
		sysfatal("dial %s: %r", addr);

	conn = (TLSconn*)malloc(sizeof *conn);
	memset(conn, 0, sizeof(*conn));
	conn->serverName = servername;
	if(ccert){
		conn->cert = readcert(ccert, &conn->certlen);
		if(conn->cert == nil)
			sysfatal("readcert: %r");
	}

	if(auth){
		ai = auth_proxy(fd, auth_getkey, "proto=p9any role=client %s", keyspec);
		if(ai == nil)
			sysfatal("auth_proxy: %r");

		conn->pskID = "p9secret";
		conn->psk = ai->secret;
		conn->psklen = ai->nsecret;
	}

	if(debug)
		conn->trace = reporter;

	fd = tlsClient(fd, conn);
	if(fd < 0)
		sysfatal("tlsclient: %r");

	if(thumb){
		if(!okCertificate(conn->cert, conn->certlen, thumb))
			sysfatal("cert for %s not recognized: %r", servername ? servername : addr);
		freeThumbprints(thumb);
	}

	free(conn->cert);
	free(conn->sessionID);
	free(conn);
	if(ai != nil)
		auth_freeAI(ai);

	pid = fork();
	switch(pid){
	case -1:
		sysfatal("fork: %r");
	case 0:
		pid = getppid();
		xfer(0, fd);
		break;
	default:
		xfer(fd, 1);
		break;
	}
	if(pid) kill(pid, SIGTERM);
	return 0;
}
