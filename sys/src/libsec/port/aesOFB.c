#include "os.h"
#include <libsec.h>

typedef ulong u32;

void
aesOFBencrypt(uchar *p, int len, AESstate *s)
{
	u32 o = s->offset;

	while(len > 0){
		if(o % 16){
		Odd:
			*p++ ^= s->ivec[o++ % 16], len--;
			continue;
		}
		aes_encrypt(s->ekey, s->rounds, s->ivec, s->ivec);
		if(len < 16 || ((p-(uchar*)0) & 3) != 0)
			goto Odd;
		((u32*)p)[0] ^= ((u32*)s->ivec)[0];
		((u32*)p)[1] ^= ((u32*)s->ivec)[1];
		((u32*)p)[2] ^= ((u32*)s->ivec)[2];
		((u32*)p)[3] ^= ((u32*)s->ivec)[3];
		o += 16, p += 16, len -= 16;
	}
	s->offset = o;
}

