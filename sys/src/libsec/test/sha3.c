#include <u.h>
#include <libc.h>
#include "libsec.h"

#include "sha3vectors"

uchar buf[32*1024];
uchar digest[2048];

/*
 * same core block function so we want to check all the various output cases
 * so block aligned, word aligned, and remaining bytes
 */
static struct {
	void (*x)(uchar*, ulong, uchar*, ulong);
	DigestState* (*feed)(uchar*, ulong, DigestState*);
	XOFState* (*conv)(XOFState*, DigestState*);
	void (*burp)(uchar*, ulong, XOFState*);
	int dlen, olen;
	char *in, *out;
} shake[] = {
	{
		shake_128, shake_128_in, shake_128_conv, shake_128_out, SHAKE_128dlen,
		SHAKE_128dlen,
		"84e950051876050dc851fbd99e6247b8",
		"8599bd89f63a848c49ca593ec37a12c6",
	},
	{
		shake_128, shake_128_in, shake_128_conv, shake_128_out, SHAKE_128dlen,
		(128+16+64)/8,
		"bf8594f322de3d179722d182273f51ba",
		"c2e5b8946c6c73739678a3dbca41a8a615a0967773a51d2fb387",
	},

	{
		shake_256, shake_256_in, shake_256_conv, shake_256_out, SHAKE_256dlen,
		2,
		"c61a9188812ae73994bc0d6d4021e31bf124dc72669749111232da7ac29e61c4",
		"23ce",
	},
	{
		shake_256, shake_256_in, shake_256_conv, shake_256_out, SHAKE_256dlen,
		8,
		"dc886df3f69c49513de3627e9481db5871e8ee88eb9f99611541930a8bc885e0",
		"00648afbc5e65164",
	},
	{
		shake_256, shake_256_in, shake_256_conv, shake_256_out, SHAKE_256dlen,
		SHAKE_256dlen,
		"dc886df3f69c49513de3627e9481db5871e8ee88eb9f99611541930a8bc885e0",
		"00648afbc5e651649db1fd82936b00dbbc122fb4c877860d385c4950d56de7e0",
	},
	{
		shake_256, shake_256_in, shake_256_conv, shake_256_out, SHAKE_256dlen,
		(256+16+64)/8,
		"dc886df3f69c49513de3627e9481db5871e8ee88eb9f99611541930a8bc885e0",
		"00648afbc5e651649db1fd82936b00dbbc122fb4c877860d385c4950d56de7e096d613d7a3f27ed8f263",
	},
	{
		shake_128, shake_128_in, shake_128_conv, shake_128_out, SHAKE_128dlen,
		504,
		"F2D1426CA68A88DE542FA4441AD5D38264486853590E7F723F91F20C040B49550000",
		"6dee7d55378ae97aabaaddab134435d49160b1b3b35889d0fb0bf591b4456c982f4535d31e732c4e026f59821655797c0315a41a8ddf1e03ad43faca1b37e1eab4b373f6be9e9bc6d9d6c6f14c7b0d52e6791890f251446e09c4c98c4b39fe422e9f67799573aa3151576d15daadc792c84d4719a3fb462c0e39c8644308ae87535f44e1d1b6f987fee29f99dc2d6b3af5265ed9ad1ab7195bbcca195129266a3901e1e4f8ee5165e12ff1854b0358a6a9b4ee275b4e2e28f0cfe89be014202f272f93149d7c19a46568b55d6a3b4b0b469f545e564b1915ac4d52e3d5b726c8b157ec6a19550800f3264fbdafd70a01a1cc6500cbb7fffa212667459354ce5ed844c7f02b3a9c0b86b57888b4fbc2bb6415db48dcbe3cf6c34ae7a7f5bd68246b9462e48697971c7a978e2e1052a6bd61b3a2c9b3b2b5e026ad48541684e509d8db608b78eb3d0e28bbc3aadfa2c63c8f705043ec969cee93afed542f92f6a32dc65e816cab7fd4de6bad2caaff5676fc79d498341f6528a316708f69794ef7c14bce28549bf75e03784347ffdcd11729b261400186eed8ce57cf9aa2d4b7874f39f5c08cfcf779e76ef1929606ed99c1a673af2e3f2dfa0bf9a54e5370e98ac1324a8716c77b7a6efcd1e8aca2321c8c36f89dacca2685af94b16db3339f1a852d9244b3d15732b6ad7e648a87b55b29065a83d1d063bf",
	},
};

