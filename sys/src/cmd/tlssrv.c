#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mp.h>
#include <libsec.h>

char *remotesys = "";
char *logfile = nil;
int debug = 0;

static int
reporter(char *fmt, ...)
{
	va_list ap;
	char buf[2000];

	va_start(ap, fmt);
	if(logfile){
		vsnprint(buf, sizeof buf, fmt, ap);
		syslog(0, logfile, "%s tls reports %s", remotesys, buf);
	}else{
		fprint(2, "%s: %s tls reports ", argv0, remotesys);
		vfprint(2, fmt, ap);
		fprint(2, "\n");
	}
	va_end(ap);
	return 0;
}

void
usage(void)
{
	fprint(2, "usage: tlssrv -c cert [-D] [-l logfile] [-r remotesys] cmd [args...]\n");
	fprint(2, "  after  auth/secretpem key.pem > /mnt/factotum/ctl\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{
	TLSconn *conn;
	char *cert;
	int fd;

	cert = nil;
	ARGBEGIN{
	case 'D':
		debug++;
		break;
	case 'c':
		cert = EARGF(usage());
		break;
	case 'l':
		logfile = EARGF(usage());
		break;
	case 'r':
		remotesys = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if(*argv == nil)
		usage();

	if(cert == nil)
		sysfatal("no certificate specified");
	conn = (TLSconn*)mallocz(sizeof *conn, 1);
	if(conn == nil)
		sysfatal("out of memory");
	conn->chain = readcertchain(cert);
	if(conn->chain == nil)
		sysfatal("%r");
	conn->cert = conn->chain->pem;
	conn->certlen = conn->chain->pemlen;
	conn->chain = conn->chain->next;
	if(debug)
		conn->trace = reporter;

	fd = tlsServer(1, conn);
	if(fd < 0){
		reporter("failed: %r");
		exits(0);
	}
	if(debug)
		reporter("open");

	dup(fd, 0);
	dup(fd, 1);

	exec(*argv, argv);
	reporter("can't exec %s: %r", *argv);
	exits("exec");
}
