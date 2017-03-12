#include "common.h"

void
usage(void)
{
	fprint(2, "usage: prflags\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *f[Fields+1], buf[20], *s;
	int n;
	Biobuf b, o;

	ARGBEGIN{
	default:
		usage();
	}ARGEND
	if(argc)
		usage();
	Binit(&b, 0, OREAD);
	Binit(&o, 1, OWRITE);

	for(; s = Brdstr(&b, '\n', 1); free(s)){
		n = gettokens(s, f, nelem(f), " ");
		if(n != Fields)
			continue;
		if(!strcmp(f[0], "-"))
			continue;
		Bprint(&o, "%s\n", flagbuf(buf, strtoul(f[1], 0, 16)));
	}
	Bterm(&b);
	Bterm(&o);
	exits("");
}
