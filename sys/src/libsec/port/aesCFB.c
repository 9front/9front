#include "os.h"
#include <libsec.h>

typedef ulong u32;

void
aesCFBencrypt(uchar *p, int len, AESstate *s)
{
	u32 a, o = s->offset;

	while(len > 0){
		if(o % 16){
		Odd:
			a = (s->ivec[o++ % 16] ^= *p), *p++ = a, len--;
			continue;
		}
		aes_encrypt(s->ekey, s->rounds, s->ivec, s->ivec);
		if(len < 16 || ((p-(uchar*)0) & 3) != 0)
			goto Odd;
		((u32*)p)[0] = (((u32*)s->ivec)[0] ^= ((u32*)p)[0]);
		((u32*)p)[1] = (((u32*)s->ivec)[1] ^= ((u32*)p)[1]);
		((u32*)p)[2] = (((u32*)s->ivec)[2] ^= ((u32*)p)[2]);
		((u32*)p)[3] = (((u32*)s->ivec)[3] ^= ((u32*)p)[3]);
		o += 16, p += 16, len -= 16;
	}
	s->offset = o;
}

void
aesCFBdecrypt(uchar *p, int len, AESstate *s)
{
	u32 a, o = s->offset;

	while(len > 0){
		if(o % 16){
		Odd:
			a = *p, *p++ ^= s->ivec[o % 16], s->ivec[o++ % 16] = a, len--;
			continue;
		}
		aes_encrypt(s->ekey, s->rounds, s->ivec, s->ivec);
		if(len < 16 || ((p-(uchar*)0) & 3) != 0)
			goto Odd;
		a = ((u32*)p)[0], ((u32*)p)[0] ^= ((u32*)s->ivec)[0], ((u32*)s->ivec)[0] = a;
		a = ((u32*)p)[1], ((u32*)p)[1] ^= ((u32*)s->ivec)[1], ((u32*)s->ivec)[1] = a;
		a = ((u32*)p)[2], ((u32*)p)[2] ^= ((u32*)s->ivec)[2], ((u32*)s->ivec)[2] = a;
		a = ((u32*)p)[3], ((u32*)p)[3] ^= ((u32*)s->ivec)[3], ((u32*)s->ivec)[3] = a;
		o += 16, p += 16, len -= 16;
	}
	s->offset = o;
}
