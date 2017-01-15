#include "os.h"
#include <libsec.h>

/* little-endian data order */
#define	GET4(p)		((p)[0]|((p)[1]<<8)|((p)[2]<<16)|((p)[3]<<24))
#define	PUT4(p,v)	(p)[0]=(v);(p)[1]=(v)>>8;(p)[2]=(v)>>16;(p)[3]=(v)>>24

#define ROTATE(v,c) (t = v, (u32int)(t << (c)) | (t >> (32 - (c))))

#define ENCRYPT(s, x, y, d) {\
	u32int v; \
	v = GET4(s); \
	v ^= (x)+(y); \
	PUT4(d, v); \
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
setupSalsastate(Salsastate *s, uchar *key, ulong keylen, uchar *iv, ulong ivlen, int rounds)
{
	if(keylen != 256/8 && keylen != 128/8)
		sysfatal("invalid salsa key length");
	if(ivlen != 64/8
	&& ivlen != 128/8 && ivlen != 192/8)	/* hsalsa, xsalsa */
		sysfatal("invalid salsa iv length");
	if(rounds == 0)
		rounds = 20;
	s->rounds = rounds;
	if(keylen == 256/8) { /* recommended */
		load(&s->input[0],  sigma+4*0, 1);
		load(&s->input[1],  key +16*0, 4);
		load(&s->input[5],  sigma+4*1, 1);
		load(&s->input[10], sigma+4*2, 1);
		load(&s->input[11], key +16*1, 4);
		load(&s->input[15], sigma+4*3, 1);
	}else{
		load(&s->input[0],  tau +4*0, 1);
		load(&s->input[1],  key, 4);
		load(&s->input[5],  tau +4*1, 1);
		load(&s->input[10], tau +4*2, 1);
		load(&s->input[11], key, 4);
		load(&s->input[15], tau +4*3, 1);
	}
	s->xkey[0] = s->input[1];
	s->xkey[1] = s->input[2];
	s->xkey[2] = s->input[3];
	s->xkey[3] = s->input[4];
	s->xkey[4] = s->input[11];
	s->xkey[5] = s->input[12];
	s->xkey[6] = s->input[13];
	s->xkey[7] = s->input[14];

	s->ivwords = ivlen/4;
	s->input[8] = 0;
	s->input[9] = 0;
	if(iv == nil){
		s->input[6] = 0;
		s->input[7] = 0;
	}else
		salsa_setiv(s, iv);
}

static void
dorounds(u32int x[16], int rounds)
{
	u32int t;

	for(; rounds > 0; rounds -= 2) {
	     x[4] ^= ROTATE( x[0]+x[12], 7);
	     x[8] ^= ROTATE( x[4]+ x[0], 9);
	    x[12] ^= ROTATE( x[8]+ x[4],13);
	     x[0] ^= ROTATE(x[12]+ x[8],18);
	     x[9] ^= ROTATE( x[5]+ x[1], 7);
	    x[13] ^= ROTATE( x[9]+ x[5], 9);
	     x[1] ^= ROTATE(x[13]+ x[9],13);
	     x[5] ^= ROTATE( x[1]+x[13],18);
	    x[14] ^= ROTATE(x[10]+ x[6], 7);
	     x[2] ^= ROTATE(x[14]+x[10], 9);
	     x[6] ^= ROTATE( x[2]+x[14],13);
	    x[10] ^= ROTATE( x[6]+ x[2],18);
	     x[3] ^= ROTATE(x[15]+x[11], 7);
	     x[7] ^= ROTATE( x[3]+x[15], 9);
	    x[11] ^= ROTATE( x[7]+ x[3],13);
	    x[15] ^= ROTATE(x[11]+ x[7],18);
	     x[1] ^= ROTATE( x[0]+ x[3], 7);
	     x[2] ^= ROTATE( x[1]+ x[0], 9);
	     x[3] ^= ROTATE( x[2]+ x[1],13);
	     x[0] ^= ROTATE( x[3]+ x[2],18);
	     x[6] ^= ROTATE( x[5]+ x[4], 7);
	     x[7] ^= ROTATE( x[6]+ x[5], 9);
	     x[4] ^= ROTATE( x[7]+ x[6],13);
	     x[5] ^= ROTATE( x[4]+ x[7],18);
	    x[11] ^= ROTATE(x[10]+ x[9], 7);
	     x[8] ^= ROTATE(x[11]+x[10], 9);
	     x[9] ^= ROTATE( x[8]+x[11],13);
	    x[10] ^= ROTATE( x[9]+ x[8],18);
	    x[12] ^= ROTATE(x[15]+x[14], 7);
	    x[13] ^= ROTATE(x[12]+x[15], 9);
	    x[14] ^= ROTATE(x[13]+x[12],13);
	    x[15] ^= ROTATE(x[14]+x[13],18);
	}
}

