#include "os.h"
#include <mp.h>
#include <libsec.h>

uchar key[] = "Jefe";
uchar data[] = "what do ya want for nothing?";

static struct {
	DigestState* (*fn)(uchar *data, ulong dlen, uchar *key, ulong klen, uchar *digest, DigestState *state);
	int dlen;
	char *out;
} tests[] = {
	hmac_md5, MD5dlen, "750c783e6ab0b503eaa86e310a5db738",
	hmac_sha2_224, SHA2_224dlen, "a30e01098bc6dbbf45690f3a7e9e6d0f8bbea2a39e6148008fd05e44",
	hmac_sha2_256, SHA2_256dlen, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
	hmac_sha2_384, SHA2_384dlen, "af45d2e376484031617f78d2b58a6b1b9c7ef464f5a01b47e42ec3736322445e8e2240ca5e69e2c78b3239ecfab21649",
	hmac_sha2_512, SHA2_512dlen, "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737",
	hmac_sha3_224, SHA3_224dlen, "7fdb8dd88bd2f60d1b798634ad386811c2cfc85bfaf5d52bbace5e66",
	hmac_sha3_256, SHA3_256dlen, "c7d4072e788877ae3596bbb0da73b887c9171f93095b294ae857fbe2645e1ba5",
	hmac_sha3_384, SHA3_384dlen, "f1101f8cbf9766fd6764d2ed61903f21ca9b18f57cf3e1a23ca13508a93243ce48c045dc007f26a21b3f5e0e9df4c20a",
	hmac_sha3_512, SHA3_512dlen, "5a4bfeab6166427c7a3647b747292b8384537cdb89afb3bf5665e4c5e709350b287baec921fd7ca0ee7a0c31d022a95e1fc92ba9d77df883960275beb4e62024",
};

void
main(void)
{
	uchar hash[1024];
	char buf[256];
	int i;

	fmtinstall('H', encodefmt);
	for(i = 0; i < nelem(tests); i++){
		tests[i].fn(data, strlen((char*)data), key, 4, hash, nil);
		snprint(buf, sizeof buf, "%.*lH", tests[i].dlen, hash);
		if(strcmp(buf, tests[i].out) != 0){
			print("Exp: %s\n", tests[i].out);
			print("Got: %s\n", buf);
			exits("fail");
		}
	}
	exits(nil);
}
