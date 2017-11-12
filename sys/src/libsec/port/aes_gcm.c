#include "os.h"
#include <libsec.h>

static void
load128(uchar b[16], ulong W[4])
{
	W[0] = (ulong)b[15] | (ulong)b[14]<<8 | (ulong)b[13]<<16 | (ulong)b[12]<<24;
	W[1] = (ulong)b[11] | (ulong)b[10]<<8 | (ulong)b[ 9]<<16 | (ulong)b[ 8]<<24;
	W[2] = (ulong)b[ 7] | (ulong)b[ 6]<<8 | (ulong)b[ 5]<<16 | (ulong)b[ 4]<<24;
	W[3] = (ulong)b[ 3] | (ulong)b[ 2]<<8 | (ulong)b[ 1]<<16 | (ulong)b[ 0]<<24;
}

static void
store128(ulong W[4], uchar b[16])
{
	b[15] = W[0], b[14] = W[0]>>8, b[13] = W[0]>>16, b[12] = W[0]>>24;
	b[11] = W[1], b[10] = W[1]>>8, b[ 9] = W[1]>>16, b[ 8] = W[1]>>24;
	b[ 7] = W[2], b[ 6] = W[2]>>8, b[ 5] = W[2]>>16, b[ 4] = W[2]>>24;
	b[ 3] = W[3], b[ 2] = W[3]>>8, b[ 1] = W[3]>>16, b[ 0] = W[3]>>24;
}

static void
gfmul(ulong X[4], ulong Y[4], ulong Z[4])
{
	long m, i;

	Z[0] = Z[1] = Z[2] = Z[3] = 0;
	for(i=127; i>=0; i--){
		m = ((long)Y[i>>5] << 31-(i&31)) >> 31;
		Z[0] ^= X[0] & m;
		Z[1] ^= X[1] & m;
		Z[2] ^= X[2] & m;
		Z[3] ^= X[3] & m;
		m = ((long)X[0]<<31) >> 31;
		X[0] = X[0]>>1 | X[1]<<31;
		X[1] = X[1]>>1 | X[2]<<31;
		X[2] = X[2]>>1 | X[3]<<31;
		X[3] = X[3]>>1 ^ (0xE1000000 & m);
	}
}

static void
prepareM(ulong H[4], ulong M[16][256][4])
{
	ulong X[4], i, j;

	for(i=0; i<16; i++){
		for(j=0; j<256; j++){
			X[0] = X[1] = X[2] = X[3] = 0;
			X[i>>2] = j<<((i&3)<<3);
			gfmul(X, H, M[i][j]);
		}
	}
}

static void
ghash1(AESGCMstate *s, ulong X[4], ulong Y[4])
{
	ulong *Xi, i;

	X[0] ^= Y[0], X[1] ^= Y[1], X[2] ^= Y[2], X[3] ^= Y[3];
	if(0){
		gfmul(X, s->H, Y);
		return;
	}

	Y[0] = Y[1] = Y[2] = Y[3] = 0;
	for(i=0; i<16; i++){
		Xi = s->M[i][(X[i>>2]>>((i&3)<<3))&0xFF];
		Y[0] ^= Xi[0];
		Y[1] ^= Xi[1];
		Y[2] ^= Xi[2];
		Y[3] ^= Xi[3];
	}
}

static void
ghashn(AESGCMstate *s, uchar *dat, ulong len, ulong Y[4])
{
	uchar tmp[16];
	ulong X[4];

	while(len >= 16){
		load128(dat, X);
		ghash1(s, X, Y);
		dat += 16, len -= 16;
	}
	if(len > 0){
		memmove(tmp, dat, len);
		memset(tmp+len, 0, 16-len);
		load128(tmp, X);
		ghash1(s, X, Y);
	}
}

static ulong
aesxctr1(AESstate *s, uchar ctr[AESbsize], uchar *dat, ulong len)
{
	uchar tmp[AESbsize];
	ulong i;

	aes_encrypt(s->ekey, s->rounds, ctr, tmp);
	if(len > AESbsize)
		len = AESbsize;
	for(i=0; i<len; i++)
		dat[i] ^= tmp[i];
	return len;
}

static void
aesxctrn(AESstate *s, uchar *dat, ulong len)
{
	uchar ctr[AESbsize];
	ulong i;

	memmove(ctr, s->ivec, AESbsize);
	while(len > 0){
		for(i=AESbsize-1; i>=AESbsize-4; i--)
			if(++ctr[i] != 0)
				break;

		if(aesxctr1(s, ctr, dat, len) < AESbsize)
			break;
		dat += AESbsize;
		len -= AESbsize;
	}
}

void
aesgcm_setiv(AESGCMstate *s, uchar *iv, int ivlen)
{
	if(ivlen == 96/8){
		memmove(s->ivec, iv, ivlen);
		memset(s->ivec+ivlen, 0, AESbsize-ivlen);
		s->ivec[AESbsize-1] = 1;
	} else {
		ulong L[4], Y[4] = {0};

		ghashn(s, iv, ivlen, Y);
		L[0] = ivlen << 3;
		L[1] = ivlen >> 29;
		L[2] = L[3] = 0;
		ghash1(s, L, Y);
		store128(Y, s->ivec);
	}
}

void
setupAESGCMstate(AESGCMstate *s, uchar *key, int keylen, uchar *iv, int ivlen)
{
	setupAESstate(s, key, keylen, nil);

	memset(s->ivec, 0, AESbsize);
	aes_encrypt(s->ekey, s->rounds, s->ivec, s->ivec);
	load128(s->ivec, s->H);
	memset(s->ivec, 0, AESbsize);
	prepareM(s->H, s->M);

	if(iv != nil && ivlen > 0)
		aesgcm_setiv(s, iv, ivlen);
}

void
aesgcm_encrypt(uchar *dat, ulong ndat, uchar *aad, ulong naad, uchar tag[16], AESGCMstate *s)
{
	ulong L[4], Y[4] = {0};

	ghashn(s, aad, naad, Y);
	aesxctrn(s, dat, ndat);
	ghashn(s, dat, ndat, Y);
	L[0] = ndat << 3;
	L[1] = ndat >> 29;
	L[2] = naad << 3;
	L[3] = naad >> 29;
	ghash1(s, L, Y);
	store128(Y, tag);
	aesxctr1(s, s->ivec, tag, 16);
}

int
aesgcm_decrypt(uchar *dat, ulong ndat, uchar *aad, ulong naad, uchar tag[16], AESGCMstate *s)
{
	ulong L[4], Y[4] = {0};
	uchar tmp[16];

	ghashn(s, aad, naad, Y);
	ghashn(s, dat, ndat, Y);
	L[0] = ndat << 3;
	L[1] = ndat >> 29;
	L[2] = naad << 3;
	L[3] = naad >> 29;
	ghash1(s, L, Y);
	store128(Y, tmp);
	aesxctr1(s, s->ivec, tmp, 16);
	if(tsmemcmp(tag, tmp, 16) != 0)
		return -1;
	aesxctrn(s, dat, ndat);
	return 0;
}
