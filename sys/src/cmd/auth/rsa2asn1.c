#include <u.h>
#include <libc.h>
#include <auth.h>
#include <mp.h>
#include <libsec.h>
#include "rsa2any.h"

int privatekey = 0;
char *format = "pkcs1";

void
usage(void)
{
	fprint(2, "usage: auth/rsa2asn1 [-a] [-f fmt] [file]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	uchar buf[16*1024];
	RSApriv *k;
	int n;

	ARGBEGIN{
	case 'a':
		privatekey = 1;
		break;
	case 'f':
		format = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if(argc > 1)
		usage();

	if((k = getrsakey(argc, argv, privatekey, nil)) == nil)
		sysfatal("%r");
	if(privatekey){
		if(strcmp(format, "pkcs1") == 0)
			n = asn1encodeRSApriv(k, buf, sizeof(buf));
		else
			sysfatal("unknown format %s", format);
		if(n < 0)
			sysfatal("encode: %r");
	}else{
		if(strcmp(format, "pkcs1") == 0)
			n = asn1encodeRSApub(&k->pub, buf, sizeof(buf));
		else if(strcmp(format, "spki") == 0)
			n = asn1encodeRSApubSPKI(&k->pub, buf, sizeof(buf));
		else
			sysfatal("unknown format %s", format);
		if(n < 0)
			sysfatal("encode: %r");
	}
	if(write(1, buf, n) != n)
		sysfatal("write: %r");
	exits(nil);
}
