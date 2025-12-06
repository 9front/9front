#include <u.h>
#include <libc.h>
#include <regexp.h>

Reprog *re;
Resub m[10];

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
	if(regexec(re, argv[1], m, n) <= 0)
		exits("no match");
	for(i = 0; i < n; i++) {
		if(m[i].sp == nil)
			print("(?");
		else
			print("(%d", (int)(m[i].sp - argv[1]));
		if(m[i].ep == nil)
			print(",?)");
		else
			print(",%d)", (int)(m[i].ep - argv[1]));
	}
	print("\n");
	exits(nil);
}