static void
hsalsablock(uchar h[32], Salsastate *s)
{
	u32int x[16];

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

	PUT4(h+0*4, x[0]);
	PUT4(h+1*4, x[5]);
	PUT4(h+2*4, x[10]);
	PUT4(h+3*4, x[15]);
	PUT4(h+4*4, x[6]);
	PUT4(h+5*4, x[7]);
	PUT4(h+6*4, x[8]);
	PUT4(h+7*4, x[9]);
}

void
salsa_setiv(Salsastate *s, uchar *iv)
{
	if(s->ivwords == 128/32){
		/* hsalsa with 128-bit iv */
		load(&s->input[6], iv, 4);
		return;
	}
	if(s->ivwords == 192/32){
		/* xsalsa with 192-bit iv */
		u32int counter[2];
		uchar h[32];

		counter[0] = s->input[8];
		counter[1] = s->input[9];

		s->input[1] = s->xkey[0];
		s->input[2] = s->xkey[1];
		s->input[3] = s->xkey[2];
		s->input[4] = s->xkey[3];
		s->input[11] = s->xkey[4];
		s->input[12] = s->xkey[5];
		s->input[13] = s->xkey[6];
		s->input[14] = s->xkey[7];

		load(&s->input[6], iv, 4);

		hsalsablock(h, s);
		load(&s->input[1],  h+16*0, 4);
		load(&s->input[11], h+16*1, 4);
		memset(h, 0, 32);

		s->input[8] = counter[0];
		s->input[9] = counter[1];

		iv += 16;
	}
	/* 64-bit iv */
	load(&s->input[6], iv, 2);
}

void
salsa_setblock(Salsastate *s, u64int blockno)
{
	s->input[8] = blockno;
	s->input[9] = blockno>>32;
}

static void
encryptblock(Salsastate *s, uchar *src, uchar *dst)
{
	u32int x[16];
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

	if(++s->input[8] == 0)
		s->input[9]++;
}

void
salsa_encrypt2(uchar *src, uchar *dst, ulong bytes, Salsastate *s)
{
	uchar tmp[SalsaBsize];

	for(; bytes >= SalsaBsize; bytes -= SalsaBsize){
		encryptblock(s, src, dst);
		src += SalsaBsize;
		dst += SalsaBsize;
	}
	if(bytes > 0){
		memmove(tmp, src, bytes);
		encryptblock(s, tmp, tmp);
		memmove(dst, tmp, bytes);
	}
}

void
salsa_encrypt(uchar *buf, ulong bytes, Salsastate *s)
{
	salsa_encrypt2(buf, buf, bytes, s);
}

void
salsa_core(u32int in[16], u32int out[16], int rounds)
{
	u32int x[16];

	x[0] = in[0];
	x[1] = in[1];
	x[2] = in[2];
	x[3] = in[3];
	x[4] = in[4];
	x[5] = in[5];
	x[6] = in[6];
	x[7] = in[7];
	x[8] = in[8];
	x[9] = in[9];
	x[10] = in[10];
	x[11] = in[11];
	x[12] = in[12];
	x[13] = in[13];
	x[14] = in[14];
	x[15] = in[15];

	dorounds(x, rounds);

	out[0] = x[0] + in[0];
	out[1] = x[1] + in[1];
	out[2] = x[2] + in[2];
	out[3] = x[3] + in[3];
	out[4] = x[4] + in[4];
	out[5] = x[5] + in[5];
	out[6] = x[6] + in[6];
	out[7] = x[7] + in[7];
	out[8] = x[8] + in[8];
	out[9] = x[9] + in[9];
	out[10] = x[10] + in[10];
	out[11] = x[11] + in[11];
	out[12] = x[12] + in[12];
	out[13] = x[13] + in[13];
	out[14] = x[14] + in[14];
	out[15] = x[15] + in[15];
}

void
hsalsa(uchar h[32], uchar *key, ulong keylen, uchar nonce[16], int rounds)
{
	Salsastate s[1];

	setupSalsastate(s, key, keylen, nonce, 16, rounds);
	hsalsablock(h, s);
	memset(s, 0, sizeof(s));
}
