#include "os.h"
#include <mp.h>
#include <libsec.h>

void
pbkdf2_hmac_sha1(uchar *p, ulong plen, uchar *s, ulong slen, ulong rounds, uchar *d, ulong dlen)
{
	uchar block[SHA1dlen], tmp[SHA1dlen], tmp2[SHA1dlen];
	ulong i, j, k, n;
	DigestState *ds;

	for(i = 1; dlen > 0; i++, d += n, dlen -= n){
		tmp[3] = i;
		tmp[2] = i >> 8;
		tmp[1] = i >> 16;
		tmp[0] = i >> 24;
		ds = hmac_sha1(s, slen, p, plen, nil, nil);
		hmac_sha1(tmp, 4, p, plen, block, ds);
		memmove(tmp, block, sizeof(tmp));
		for(j = 1; j < rounds; j++){
			hmac_sha1(tmp, sizeof(tmp), p, plen, tmp2, nil);
			memmove(tmp, tmp2, sizeof(tmp));
			for(k=0; k<sizeof(tmp); k++)
				block[k] ^= tmp[k];
		}
		n = dlen > sizeof(block) ? sizeof(block) : dlen;
		memmove(d, block, n); 
	}
}
