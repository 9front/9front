#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mp.h>
#include <libsec.h>
#include <auth.h>

int debug, auth;
char *keyspec = "";
char *remotesys = "";
char *logfile = nil;

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
	fprint(2, "usage: tlssrv [-D] -[aA] [-k keyspec]] [-c cert] [-l logfile] [-r remotesys] cmd [args...]\n");
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
	case 'a':
		auth = 1;
		break;
	case 'A':
		auth = -1;	/* authenticate, but dont change user */
		break;
	case 'k':
		keyspec = EARGF(usage());
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

	conn = (TLSconn*)mallocz(sizeof *conn, 1);
	if(conn == nil)
		sysfatal("out of memory");

	if(auth){
		AuthInfo *ai;

		ai = auth_proxy(0, nil, "proto=p9any role=server %s", keyspec);
		if(ai == nil)
			sysfatal("auth_proxy: %r");

		if(auth == 1){
			Dir nd;

			if(auth_chuid(ai, nil) < 0)
				sysfatal("auth_chuid: %r");

			/* chown network connection */
			nulldir(&nd);
			nd.mode = 0660;
			nd.uid = ai->cuid;
			dirfwstat(0, &nd);
		}

		conn->pskID = "p9secret";
		conn->psk = ai->secret;
		conn->psklen = ai->nsecret;
	}

	if(cert){
		conn->chain = readcertchain(cert);
		if(conn->chain == nil)
			sysfatal("%r");
		conn->cert = conn->chain->pem;
		conn->certlen = conn->chain->pemlen;
		conn->chain = conn->chain->next;
	}

	if(conn->cert == nil && conn->psklen == 0)
		sysfatal("no certificate or shared secret");

	if(debug)
		conn->trace = reporter;

	fd = tlsServer(0, conn);
	if(fd < 0){
		reporter("failed: %r");
		exits(0);
	}
	if(debug)
		reporter("open");

	dup(fd, 0);
	dup(fd, 1);
	if(fd > 1)
		close(fd);

	exec(*argv, argv);
	reporter("can't exec %s: %r", *argv);
	exits("exec");
}
