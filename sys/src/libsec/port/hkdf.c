#include "os.h"
#include <libsec.h>

/* rfc5869 */
void
hkdf_x(salt, nsalt, info, ninfo, key, nkey, d, dlen, x, xlen)
	uchar *salt, *info, *key, *d;
	ulong nsalt, ninfo, nkey, dlen;
	DigestState* (*x)(uchar*, ulong, uchar*, ulong, uchar*, DigestState*);
	int xlen;
{
	uchar prk[256], tmp[256], cnt;
	DigestState *ds;

	assert(xlen <= sizeof(tmp));

	memset(tmp, 0, xlen);
	if(nsalt == 0){
		salt = tmp;
		nsalt = xlen;
	}
	/* note that salt and key are swapped in this case */
	(*x)(key, nkey, salt, nsalt, prk, nil);
	ds = nil;
	for(cnt=1;; cnt++) {
		if(ninfo > 0)
			ds = (*x)(info, ninfo, prk, xlen, nil, ds);
		(*x)(&cnt, 1, prk, xlen, tmp, ds);
		if(dlen <= xlen){
			memmove(d, tmp, dlen);
			break;
		}
		memmove(d, tmp, xlen);
		dlen -= xlen;
		d += xlen;
		ds = (*x)(tmp, xlen, prk, xlen, nil, nil);
	}
}
