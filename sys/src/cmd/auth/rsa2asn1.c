#include <u.h>
#include <libc.h>
#include <auth.h>
#include <mp.h>
#include <libsec.h>
#include "rsa2any.h"

void
usage(void)
{
	fprint(2, "usage: auth/rsa2asn1 [file]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	uchar buf[16*1024];
	RSApriv *k;
	int n;

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(argc > 1)
		usage();

	if((k = getrsakey(argc, argv, 0, nil)) == nil)
		sysfatal("%r");
	if((n = asn1encodeRSApub(&k->pub, buf, sizeof(buf))) < 0)
		sysfatal("asn1encodeRSApub: %r");
	if(write(1, buf, n) != n)
		sysfatal("write: %r");
	exits(nil);
}
