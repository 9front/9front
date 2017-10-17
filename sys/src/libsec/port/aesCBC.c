#include "os.h"
#include <libsec.h>

/*
 * Define by analogy with desCBCencrypt;  AES modes are not standardized yet.
 * Because of the way that non-multiple-of-16 buffers are handled,
 * the decryptor must be fed buffers of the same size as the encryptor.
 */
void
aesCBCencrypt(uchar *p, int len, AESstate *s)
{
	uchar *p2, *ip, *eip;
	uchar q[AESbsize];

	for(; len >= AESbsize; len -= AESbsize){
		p2 = p;
		ip = s->ivec;
		for(eip = ip+AESbsize; ip < eip; )
			*p2++ ^= *ip++;
		aes_encrypt(s->ekey, s->rounds, p, q);
		memmove(s->ivec, q, AESbsize);
		memmove(p, q, AESbsize);
		p += AESbsize;
	}

	if(len > 0){
		ip = s->ivec;
		aes_encrypt(s->ekey, s->rounds, ip, q);
		memmove(s->ivec, q, AESbsize);
		for(eip = ip+len; ip < eip; )
			*p++ ^= *ip++;
	}
}

void
aesCBCdecrypt(uchar *p, int len, AESstate *s)
{
	uchar *ip, *eip, *tp;
	uchar tmp[AESbsize], q[AESbsize];

	for(; len >= AESbsize; len -= AESbsize){
		memmove(tmp, p, AESbsize);
		aes_decrypt(s->dkey, s->rounds, p, q);
		memmove(p, q, AESbsize);
		tp = tmp;
		ip = s->ivec;
		for(eip = ip+AESbsize; ip < eip; ){
			*p++ ^= *ip;
			*ip++ = *tp++;
		}
	}

	if(len > 0){
		ip = s->ivec;
		aes_encrypt(s->ekey, s->rounds, ip, q);
		memmove(s->ivec, q, AESbsize);
		for(eip = ip+len; ip < eip; )
			*p++ ^= *ip++;
	}
}
