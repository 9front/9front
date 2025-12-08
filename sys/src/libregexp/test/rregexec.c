#include <u.h>
#include <libc.h>
#include <regexp.h>

Reprog *re;
Resub m[10];
Rune buf[8192];

void
usage(void)
{
	fprint(2, "usage: %s pattern string [nsub]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	int i, n;
	char *cp;

	ARGBEGIN {
	} ARGEND;

	if(argc < 2)
		usage();
	re = regcomp(argv[0]);
	if(re == nil)
		sysfatal("regcomp");
	n = nelem(m);
	if(argc == 3)
		n = atoi(argv[2]);
	if(n > nelem(m))
		sysfatal("too many substitutions");

	/* convert to rune buffer */
	cp = argv[1];
	for(i = 0; *cp != '\0'; i++){
		if(i >= nelem(buf))
			sysfatal("string too long");
		cp += chartorune(&buf[i], cp);
	}

	if(rregexec(re, buf, m, n) <= 0)
		exits("no match");
	for(i = 0; i < n; i++) {
		if(m[i].rsp == nil)
			print("(?");
		else
			print("(%d", (int)(m[i].rsp - buf));
		if(m[i].rep == nil)
			print(",?)");
		else
			print(",%d)", (int)(m[i].rep - buf));
	}
	print("\n");
	exits(nil);
}
