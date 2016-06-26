/*
Adapted from chacha-merged.c version 20080118
D. J. Bernstein
Public domain.

modified for use in Plan 9 and Inferno (no algorithmic changes),
and including the changes to block number and nonce defined in RFC7539
*/

#include "os.h"
#include <libsec.h>

enum{
	Blockwords=	ChachaBsize/sizeof(u32int)
};

/* little-endian data order */
#define	GET4(p)		((p)[0]|((p)[1]<<8)|((p)[2]<<16)|((p)[3]<<24))
#define	PUT4(p,v)	(p)[0]=(v);(p)[1]=(v)>>8;(p)[2]=(v)>>16;(p)[3]=(v)>>24

#define ROTATE(v,c) ((u32int)((v) << (c)) | ((v) >> (32 - (c))))

#define QUARTERROUND(ia,ib,ic,id) { \
	u32int a, b, c, d, t; \
	a = x[ia]; b = x[ib]; c = x[ic]; d = x[id]; \
	a += b; t = d^a; d = ROTATE(t,16); \
	c += d; t = b^c; b = ROTATE(t,12); \
	a += b; t = d^a; d = ROTATE(t, 8); \
	c += d; t = b^c; b = ROTATE(t, 7); \
	x[ia] = a; x[ib] = b; x[ic] = c; x[id] = d; \
}

#define ENCRYPT(s, x, y, d) {\
	u32int v; \
	uchar *sp, *dp; \
	sp = (s); \
	v = GET4(sp); \
	v ^= (x)+(y); \
	dp = (d); \
	PUT4(dp, v); \
}

static uchar sigma[16] = "expand 32-byte k";
static uchar tau[16] = "expand 16-byte k";

static void
load(u32int *d, uchar *s, int nw)
{
	int i;

	for(i = 0; i < nw; i++, s+=4)
		d[i] = GET4(s);
}

void
setupChachastate(Chachastate *s, uchar *key, ulong keylen, uchar *iv, ulong ivlen, int rounds)
{
	if(keylen != 256/8 && keylen != 128/8)
		sysfatal("invalid chacha key length");
	if(ivlen != 96/8 && ivlen != 64/8)
		sysfatal("invalid chacha iv length");
	if(rounds == 0)
		rounds = 20;
	s->rounds = rounds;
	if(keylen == 256/8) { /* recommended */
		load(&s->input[0], sigma, 4);
		load(&s->input[4], key, 8);
	}else{
		load(&s->input[0], tau, 4);
		load(&s->input[4], key, 4);
		load(&s->input[8], key, 4);
	}
	s->ivwords = ivlen/sizeof(u32int);
	s->input[12] = 0;
	s->input[13] = 0;
	if(iv == nil){
		s->input[14] = 0;
		s->input[15] = 0;
	}else
		chacha_setiv(s, iv);
}

void
chacha_setiv(Chachastate *s, uchar *iv)
{
	load(&s->input[16 - s->ivwords], iv, s->ivwords);
}

void
chacha_setblock(Chachastate *s, u64int blockno)
{
	s->input[12] = blockno;
	if(s->ivwords == 2)
		s->input[13] = blockno>>32;
}

static void
dorounds(u32int x[Blockwords], int rounds)
{
	for(; rounds > 0; rounds -= 2) {
		QUARTERROUND(0, 4, 8,12)
		QUARTERROUND(1, 5, 9,13)
		QUARTERROUND(2, 6,10,14)
		QUARTERROUND(3, 7,11,15)

		QUARTERROUND(0, 5,10,15)
		QUARTERROUND(1, 6,11,12)
		QUARTERROUND(2, 7, 8,13)
		QUARTERROUND(3, 4, 9,14)
	}
}

static void
encryptblock(Chachastate *s, uchar *src, uchar *dst)
{
	u32int x[Blockwords];
	int i;

	x[0] = s->input[0];
	x[1] = s->input[1];
	x[2] = s->input[2];
	x[3] = s->input[3];
	x[4] = s->input[4];
	x[5] = s->input[5];
	x[6] = s->input[6];
	x[7] = s->input[7];
	x[8] = s->input[8];
	x[9] = s->input[9];
	x[10] = s->input[10];
	x[11] = s->input[11];
	x[12] = s->input[12];
	x[13] = s->input[13];
	x[14] = s->input[14];
	x[15] = s->input[15];
	dorounds(x, s->rounds);

	for(i=0; i<nelem(x); i+=4){
		ENCRYPT(src, x[i], s->input[i], dst);
		ENCRYPT(src+4, x[i+1], s->input[i+1], dst+4);
		ENCRYPT(src+8, x[i+2], s->input[i+2], dst+8);
		ENCRYPT(src+12, x[i+3], s->input[i+3], dst+12);
		src += 16;
		dst += 16;
	}

	if(++s->input[12] == 0 && s->ivwords == 2)
		s->input[13]++;
}

void
chacha_encrypt2(uchar *src, uchar *dst, ulong bytes, Chachastate *s)
{
	uchar tmp[ChachaBsize];

	for(; bytes >= ChachaBsize; bytes -= ChachaBsize){
		encryptblock(s, src, dst);
		src += ChachaBsize;
		dst += ChachaBsize;
	}
	if(bytes > 0){
		memmove(tmp, src, bytes);
		encryptblock(s, tmp, tmp);
		memmove(dst, tmp, bytes);
	}
}

void
chacha_encrypt(uchar *buf, ulong bytes, Chachastate *s)
{
	chacha_encrypt2(buf, buf, bytes, s);
}
