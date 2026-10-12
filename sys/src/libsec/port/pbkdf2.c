#include "os.h"
#include <libsec.h>

/* rfc2898 */
void
pbkdf2_x(uchar *p, ulong plen, uchar *s, ulong slen,
	ulong rounds, uchar *d, ulong dlen,
	DigestState* (*x)(uchar*, ulong, uchar*, ulong, uchar*, DigestState*), int xlen)
{
	uchar block[256], tmp[256];
	ulong i, j, k, n;
	DigestState ds, scratch;

	assert(xlen <= sizeof(tmp));

	for(i = 1; dlen > 0; i++, d += n, dlen -= n){
		tmp[3] = i;
		tmp[2] = i >> 8;
		tmp[1] = i >> 16;
		tmp[0] = i >> 24;
		memset(&ds, 0, sizeof ds);
		(*x)(s, slen, p, plen, nil, &ds);
		(*x)(tmp, 4, p, plen, block, &ds);
		memmove(tmp, block, xlen);
		for(j = 1; j < rounds; j++){
			memset(&scratch, 0, sizeof scratch);
			(*x)(tmp, xlen, p, plen, tmp, &scratch);
			for(k=0; k<xlen; k++)
				block[k] ^= tmp[k];
		}
		n = dlen > xlen ? xlen : dlen;
		memmove(d, block, n); 
	}
}
