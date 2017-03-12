#include "spf.h"

char	dflag;
char	vflag;
char	*netroot = "/net";

void
usage(void)
{
	fprint(2, "usage: mtest [-dv] sender dom hello ip\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *a[5], *s;
	int i;

	ARGBEGIN{
	case 'd':
		dflag = 1;
		break;
	case 'v':
		vflag = 1;
		break;
	default:
		usage();
	}ARGEND

	fmtinstall('I', eipfmt);
	memset(a, 0, sizeof a);
	for(i = 0; i < argc && i < nelem(a); i++)
		a[i] = argv[i];
	s = macro(a[0], a[1], a[2], a[3], a[4]);
	print("%s\n", s);
	free(s);
	exits("");
}
