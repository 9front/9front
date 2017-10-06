#include <u.h>
#include <libc.h>
#include <auth.h>
#include <mp.h>
#include <libsec.h>
#include "rsa2any.h"

void
usage(void)
{
	fprint(2, "usage: auth/rsa2pub [file]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	RSApriv *key;
	Attr *a;
	char *s;
	int n;

	fmtinstall('A', _attrfmt);
	fmtinstall('B', mpfmt);
	quotefmtinstall();

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(argc > 1)
		usage();

	if((key = getrsakey(argc, argv, 0, &a)) == nil)
		sysfatal("%r");

	if((s = smprint("key %A size=%d ek=%B n=%B\n", a, 
		mpsignif(key->pub.n), key->pub.ek, key->pub.n)) == nil)
		sysfatal("smprint: %r");
	n = strlen(s);
	if(write(1, s, n) != n)
		sysfatal("write: %r");
	exits(nil);
}
