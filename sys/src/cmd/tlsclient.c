#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>

void
usage(void)
{
	fprint(2, "usage: tlsclient [-c lib/tls/clientcert] [-t /sys/lib/tls/xxx] [-x /sys/lib/tls/xxx.exclude] dialstring\n");
	exits("usage");
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

void
main(int argc, char **argv)
{
	int fd, netfd, debug;
	uchar digest[20];
	TLSconn *conn;
	char *addr, *file, *filex, *ccert;
	Thumbprint *thumb;

	file = nil;
	filex = nil;
	thumb = nil;
	ccert=nil;
	debug=0;
	ARGBEGIN{
	case 't':
		file = EARGF(usage());
		break;
	case 'x':
		filex = EARGF(usage());
		break;
	case 'D':
		debug++;
		break;
	case 'c':
		ccert = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if(argc != 1)
		usage();

	if(filex && !file)	
		sysfatal("specifying -x without -t is useless");
	if(file){
		thumb = initThumbprints(file, filex);
		if(thumb == nil)
			sysfatal("initThumbprints: %r");
	}

	addr = argv[0];
	if((netfd = dial(addr, 0, 0, 0)) < 0)
		sysfatal("dial %s: %r", addr);

	conn = (TLSconn*)mallocz(sizeof *conn, 1);
	if(ccert)
		conn->cert = readcert(ccert, &conn->certlen);
	if(debug)
		conn->trace = reporter;
	fd = tlsClient(netfd, conn);
	if(fd < 0)
		sysfatal("tlsclient: %r");
	if(thumb){
		if(conn->cert==nil || conn->certlen<=0)
			sysfatal("server did not provide TLS certificate");
		sha1(conn->cert, conn->certlen, digest, nil);
		if(!okThumbprint(digest, thumb)){
			fmtinstall('H', encodefmt);
			sysfatal("server certificate %.*H not recognized", SHA1dlen, digest);
		}
	}
	free(conn->cert);
	close(netfd);

	rfork(RFNOTEG);
	switch(fork()){
	case -1:
		fprint(2, "%s: fork: %r\n", argv0);
		exits("dial");
	case 0:
		xfer(0, fd);
		break;
	default:
		xfer(fd, 1);
		break;
	}
	postnote(PNGROUP, getpid(), "die yankee pig dog");
	exits(0);
}
