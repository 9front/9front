#include "os.h"
#include <libsec.h>

/* rfc5869 */
void
hkdf_x(uchar *salt, ulong nsalt, uchar *info, ulong ninfo,
	uchar *key, ulong nkey, uchar *d, ulong dlen,
	DigestState* (*x)(uchar*, ulong, uchar*, ulong, uchar*, DigestState*), int xlen)
{
	uchar prk[256], tmp[256], cnt;
	DigestState ds;

	assert(xlen <= sizeof(tmp));

	memset(tmp, 0, xlen);
	if(nsalt == 0){
		salt = tmp;
		nsalt = xlen;
	}
	memset(&ds, 0, sizeof ds);
	/* note that salt and key are swapped in this case */
	(*x)(key, nkey, salt, nsalt, prk, &ds);
	memset(&ds, 0, sizeof ds);
	for(cnt=1;; cnt++) {
		if(ninfo > 0)
			(*x)(info, ninfo, prk, xlen, nil, &ds);
		(*x)(&cnt, 1, prk, xlen, tmp, &ds);
		memset(&ds, 0, sizeof ds);
		if(dlen <= xlen){
			memmove(d, tmp, dlen);
			break;
		}
		memmove(d, tmp, xlen);
		dlen -= xlen;
		d += xlen;
		(*x)(tmp, xlen, prk, xlen, nil, &ds);
	}
}
