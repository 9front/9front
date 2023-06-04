#include <u.h>
#include <libc.h>
#include <bio.h>
#include <auth.h>
#include <mp.h>
#include <libsec.h>

int fd;
int req = 0;
char subject[1024];

void
usage(void)
{
	fprint(2, "usage: auth/x5092pub [-r] [file]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int tot, n;
	char *s;
	uchar *buf;
	RSApub *pub;

	quotefmtinstall();
	fmtinstall('B', mpfmt);
	fmtinstall('H', encodefmt);

	ARGBEGIN{
	case 'r':
		req = 1;
		break;
	default:
		usage();
	}ARGEND

	fd = 0;
	if(argc == 1)
		fd = open(argv[0], OREAD);
	else if(argc != 0)
		usage();
	buf = nil;
	tot = 0;
	for(;;){
		buf = realloc(buf, tot+8192);
		if(buf == nil)
			sysfatal("realloc: %r");
		if((n = read(fd, buf+tot, 8192)) < 0)
			sysfatal("read: %r");
		if(n == 0)
			break;
		tot += n;
	}
	if(req)
		pub = X509reqtoRSApub(buf, tot, subject, sizeof(subject));
	else
		pub = X509toRSApub(buf, tot, subject, sizeof(subject));
	if(pub == nil)
		sysfatal("X509toRSApub: %r");
	s = smprint("key proto=rsa size=%d ek=%B n=%B subject=%q \n", mpsignif(pub->n), pub->ek, pub->n, subject);
	if(s == nil)
		sysfatal("smprint: %r");
	if(write(1, s, strlen(s)) != strlen(s))
		sysfatal("write: %r");
	exits(nil);
}
