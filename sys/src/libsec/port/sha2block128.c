/*
 * sha2_512 block cipher - unrolled version
 *
 *   note: the following upper and lower case macro names are distinct
 *	   and reflect the functions defined in FIPS pub. 180-2.
 */

#include "os.h"

#define ROTR(x,n)	(((x) >> (n)) | ((x) << (64-(n))))
#define sigma0(x)	(ROTR((x),1) ^ ROTR((x),8) ^ ((x) >> 7))
#define sigma1(x)	(ROTR((x),19) ^ ROTR((x),61) ^ ((x) >> 6))
#define SIGMA0(x)	(ROTR((x),28) ^ ROTR((x),34) ^ ROTR((x),39))
#define SIGMA1(x)	(ROTR((x),14) ^ ROTR((x),18) ^ ROTR((x),41))
#define Ch(x,y,z)	((z) ^ ((x) & ((y) ^ (z))))
#define Maj(x,y,z)	(((x) | (y)) & ((z) | ((x) & (y))))

/*
 * first 64 bits of the fractional parts of cube roots of
 * first 80 primes (2..311).
 */
static u64int K512[80] = {
	0x428a2f98d728ae22LL, 0x7137449123ef65cdLL, 0xb5c0fbcfec4d3b2fLL, 0xe9b5dba58189dbbcLL,
	0x3956c25bf348b538LL, 0x59f111f1b605d019LL, 0x923f82a4af194f9bLL, 0xab1c5ed5da6d8118LL,
	0xd807aa98a3030242LL, 0x12835b0145706fbeLL, 0x243185be4ee4b28cLL, 0x550c7dc3d5ffb4e2LL,
	0x72be5d74f27b896fLL, 0x80deb1fe3b1696b1LL, 0x9bdc06a725c71235LL, 0xc19bf174cf692694LL,
	0xe49b69c19ef14ad2LL, 0xefbe4786384f25e3LL, 0x0fc19dc68b8cd5b5LL, 0x240ca1cc77ac9c65LL,
	0x2de92c6f592b0275LL, 0x4a7484aa6ea6e483LL, 0x5cb0a9dcbd41fbd4LL, 0x76f988da831153b5LL,
	0x983e5152ee66dfabLL, 0xa831c66d2db43210LL, 0xb00327c898fb213fLL, 0xbf597fc7beef0ee4LL,
	0xc6e00bf33da88fc2LL, 0xd5a79147930aa725LL, 0x06ca6351e003826fLL, 0x142929670a0e6e70LL,
	0x27b70a8546d22ffcLL, 0x2e1b21385c26c926LL, 0x4d2c6dfc5ac42aedLL, 0x53380d139d95b3dfLL,
	0x650a73548baf63deLL, 0x766a0abb3c77b2a8LL, 0x81c2c92e47edaee6LL, 0x92722c851482353bLL,
	0xa2bfe8a14cf10364LL, 0xa81a664bbc423001LL, 0xc24b8b70d0f89791LL, 0xc76c51a30654be30LL,
	0xd192e819d6ef5218LL, 0xd69906245565a910LL, 0xf40e35855771202aLL, 0x106aa07032bbd1b8LL,
	0x19a4c116b8d2d0c8LL, 0x1e376c085141ab53LL, 0x2748774cdf8eeb99LL, 0x34b0bcb5e19b48a8LL,
	0x391c0cb3c5c95a63LL, 0x4ed8aa4ae3418acbLL, 0x5b9cca4f7763e373LL, 0x682e6ff3d6b2b8a3LL,
	0x748f82ee5defb2fcLL, 0x78a5636f43172f60LL, 0x84c87814a1f0ab72LL, 0x8cc702081a6439ecLL,
	0x90befffa23631e28LL, 0xa4506cebde82bde9LL, 0xbef9a3f7b2c67915LL, 0xc67178f2e372532bLL,
	0xca273eceea26619cLL, 0xd186b8c721c0c207LL, 0xeada7dd6cde0eb1eLL, 0xf57d4f7fee6ed178LL,
	0x06f067aa72176fbaLL, 0x0a637dc5a2c898a6LL, 0x113f9804bef90daeLL, 0x1b710b35131c471bLL,
	0x28db77f523047d84LL, 0x32caab7b40c72493LL, 0x3c9ebe0a15c9bebcLL, 0x431d67c49c100d4cLL,
	0x4cc5d4becb3e42b6LL, 0x597f299cfc657e2aLL, 0x5fcb6fab3ad6faecLL, 0x6c44198c4a475817LL
};

