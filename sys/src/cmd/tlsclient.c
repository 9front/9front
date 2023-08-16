#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>
#include <auth.h>

int debug, auth, dialfile;
char *keyspec = "";
char *servername, *file, *filex, *ccert, *dumpcert;

void
usage(void)
{
	fprint(2, "usage: tlsclient [-D] [-a [-k keyspec] ] [-c clientcert.pem] [-d servercert] [-t /sys/lib/tls/xxx] [-x /sys/lib/tls/xxx.exclude] [-n servername] [-o] dialstring [cmd [args...]]\n");
	exits("usage");
}

void
xfer(int from, int to)
{
	char buf[IOUNIT];
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

void
main(int argc, char **argv)
{
	int fd, dfd;
	char *addr;
	TLSconn *conn;
	Thumbprint *thumb;
	AuthInfo *ai = nil;

	fmtinstall('[', encodefmt);
	fmtinstall('H', encodefmt);

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
	case 'd':
		dumpcert = EARGF(usage());
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
	if((fd = dialfile? open(addr, ORDWR): dial(addr, 0, 0, 0)) < 0)
		sysfatal("dial %s: %r", addr);

	conn = (TLSconn*)mallocz(sizeof *conn, 1);
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

	if(dumpcert){
		if((dfd = create(dumpcert, OWRITE, 0666)) < 0)
			sysfatal("create: %r");
		if(conn->cert != nil)
			write(dfd, conn->cert, conn->certlen);
		write(dfd, "", 0);
		close(dfd);
	}

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

	if(*argv){
		dup(fd, 0);
		dup(fd, 1);
		/* dup(fd, 2); keep stderr */
		if(fd > 2) close(fd);
		exec(argv[0], argv);
		if(argv[0][0] != '/')
			exec(smprint("/bin/%s", argv[0]), argv);
		sysfatal("exec: %r");
	}

	rfork(RFNOTEG);
	switch(fork()){
	case -1:
		sysfatal("fork: %r");
	case 0:
		xfer(0, fd);
		break;
	default:
		xfer(fd, 1);
		break;
	}
	postnote(PNGROUP, getpid(), "die yankee pig dog");
	exits(nil);
}
