#include <u.h>
#include <libc.h>
#include <bio.h>

static int
estrtoul(char *s)
{
	char *epr;
	Rune code;

	code = strtoul(s, &epr, 16);
	if(s == epr)
		sysfatal("bad code point hex string");
	return code;
}

void
main(int, char)
{
	Rune buffer1[64];
	Rune buffer2[64];
	char utfbuff1[128];
	char utfbuff2[128];
	char srctmp[128], tmp1[128], tmp2[128];
	char *fields[10];
	char *runes[32];
	char *p;
	int n, n2;
	int i;
	uint fail;
	Biobuf *b;

	b = Bopen("/lib/ucd/NormalizationTest.txt", OREAD);
	if(b == nil)
		sysfatal("could not load composition exclusions: %r");

	struct {
		Rune src[32];
		Rune nfc[32];
		Rune nfd[32];
	} test;
	while((p = Brdline(b, '\n')) != nil){
		p[Blinelen(b)-1] = 0;
		if(p[0] == 0 || p[0] == '#' || p[0] == '@')
			continue;
		getfields(p, fields, 6 + 1, 0, ";");
		n = getfields(fields[0], runes, nelem(runes), 0, " ");
		for(i = 0; i < n; i++)
			test.src[i] = estrtoul(runes[i]);
		test.src[i] = 0;

		n = getfields(fields[1], runes, nelem(runes), 0, " ");
		for(i = 0; i < n; i++)
			test.nfc[i] = estrtoul(runes[i]);
		test.nfc[i] = 0;

		n = getfields(fields[2], runes, nelem(runes), 0, " ");
		for(i = 0; i < n; i++)
			test.nfd[i] = estrtoul(runes[i]);
		test.nfd[i] = 0;

		n = runecomp(buffer1, test.src, nelem(buffer1));
		n2 = runedecomp(buffer2, test.src, nelem(buffer2));
		fail = 0;

		if(runestrcmp(buffer1, test.nfc) != 0)
			fail |= 1<<0;
		if(runestrcmp(buffer2, test.nfd) != 0)
			fail |= 1<<1;
		if(fail)
			print("%d %d %S %S %S %S %S\n", fail, i, test.src, test.nfd, test.nfc, buffer2, buffer1);
		assert(n == runestrlen(test.nfc));
		assert(n2 == runestrlen(test.nfd));

		snprint(srctmp, sizeof tmp1, "%S", test.src);
		snprint(tmp1, sizeof tmp1, "%S", test.nfc);
		snprint(tmp2, sizeof tmp2, "%S", test.nfd);

		n = utfcomp(utfbuff1, srctmp, nelem(utfbuff1));
		n2 = utfdecomp(utfbuff2, srctmp, nelem(utfbuff2));

		if(strcmp(utfbuff1, tmp1) != 0)
			fail |= 1<<2;
		if(strcmp(utfbuff2, tmp2) != 0)
			fail |= 1<<3;
		if(fail)
			print("%d %d %s %s %s %s %s\n", fail, i, srctmp, tmp2, tmp1, utfbuff2, utfbuff1);
		assert(n == strlen(tmp1));
		assert(n2 == strlen(tmp2));
	}
	exits(nil);
}
