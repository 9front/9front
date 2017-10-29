#include "os.h"
#include <libsec.h>

/* little-endian data order */
#define	GET4(p)		((p)[0]|((p)[1]<<8)|((p)[2]<<16)|((p)[3]<<24))
#define	PUT4(p,v)	(p)[0]=(v);(p)[1]=(v)>>8;(p)[2]=(v)>>16;(p)[3]=(v)>>24

static void
gf_mulx(uchar *x)
{
	ulong t0, t1, t2, t3, t4;

	t0 = GET4(x);
	t1 = GET4(x+4);
	t2 = GET4(x+8);
	t3 = GET4(x+12);

	t4 =             (t3 >> 31);
	t3 = (t3 << 1) | (t2 >> 31);
	t2 = (t2 << 1) | (t1 >> 31);
	t1 = (t1 << 1) | (t0 >> 31);
	t0 = (t0 << 1) ^ (t4*135);

	PUT4(x, t0);
	PUT4(x+4, t1);
	PUT4(x+8, t2);
	PUT4(x+12, t3);
}

static void
xor128(uchar *o, uchar *i1, uchar *i2)
{
	int i;

	for(i=0; i<16; i++)
		o[i] = i1[i] ^ i2[i];
}

static void
setupT(AESstate *tweak, uvlong sectorNumber, uchar T[AESbsize])
{
	PUT4(T+0, (ulong)sectorNumber), sectorNumber >>= 32;
	PUT4(T+4, (ulong)sectorNumber);
	PUT4(T+8, 0);
	PUT4(T+12, 0);
	aes_encrypt(tweak->ekey, tweak->rounds, T, T);
}

void
aes_xts_encrypt(AESstate *tweak, AESstate *ecb,
	uvlong sectorNumber, uchar *input, uchar *output, ulong len)
{
	uchar T[AESbsize], x[AESbsize];
	
	if(len % AESbsize)
		abort();

	setupT(tweak, sectorNumber, T);
	for (; len > 0; len -= AESbsize, input += AESbsize, output += AESbsize) {
		xor128(x, input, T);
		aes_encrypt(ecb->ekey, ecb->rounds, x, x);
		xor128(output, x, T);
		gf_mulx(T);
	}
}

void
aes_xts_decrypt(AESstate *tweak, AESstate *ecb,
	uvlong sectorNumber, uchar *input, uchar *output, ulong len)
{
	uchar T[AESbsize], x[AESbsize];
	
	if(len % AESbsize)
		abort();

	setupT(tweak, sectorNumber, T);
	for (; len > 0; len -= AESbsize, input += AESbsize, output += AESbsize) {
		xor128(x, input, T);
		aes_decrypt(ecb->dkey, ecb->rounds, x, x);
		xor128(output, x, T);
		gf_mulx(T);
	}
}
