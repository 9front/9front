#include "os.h"
#include <libsec.h>

#define movw(w, S, D)	memmove(D, S, (w)*4)

static void
xorw(ulong w, u32int *S, u32int *D)
{
	for(w /= 8; w; w--, D += 8, S += 8){
		D[0] ^= S[0];
		D[1] ^= S[1];
		D[2] ^= S[2];
		D[3] ^= S[3];
		D[4] ^= S[4];
		D[5] ^= S[5];
		D[6] ^= S[6];
		D[7] ^= S[7];
	}
}

static void
scryptBlockMix(ulong R, u32int *B, u32int *Y)
{
	u32int X[16];
	ulong i;

	R *= 2;
	movw(16, &B[(R-1)*16], X);
	for(i = 0; i < R; i += 2){
		xorw(16, &B[i*16], X);
		salsa_core(X, X, 8);
		movw(16, X, &Y[i*8]);

		xorw(16, &B[(i+1)*16], X);
		salsa_core(X, X, 8);
		movw(16, X, &Y[i*8 + R*8]);
	}
}

static void
scryptROMix(ulong R, ulong N, u32int *V, u32int *X, uchar *B)
{
	ulong w, i, d;
	u32int *Y;

	w = R*32;
	for(i=0; i<w; i++, B+=4)
		X[i] = B[0] | (B[1]<<8) | (B[2]<<16) | (B[3]<<24);

	Y = &X[w];
	for(i=0; i<N; i += 2){
		movw(w, X, &V[i*w]);
		scryptBlockMix(R, X, Y);

		movw(w, Y, &V[(i+1)*w]);
		scryptBlockMix(R, Y, X);
	}
	for(i=0; i<N; i += 2){
		xorw(w, &V[(X[w-16] & (N-1))*w], X);
		scryptBlockMix(R, X, Y);

		xorw(w, &V[(Y[w-16] & (N-1))*w], Y);
		scryptBlockMix(R, Y, X);
	}

	B -= w*4;
	for(i=0; i<w; i++, B+=4)
		d = X[i], B[0]=d, B[1]=d>>8, B[2]=d>>16, B[3]=d>>24;
}

char*
scrypt(p, plen, s, slen, N, R, P, d, dlen)
	ulong plen, slen, dlen, N, R, P;
	uchar *p, *s, *d;
{
	static char oom[] = "out of memory";

	ulong rb, i;
	u32int *V, *X;
	uchar *B;

	if(P < 1)
		return "invalid parallelization parameter P";
	if(R < 1 || R >= (1UL<<(31-7))/P)
		return "invalid block size parameter R";
	if(N < 2 || (N & (N-1)) != 0 || N >= (1UL<<(31-7))/R)
		return "invalid cpu/memory cost parameter N";

	rb = R<<7;
	if((B = malloc(P*rb)) == nil)
		return oom;
	if((V = malloc(N*rb)) == nil){
		free(B);
		return oom;
	}
	if((X = malloc(2*rb)) == nil){
		free(V);
		free(B);
		return oom;
	}

	pbkdf2_x(p, plen, s, slen, 1, B, P*rb, hmac_sha2_256, SHA2_256dlen);

	for(i=0; i<P; i++)
		scryptROMix(R, N, V, X, &B[i*rb]);

	memset(X, 0, 2*rb);
	free(X);

	memset(V, 0, N*rb);
	free(V);

	pbkdf2_x(p, plen, B, P*rb, 1, d, dlen, hmac_sha2_256, SHA2_256dlen);

	memset(B, 0, P*rb);
	free(B);

	return nil;
}
