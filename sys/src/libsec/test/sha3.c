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
	DigestState*(*x)(uchar*, ulong, uchar*, ulong, DigestState*);
	int dlen, olen;
	char *in, *out;
} shake[] = {
	{
		shake_128, SHAKE_128dlen,
		SHAKE_128dlen,
		"84e950051876050dc851fbd99e6247b8",
		"8599bd89f63a848c49ca593ec37a12c6",
	},
	{
		shake_128, SHAKE_128dlen,
		(128+16+64)/8,
		"bf8594f322de3d179722d182273f51ba",
		"c2e5b8946c6c73739678a3dbca41a8a615a0967773a51d2fb387",
	},

	{
		shake_256, SHAKE_256dlen,
		2,
		"c61a9188812ae73994bc0d6d4021e31bf124dc72669749111232da7ac29e61c4",
		"23ce",
	},
	{
		shake_256, SHAKE_256dlen,
		8,
		"dc886df3f69c49513de3627e9481db5871e8ee88eb9f99611541930a8bc885e0",
		"00648afbc5e65164",
	},
	{
		shake_256, SHAKE_256dlen,
		SHAKE_256dlen,
		"dc886df3f69c49513de3627e9481db5871e8ee88eb9f99611541930a8bc885e0",
		"00648afbc5e651649db1fd82936b00dbbc122fb4c877860d385c4950d56de7e0",
	},
	{
		shake_256, SHAKE_256dlen,
		(256+16+64)/8,
		"dc886df3f69c49513de3627e9481db5871e8ee88eb9f99611541930a8bc885e0",
		"00648afbc5e651649db1fd82936b00dbbc122fb4c877860d385c4950d56de7e096d613d7a3f27ed8f263",
	},
};

void
main(void)
{
	int i, j, k, n;
	DigestState s;

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
		shake[i].x(buf, n, digest, shake[i].olen, 0);
		snprint((char*)buf, sizeof buf, "%.*lH", shake[i].olen, digest);
		if(strcmp((char*)buf, shake[i].out) != 0){
			fprint(2, "Shake: shake_%d(%s)\nExp: %s\nGot: %s\n\n", shake[i].dlen*8, shake[i].in, shake[i].out, (char*)buf);
			sysfatal("fail");
		}
	}
	exits(nil);
}
