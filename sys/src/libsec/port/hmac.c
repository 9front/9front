#include "os.h"
#include <libsec.h>

/* rfc2104 */
DigestState*
hmac_x(uchar *p, ulong len, uchar *key, ulong klen, uchar *digest, DigestState *s,
	DigestState*(*x)(uchar*, ulong, uchar*, DigestState*), int xlen, int B)
{
	int i;
	uchar pad[144], innerdigest[256];

	if(xlen > sizeof(innerdigest))
		abort();
	if(B > sizeof(pad))
		abort();
	if(klen > B){
		if(xlen > B)
			return nil;
		(*x)(key, klen, innerdigest, nil);
		key = innerdigest;
		klen = xlen;
	}

	/* first time through */
	if(s == nil || s->seeded == 0){
		memset(pad, 0x36, B);
		for(i = 0; i < klen; i++)
			pad[i] ^= key[i];
		s = (*x)(pad, B, nil, s);
		if(s == nil)
			return nil;
	}

	s = (*x)(p, len, nil, s);
	if(digest == nil)
		return s;

	/* last time through */
	memset(pad, 0x5c, B);
	for(i = 0; i < klen; i++)
		pad[i] ^= key[i];
	(*x)(nil, 0, innerdigest, s);
	s = (*x)(pad, B, nil, nil);
	(*x)(innerdigest, xlen, digest, s);
	return nil;
}
