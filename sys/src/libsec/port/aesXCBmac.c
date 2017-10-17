#include "os.h"
#include <libsec.h>

/*
 * AES-XCBC-MAC-96 message authentication, per rfc3566.
 */
static uchar basekey[3][16] = {
	{
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	},
	{
	0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
	0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
	},
	{
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	},
};

void
setupAESXCBCstate(AESstate *s)		/* was setupmac96 */
{
	int i, j;
	uint q[16 / sizeof(uint)];
	uchar *p;

	assert(s->keybytes == 16);
	for(i = 0; i < 3; i++)
		aes_encrypt(s->ekey, s->rounds, basekey[i],
			s->mackey + AESbsize*i);

	p = s->mackey;
	memset(q, 0, AESbsize);

	/*
	 * put the in the right endian.  once figured, probably better
	 * to use some fcall macros.
	 * keys for encryption in local endianness for the algorithm...
	 * only key1 is used for encryption;
	 * BUG!!: I think this is what I got wrong.
	 */
	for(i = 0; i < 16 / sizeof(uint); i ++){
		for(j = 0; j < sizeof(uint); j++)
			q[i] |= p[sizeof(uint)-j-1] << 8*j;
		p += sizeof(uint);
	}
	memmove(s->mackey, q, 16);
}

/*
 * Not dealing with > 128-bit keys, not dealing with strange corner cases like
 * empty message.  Should be fine for AES-XCBC-MAC-96.
 */
uchar*
aesXCBCmac(uchar *p, int len, AESstate *s)
{
	uchar *p2, *ip, *eip, *mackey;
	uchar q[AESbsize];

	assert(s->keybytes == 16);	/* more complicated for bigger */
	memset(s->ivec, 0, AESbsize);	/* E[0] is 0+ */

	for(; len > AESbsize; len -= AESbsize){
		memmove(q, p, AESbsize);
		p2 = q;
		ip = s->ivec;
		for(eip = ip + AESbsize; ip < eip; )
			*p2++ ^= *ip++;
		aes_encrypt((ulong *)s->mackey, s->rounds, q, s->ivec);
		p += AESbsize;
	}
	/* the last one */

	memmove(q, p, len);
	p2 = q+len;
	if(len == AESbsize)
		mackey = s->mackey + AESbsize;	/* k2 */
	else{
		mackey = s->mackey+2*AESbsize;	/* k3 */
		*p2++ = 1 << 7;			/* padding */
		len = AESbsize - len - 1;
		memset(p2, 0, len);
	}

	ip = s->ivec;
	p2 = q;
	for(eip = ip + AESbsize; ip < eip; )
		*p2++ ^= *ip++ ^ *mackey++;
	aes_encrypt((ulong *)s->mackey, s->rounds, q, s->ivec);
	return s->ivec;			/* only the 12 bytes leftmost */
}