void
_sha2block128(uchar *p, ulong len, u64int *s)
{
	u64int w[16], a, b, c, d, e, f, g, h;
	uchar *end;

	/* at this point, we have a multiple of 64 bytes */
	for(end = p+len; p < end;){
		a = s[0];
		b = s[1];
		c = s[2];
		d = s[3];
		e = s[4];
		f = s[5];
		g = s[6];
		h = s[7];

#define STEP(a,b,c,d,e,f,g,h,i) \
	if(i < 16) { \
		w[i] = 	(u64int)(p[0]<<24 | p[1]<<16 | p[2]<<8 | p[3])<<32 | \
			(p[4]<<24 | p[5]<<16 | p[6]<<8 | p[7]); \
		p += 8; \
	} else { \
		u64int s0, s1; \
		s1 = sigma1(w[i-2&15]); \
		s0 = sigma0(w[i-15&15]); \
		w[i&15] += s1 + w[i-7&15] + s0; \
	} \
	h += SIGMA1(e) + Ch(e,f,g) + K512[i] + w[i&15]; \
	d += h; \
	h += SIGMA0(a) + Maj(a,b,c);

		STEP(a,b,c,d,e,f,g,h,0);
		STEP(h,a,b,c,d,e,f,g,1);
		STEP(g,h,a,b,c,d,e,f,2);
		STEP(f,g,h,a,b,c,d,e,3);
		STEP(e,f,g,h,a,b,c,d,4);
		STEP(d,e,f,g,h,a,b,c,5);
		STEP(c,d,e,f,g,h,a,b,6);
		STEP(b,c,d,e,f,g,h,a,7);

		STEP(a,b,c,d,e,f,g,h,8);
		STEP(h,a,b,c,d,e,f,g,9);
		STEP(g,h,a,b,c,d,e,f,10);
		STEP(f,g,h,a,b,c,d,e,11);
		STEP(e,f,g,h,a,b,c,d,12);
		STEP(d,e,f,g,h,a,b,c,13);
		STEP(c,d,e,f,g,h,a,b,14);
		STEP(b,c,d,e,f,g,h,a,15);

		STEP(a,b,c,d,e,f,g,h,16);
		STEP(h,a,b,c,d,e,f,g,17);
		STEP(g,h,a,b,c,d,e,f,18);
		STEP(f,g,h,a,b,c,d,e,19);
		STEP(e,f,g,h,a,b,c,d,20);
		STEP(d,e,f,g,h,a,b,c,21);
		STEP(c,d,e,f,g,h,a,b,22);
		STEP(b,c,d,e,f,g,h,a,23);

		STEP(a,b,c,d,e,f,g,h,24);
		STEP(h,a,b,c,d,e,f,g,25);
		STEP(g,h,a,b,c,d,e,f,26);
		STEP(f,g,h,a,b,c,d,e,27);
		STEP(e,f,g,h,a,b,c,d,28);
		STEP(d,e,f,g,h,a,b,c,29);
		STEP(c,d,e,f,g,h,a,b,30);
		STEP(b,c,d,e,f,g,h,a,31);

		STEP(a,b,c,d,e,f,g,h,32);
		STEP(h,a,b,c,d,e,f,g,33);
		STEP(g,h,a,b,c,d,e,f,34);
		STEP(f,g,h,a,b,c,d,e,35);
		STEP(e,f,g,h,a,b,c,d,36);
		STEP(d,e,f,g,h,a,b,c,37);
		STEP(c,d,e,f,g,h,a,b,38);
		STEP(b,c,d,e,f,g,h,a,39);

		STEP(a,b,c,d,e,f,g,h,40);
		STEP(h,a,b,c,d,e,f,g,41);
		STEP(g,h,a,b,c,d,e,f,42);
		STEP(f,g,h,a,b,c,d,e,43);
		STEP(e,f,g,h,a,b,c,d,44);
		STEP(d,e,f,g,h,a,b,c,45);
		STEP(c,d,e,f,g,h,a,b,46);
		STEP(b,c,d,e,f,g,h,a,47);

		STEP(a,b,c,d,e,f,g,h,48);
		STEP(h,a,b,c,d,e,f,g,49);
		STEP(g,h,a,b,c,d,e,f,50);
		STEP(f,g,h,a,b,c,d,e,51);
		STEP(e,f,g,h,a,b,c,d,52);
		STEP(d,e,f,g,h,a,b,c,53);
		STEP(c,d,e,f,g,h,a,b,54);
		STEP(b,c,d,e,f,g,h,a,55);

		STEP(a,b,c,d,e,f,g,h,56);
		STEP(h,a,b,c,d,e,f,g,57);
		STEP(g,h,a,b,c,d,e,f,58);
		STEP(f,g,h,a,b,c,d,e,59);
		STEP(e,f,g,h,a,b,c,d,60);
		STEP(d,e,f,g,h,a,b,c,61);
		STEP(c,d,e,f,g,h,a,b,62);
		STEP(b,c,d,e,f,g,h,a,63);

		STEP(a,b,c,d,e,f,g,h,64);
		STEP(h,a,b,c,d,e,f,g,65);
		STEP(g,h,a,b,c,d,e,f,66);
		STEP(f,g,h,a,b,c,d,e,67);
		STEP(e,f,g,h,a,b,c,d,68);
		STEP(d,e,f,g,h,a,b,c,69);
		STEP(c,d,e,f,g,h,a,b,70);
		STEP(b,c,d,e,f,g,h,a,71);

		STEP(a,b,c,d,e,f,g,h,72);
		STEP(h,a,b,c,d,e,f,g,73);
		STEP(g,h,a,b,c,d,e,f,74);
		STEP(f,g,h,a,b,c,d,e,75);
		STEP(e,f,g,h,a,b,c,d,76);
		STEP(d,e,f,g,h,a,b,c,77);
		STEP(c,d,e,f,g,h,a,b,78);
		STEP(b,c,d,e,f,g,h,a,79);

		s[0] += a;
		s[1] += b;
		s[2] += c;
		s[3] += d;
		s[4] += e;
		s[5] += f;
		s[6] += g;
		s[7] += h;
	}
}