void
main(void)
{
	int i, j, k, n;
	DigestState s;
	XOFState x;

	fmtinstall('H', encodefmt);
	for(i = 0; i < nelem(tests); i++){
		memset(buf, 0, sizeof buf);
		n = dec16(buf, sizeof buf, tests[i].in, strlen(tests[i].in));
		if(n < 0)
			sysfatal("%r");
		tests[i].x(buf, n, digest, 0);
		snprint((char*)buf, sizeof buf, "%.*lH", tests[i].len, digest);
		if(strcmp((char*)buf, tests[i].out) != 0){
			fprint(2, "Test: sha3_%d(%s)\nExp: %s\nGot: %s\n\n", tests[i].len*8, tests[i].in, tests[i].out, (char*)buf);
			sysfatal("fail");
		}
	}

	for(i = 0; i < nelem(monte); i++){
		memset(buf, 0, sizeof buf);
		n = dec16(buf, sizeof buf, monte[i].seed, strlen(monte[i].seed));
		if(n < 0)
			sysfatal("%r");
		memset(&s, 0, sizeof s);
		monte[i].x(buf, n, digest, &s);
		for(j = 0; j < 100; j++){
			for(k = 1; k < 1001; k++){
				memset(&s, 0, sizeof s);
				if(k&1)
					monte[i].x(digest, monte[i].len, &digest[SHA3_512dlen], &s);
				else
					monte[i].x(&digest[SHA3_512dlen], monte[i].len, digest, &s);
			}
			snprint((char*)buf, sizeof buf, "%.*lH", monte[i].len, &digest[SHA3_512dlen]);
			if(strcmp((char*)buf, monte[i].checkp[j]) != 0){
				fprint(2, "Monte: sha3_%d(%d)\nExp: %s\nGot: %s\n\n", monte[i].len*8, j, monte[i].checkp[j], (char*)buf);
				sysfatal("fail");
			}
		}
	}
	for(i = 0; i < nelem(shake); i++){
		n = dec16(buf, sizeof buf, shake[i].in, strlen(shake[i].in));
		if(n < 0)
			sysfatal("%r");
		shake[i].x(buf, n, digest, shake[i].olen);
		snprint((char*)buf, sizeof buf, "%.*lH", shake[i].olen, digest);
		if(strcmp((char*)buf, shake[i].out) != 0){
			fprint(2, "Shake: shake_%d(%s)\nExp: %s\nGot: %s\n\n", shake[i].dlen*8, shake[i].in, shake[i].out, (char*)buf);
			sysfatal("fail");
		}
	}
	for(i = 0; i < nelem(shake); i++){
		n = dec16(buf, sizeof buf, shake[i].in, strlen(shake[i].in));
		if(n < 0)
			sysfatal("%r");
		memset(&s, 0, sizeof s);
		shake[i].feed(buf, n, &s);

		memset(&x, 0, sizeof x);
		shake[i].conv(&x, &s);

		memset(digest, 0, shake[i].olen);
		for(j = 0; j < shake[i].olen; j++)
			shake[1].burp(digest + j, 1, &x);
		snprint((char*)buf, sizeof buf, "%.*lH", shake[i].olen, digest);
		if(strcmp((char*)buf, shake[i].out) != 0){
			fprint(2, "Shake Trickle: shake_%d(%s)\nExp: %s\nGot: %s\n\n", shake[i].dlen*8, shake[i].in, shake[i].out, (char*)buf);
			sysfatal("fail");
		}
	}
	exits(nil);
}
