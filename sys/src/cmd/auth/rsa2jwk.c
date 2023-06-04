#include <u.h>
#include <libc.h>
#include <auth.h>
#include <mp.h>
#include <libsec.h>
#include "rsa2any.h"

#define between(x,min,max)	(((min-1-x) & (x-max-1))>>8)

int
encurl64chr(int o)
{
	int c;

	c  = between(o,  0, 25) & ('A'+o);
	c |= between(o, 26, 51) & ('a'+(o-26));
	c |= between(o, 52, 61) & ('0'+(o-52));
	c |= between(o, 62, 62) & ('-');
	c |= between(o, 63, 63) & ('_');
	return c;
}

char*
encurl64(void *in, int n)
{
	int lim;
	char *out, *p;

	lim = 4*n/3 + 5;
	if((out = malloc(lim)) == nil)
		sysfatal("malloc: %r");
	enc64x(out, lim, in, n, encurl64chr);
	if((p = strchr(out, '=')) != nil)
		*p = 0;
	return out;
}

void
usage(void)
{
	fprint(2, "usage: auth/rsa2pub [file]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	uchar nbuf[8192], ebuf[512];
	char *nstr, *estr, *s;
	RSApriv *k;
	int nlen, elen;

	fmtinstall('[', encodefmt);
	quotefmtinstall();

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(argc > 1)
		usage();

	if((k = getrsakey(argc, argv, 0, nil)) == nil)
		sysfatal("%r");

	nlen = (mpsignif(k->pub.n)+7)/8;
	if(nlen >= sizeof(nbuf))
		sysfatal("key too big");
	mptobe(k->pub.n, nbuf, nlen, nil);
	nstr = encurl64(nbuf, nlen);

	elen = (mpsignif(k->pub.ek)+7)/8;
	if(elen >= sizeof(ebuf))
		sysfatal("key too big");
	mptobe(k->pub.ek, ebuf, elen, nil);
	estr = encurl64(ebuf, elen);

	s = smprint(
		"{"
		"\"kty\": \"RSA\","
		"\"n\": \"%s\","
		"\"e\": \"%s\""
		"}\n",
		nstr, estr);
	if(s == nil)
		sysfatal("smprint: %r");
	if(write(1, s, strlen(s))  != strlen(s))
		sysfatal("write: %r");
	exits(nil);
}
