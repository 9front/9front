#include <u.h>
#include <libc.h>
#include <auth.h>
#include <mp.h>
#include <libsec.h>
#include "rsa2any.h"

void
usage(void)
{
	fprint(2, "usage: auth/rsa2ssh [-c comment] [file]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	RSApriv *k;
	char *comment, *s;
	uchar buf[8192], *p;

	fmtinstall('B', mpfmt);
	fmtinstall('[', encodefmt);

	comment = "";

	ARGBEGIN{
	case 'c':
		comment = EARGF(usage());
		break;
	case '2':	/* backwards compatibility */
		break;
	default:
		usage();
	}ARGEND

	if(argc > 1)
		usage();

	if((k = getrsakey(argc, argv, 0, nil)) == nil)
		sysfatal("%r");

	p = buf;
	p = put4(p, 7);
	p = putn(p, "ssh-rsa", 7);
	p = putmp2(p, k->pub.ek);
	p = putmp2(p, k->pub.n);

	s = smprint("ssh-rsa %.*[ %s\n", (int)(p-buf), buf, comment);
	if(s == nil)
		sysfatal("smprint: %r");
	if(write(1, s, strlen(s)) != strlen(s))
		sysfatal("write: %r");

	exits(nil);
}
