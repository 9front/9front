// Author Taru Karttunen <taruti@taruti.net>
// This file can be used as both Public Domain or Creative Commons CC0.
#include "os.h"
#include <libsec.h>

#define AesBlockSize 16

static void xor128(uchar* o,uchar* i1,uchar* i2) {
	((ulong*)o)[0] = ((ulong*)i1)[0] ^ ((ulong*)i2)[0];
	((ulong*)o)[1] = ((ulong*)i1)[1] ^ ((ulong*)i2)[1];
	((ulong*)o)[2] = ((ulong*)i1)[2] ^ ((ulong*)i2)[2];
	((ulong*)o)[3] = ((ulong*)i1)[3] ^ ((ulong*)i2)[3];
}

static void gf_mulx(uchar* x) {
    ulong t = ((((ulong*)(x))[3] & 0x80000000u) ? 0x00000087u : 0);;
    ((ulong*)(x))[3] = (((ulong*)(x))[3] << 1) | (((ulong*)(x))[2] & 0x80000000u ? 1 : 0);
    ((ulong*)(x))[2] = (((ulong*)(x))[2] << 1) | (((ulong*)(x))[1] & 0x80000000u ? 1 : 0);
    ((ulong*)(x))[1] = (((ulong*)(x))[1] << 1) | (((ulong*)(x))[0] & 0x80000000u ? 1 : 0);
    ((ulong*)(x))[0] = (((ulong*)(x))[0] << 1) ^ t;

}

int aes_xts_encrypt(ulong tweak[], ulong ecb[],  vlong sectorNumber, uchar *input, uchar *output, ulong len) {
	uchar T[16], x[16];
	int i;
	
	if(len % 16 != 0)
		return -1;

	for(i=0; i<AesBlockSize; i++) {
		T[i] = (uchar)(sectorNumber & 0xFF);
		sectorNumber = sectorNumber >> 8;
	}
	
	aes_encrypt(tweak, 10, T, T);

	for (i=0; i<len; i+=AesBlockSize) {
		xor128(&x[0], &input[i], &T[0]);
		aes_encrypt(ecb, 10, x, x);
		xor128(&output[i], &x[0], &T[0]);
		gf_mulx(&T[0]);
	}
	return 0;
}

int aes_xts_decrypt(ulong tweak[], ulong ecb[], vlong sectorNumber, uchar *input, uchar *output, ulong len) {
	uchar T[16], x[16];
	int i;
	
	if(len % 16 != 0)
		return -1;

	for(i=0; i<AesBlockSize; i++) {
		T[i] = (uchar)(sectorNumber & 0xFF);
		sectorNumber = sectorNumber >> 8;
	}
	
	aes_encrypt(tweak, 10, T, T);

	for (i=0; i<len; i+=AesBlockSize) {
		xor128(&x[0], &input[i], &T[0]);
		aes_decrypt(ecb, 10, x, x);
		xor128(&output[i], &x[0], &T[0]);
		gf_mulx(&T[0]);
	}
	return 0;
}

