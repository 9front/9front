#include "dat.h"
#include "common.h"

void
usage(void)
{
	fprint(2, "usage: mbcreate [-f] ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int r;
	int (*f)(char*, char*);

	f = creatembox;
	ARGBEGIN{
	case 'f':
		f = createfolder;
		break;
	default:
		usage();
	}ARGEND

	r = 0;
	tmfmtinstall();
	for(; *argv; argv++)
		r |= f(getuser(), *argv);
	if(r)
		exits("errors");
	exits("");
}
