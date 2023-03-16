#include <u.h>
#include <libc.h>
#include <auth.h>

extern int newnsdebug;

char	*defargv[] = { "/bin/rc", "-i", nil };
char	*namespace = nil;

void
usage(void)
{
	fprint(2, "usage: %s [-d] [-n namespace] [cmd [args...]]\n", argv0);
	exits("usage");
}

void
run(char **a)
{
	exec(a[0], a);

	if(a[0][0] != '/' && a[0][0] != '#' &&
	  (a[0][0] != '.' || (a[0][1] != '/' &&
		             (a[0][1] != '.' ||  a[0][2] != '/'))))
		exec(smprint("/bin/%s", a[0]), a);

	sysfatal("exec: %s: %r", a[0]);
}

void
main(int argc, char *argv[])
{
	ARGBEGIN{
	case 'd':
		newnsdebug = 1;
		break;
	case 'n':
		namespace = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if(procsetuser("none") < 0)
		sysfatal("can't become none: %r");
	if(newns("none", namespace) < 0)
		sysfatal("can't build namespace: %r");

	if(argc == 0)
		argv = defargv;

	run(argv);
}
