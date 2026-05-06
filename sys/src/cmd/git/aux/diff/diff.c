#include <u.h>
#include <libc.h>
#include <bio.h>
#include "diff.h"

void	
done(int status)
{
	switch(status)
	{
	case 0:
		exits("");
	case 1:
		exits("some");
	default:
		exits("error");
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-abcefmnrw] file1 ... file2\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	int i;
	Dir *fsb, *tsb;

	Binit(&stdout, 1, OWRITE);
	ARGBEGIN{
	case 'e':
	case 'f':
	case 'n':
	case 'c':
	case 'a':
	case 'u':
		mode = ARGC();
		break;
	case 'w':
		bflag = 2;
		break;

	case 'b':
		bflag = 1;
		break;

	case 'r':
		rflag = 1;
		break;

	case 'm':
		mflag = 1;	
		break;

	case 'h':
	default:
		usage();
	}ARGEND;
	if (argc < 2)
		usage();
	if ((tsb = dirstat(argv[argc-1])) == nil)
		sysfatal("can't stat %s", argv[argc-1]);
	if (argc > 2) {
		if (!DIRECTORY(tsb))
			sysfatal("not directory: %s", argv[argc-1]);
		mflag = 1;
	} else {
		if ((fsb = dirstat(argv[0])) == nil)
			sysfatal("can't stat %s", argv[0]);
		if (DIRECTORY(fsb) && DIRECTORY(tsb))
			mflag = 1;
		free(fsb);
	}
	free(tsb);
	for (i = 0; i < argc-1; i++)
		diff(argv[i], argv[argc-1], 0);
		
	done(anychange);
	/*NOTREACHED*/
}
