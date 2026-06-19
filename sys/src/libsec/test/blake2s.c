#include <u.h>
#include <libc.h>
#include <libsec.h>

struct {
	char *in, *exp, *key;
} tests[] = {
	{ /* empty input */
		"",
		"69217a3079908094e11121d042354a7c1f55b6482ca1a51e1b250dfd1ed0eef9",
		"",
	},
	{ /* reference hash */
		"abc",
		"508c5e8c327c14e2e1a72ba34eeb452f37458b209ed63a294d999b4c86675982",
		"",
	},
	{ /* exactly 1 block */
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
		"f85b88e0ac55872416d202c5f4881e7dbc9c7270542ef75074ff9b0a610b5a0e",
		"",
	},
	{ /* exactly 2 blocks */
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
		"ea263e84e451e17ff77d642cd7a751757765aded33d62b96f1e998af31024e30",
		"",
	},
	{ /* 1 block and some change */
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
		"c4b49a77ee46b6c166d56157131d1ec182153d0004428d6ac011edc942becd93",
		"",
	},
	{ /* 2 blocks and some change */
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
		"cae0fc9b9f296425db4a4af96f83e947649e78954f4081ed72ebdfdfb29cfca4",
		"",
	},
	{ /* 3 blocks and some change */
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
		"1b066f374e7e2d95a64643c5da9e1b89eab1c202cd5c17e27bc061ba3bbdc24a",
		"",
	},
	{ /* empty input with key */
		"",
		"b929086ee1f00ca75c05c4deb8eee28b174c6ba98b52b573a6b017db769a125c",
		"9",
	},
	{ /* reference input with key */
		"abc",
		"e6ee9e3d8b855f2b6a78a072f1a4e14226e7e6b15072681a965236f2b5405aad",
		"9",
	},
	{ /* empty input, 16 byte output */
		"",
		"64550d6ffe2c0a01a14aba1eade0200c",
		"",
	},
	{ /* reference input, 16 byte output */
		"abc",
		"aa4938119b1dc7b87cbad0ffd200d0ae",
		"",
	},
	{ /* empty input with key, 16 byte output */
		"",
		"4487d3ac1ad5879bd10ae24bac221455",
		"9",
	},
	{ /* reference input with key, 16 byte output */
		"abc",
		"0489aca3bac800d661ac5ba911212150",
		"9",
	},
};

void
main(int, char**)
{
	DigestState *s;
	uchar digest[BLAKE2S_256dlen];
	char buf[256];
	int i;
	char *p;
	DigestState* (*hash)(uchar*, ulong, uchar*, ulong, uchar*, DigestState*);
	int len;

	fmtinstall('H', encodefmt);
	for(i = 0; i < nelem(tests); i++){
		switch(strlen(tests[i].exp)){
		case 64:
			hash = mac_blake2s_256;
			len = BLAKE2S_256dlen;
			break;
		case 32:
			hash = mac_blake2s_128;
			len = BLAKE2S_128dlen;
			break;
		default:
			sysfatal("invalid test case");
		}
		/* do two calls to test storage in the DigestState */
		s = hash((uchar*)tests[i].in, strlen(tests[i].in), (uchar*)tests[i].key, strlen(tests[i].key), nil, nil);
		hash(nil, 0, nil, 0, digest, s);
		snprint(buf, sizeof buf, "%.*lH", len, digest);
		if(strcmp(buf, tests[i].exp) != 0){
			fprint(2, "Test: %s\nExp: %s\nGot: %s\n\n", tests[i].in, tests[i].exp, buf);
			exits("fail");
		}

		s = nil;
		memset(digest, 0, BLAKE2S_256dlen);
		for(p = tests[i].in; *p != 0; p++)
			s = hash((uchar*)p, 1, (uchar*)tests[i].key, strlen(tests[i].key), nil, s);

		hash(nil, 0, (uchar*)tests[i].key, strlen(tests[i].key), digest, s);
		snprint(buf, sizeof buf, "%.*lH", len, digest);
		if(strcmp(buf, tests[i].exp) != 0){
			fprint(2, "Trickle Test: %s\nExp: %s\nGot: %s\n\n", tests[i].in, tests[i].exp, buf);
			exits("fail");
		}
	}
	exits(nil);
}
