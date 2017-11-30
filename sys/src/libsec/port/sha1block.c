#include "os.h"

#define ROTL(x,n)	(((x)<<n)|((x)>>32-n))

#define F0(x,y,z)	(0x5a827999 + ((z) ^ ((x) & ((y) ^ (z)))))
#define F1(x,y,z)	(0x6ed9eba1 + ((x) ^ (y) ^ (z)))
#define F2(x,y,z)	(0x8f1bbcdc + (((x) & (y)) | (((x) | (y)) & (z))))
#define F3(x,y,z)	(0xca62c1d6 + ((x) ^ (y) ^ (z)))

void
_sha1block(uchar *p, ulong len, u32int *s)
{
	u32int w[16], a, b, c, d, e;
	uchar *end;

	/* at this point, we have a multiple of 64 bytes */
	for(end = p+len; p < end;){
		a = s[0];
		b = s[1];
		c = s[2];
		d = s[3];
		e = s[4];

#define STEP(a,b,c,d,e,f,i) \
	if(i < 16) {\
		w[i] = p[0]<<24 | p[1]<<16 | p[2]<<8 | p[3]; \
		p += 4; \
	} else { \
		u32int x = w[i-3&15] ^ w[i-8&15] ^ w[i-14&15] ^ w[i-16&15]; \
		w[i&15] = ROTL(x, 1); \
	} \
	e += ROTL(a, 5) + w[i&15] + f(b,c,d); \
	b = ROTL(b, 30);

		STEP(a,b,c,d,e,F0,0);
		STEP(e,a,b,c,d,F0,1);
		STEP(d,e,a,b,c,F0,2);
		STEP(c,d,e,a,b,F0,3);
		STEP(b,c,d,e,a,F0,4);
	
		STEP(a,b,c,d,e,F0,5);
		STEP(e,a,b,c,d,F0,6);
		STEP(d,e,a,b,c,F0,7);
		STEP(c,d,e,a,b,F0,8);
		STEP(b,c,d,e,a,F0,9);
	
		STEP(a,b,c,d,e,F0,10);
		STEP(e,a,b,c,d,F0,11);
		STEP(d,e,a,b,c,F0,12);
		STEP(c,d,e,a,b,F0,13);
		STEP(b,c,d,e,a,F0,14);
	
		STEP(a,b,c,d,e,F0,15);
		STEP(e,a,b,c,d,F0,16);
		STEP(d,e,a,b,c,F0,17);
		STEP(c,d,e,a,b,F0,18);
		STEP(b,c,d,e,a,F0,19);
	
		STEP(a,b,c,d,e,F1,20);
		STEP(e,a,b,c,d,F1,21);
		STEP(d,e,a,b,c,F1,22);
		STEP(c,d,e,a,b,F1,23);
		STEP(b,c,d,e,a,F1,24);
	
		STEP(a,b,c,d,e,F1,25);
		STEP(e,a,b,c,d,F1,26);
		STEP(d,e,a,b,c,F1,27);
		STEP(c,d,e,a,b,F1,28);
		STEP(b,c,d,e,a,F1,29);
	
		STEP(a,b,c,d,e,F1,30);
		STEP(e,a,b,c,d,F1,31);
		STEP(d,e,a,b,c,F1,32);
		STEP(c,d,e,a,b,F1,33);
		STEP(b,c,d,e,a,F1,34);
	
		STEP(a,b,c,d,e,F1,35);
		STEP(e,a,b,c,d,F1,36);
		STEP(d,e,a,b,c,F1,37);
		STEP(c,d,e,a,b,F1,38);
		STEP(b,c,d,e,a,F1,39);
	
		STEP(a,b,c,d,e,F2,40);
		STEP(e,a,b,c,d,F2,41);
		STEP(d,e,a,b,c,F2,42);
		STEP(c,d,e,a,b,F2,43);
		STEP(b,c,d,e,a,F2,44);
	
		STEP(a,b,c,d,e,F2,45);
		STEP(e,a,b,c,d,F2,46);
		STEP(d,e,a,b,c,F2,47);
		STEP(c,d,e,a,b,F2,48);
		STEP(b,c,d,e,a,F2,49);
	
		STEP(a,b,c,d,e,F2,50);
		STEP(e,a,b,c,d,F2,51);
		STEP(d,e,a,b,c,F2,52);
		STEP(c,d,e,a,b,F2,53);
		STEP(b,c,d,e,a,F2,54);
	
		STEP(a,b,c,d,e,F2,55);
		STEP(e,a,b,c,d,F2,56);
		STEP(d,e,a,b,c,F2,57);
		STEP(c,d,e,a,b,F2,58);
		STEP(b,c,d,e,a,F2,59);
	
		STEP(a,b,c,d,e,F3,60);
		STEP(e,a,b,c,d,F3,61);
		STEP(d,e,a,b,c,F3,62);
		STEP(c,d,e,a,b,F3,63);
		STEP(b,c,d,e,a,F3,64);
	
		STEP(a,b,c,d,e,F3,65);
		STEP(e,a,b,c,d,F3,66);
		STEP(d,e,a,b,c,F3,67);
		STEP(c,d,e,a,b,F3,68);
		STEP(b,c,d,e,a,F3,69);
	
		STEP(a,b,c,d,e,F3,70);
		STEP(e,a,b,c,d,F3,71);
		STEP(d,e,a,b,c,F3,72);
		STEP(c,d,e,a,b,F3,73);
		STEP(b,c,d,e,a,F3,74);
	
		STEP(a,b,c,d,e,F3,75);
		STEP(e,a,b,c,d,F3,76);
		STEP(d,e,a,b,c,F3,77);
		STEP(c,d,e,a,b,F3,78);
		STEP(b,c,d,e,a,F3,79);

		s[0] += a;
		s[1] += b;
		s[2] += c;
		s[3] += d;
		s[4] += e;
	}
}
