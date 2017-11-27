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
	uchar *ip, *eip;

	if(((p-(uchar*)0) & 3) == 0){
		for(; len >= AESbsize; len -= AESbsize){
			ip = s->ivec;
			((u32int*)ip)[0] ^= ((u32int*)p)[0];
			((u32int*)ip)[1] ^= ((u32int*)p)[1];
			((u32int*)ip)[2] ^= ((u32int*)p)[2];
			((u32int*)ip)[3] ^= ((u32int*)p)[3];

			aes_encrypt(s->ekey, s->rounds, ip, ip);

			((u32int*)p)[0] = ((u32int*)ip)[0];
			((u32int*)p)[1] = ((u32int*)ip)[1];
			((u32int*)p)[2] = ((u32int*)ip)[2];
			((u32int*)p)[3] = ((u32int*)ip)[3];
			p += AESbsize;
		}
	} else {
		for(; len >= AESbsize; len -= AESbsize){
			ip = s->ivec;
			for(eip = ip+AESbsize; ip < eip; )
				*ip++ ^= *p++;
			aes_encrypt(s->ekey, s->rounds, s->ivec, s->ivec);
			memmove(p - AESbsize, s->ivec, AESbsize);
		}
	}

	if(len > 0){
		ip = s->ivec;
		aes_encrypt(s->ekey, s->rounds, ip, ip);
		for(eip = ip+len; ip < eip; )
			*p++ ^= *ip++;
	}
}

void
aesCBCdecrypt(uchar *p, int len, AESstate *s)
{
	uchar *ip, *eip, *tp;
	u32int t[4];

	if(((p-(uchar*)0) & 3) == 0){
		for(; len >= AESbsize; len -= AESbsize){
			t[0] = ((u32int*)p)[0];
			t[1] = ((u32int*)p)[1];
			t[2] = ((u32int*)p)[2];
			t[3] = ((u32int*)p)[3];

			aes_decrypt(s->dkey, s->rounds, p, p);

			ip = s->ivec;
			((u32int*)p)[0] ^= ((u32int*)ip)[0];
			((u32int*)p)[1] ^= ((u32int*)ip)[1];
			((u32int*)p)[2] ^= ((u32int*)ip)[2];
			((u32int*)p)[3] ^= ((u32int*)ip)[3];
			p += AESbsize;

			((u32int*)ip)[0] = t[0];
			((u32int*)ip)[1] = t[1];
			((u32int*)ip)[2] = t[2];
			((u32int*)ip)[3] = t[3];
		}
	} else {
		for(; len >= AESbsize; len -= AESbsize){
			tp = (uchar*)t;
			memmove(tp, p, AESbsize);
			aes_decrypt(s->dkey, s->rounds, p, p);
			ip = s->ivec;
			for(eip = ip+AESbsize; ip < eip; ){
				*p++ ^= *ip;
				*ip++ = *tp++;
			}
		}
	}

	if(len > 0){
		ip = s->ivec;
		aes_encrypt(s->ekey, s->rounds, ip, ip);
		for(eip = ip+len; ip < eip; )
			*p++ ^= *ip++;
	}
}
