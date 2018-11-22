#include <u.h>
#include <libc.h>
#include <bio.h>
#include "snap.h"

void
usage(void)
{
	fprint(2, "usage: %s [-d] [-o snapfile] pid...\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *user, *sys, *arch, *term, *ofile;
	int i;
	long pid, me;
	Biobuf *b;
	Dir *d;
	Proc *p;

	ofile = "/fd/1";
	ARGBEGIN{
	case 'd':
		debug++;
		break;
	case 'o':
		ofile = ARGF();
		break;
	default:
		usage();
	}ARGEND;

	if(argc < 1)
		usage();

	/* get kernel compilation time */
	if((d = dirstat("#/")) == nil)
		sysfatal("cannot stat #/: %r");

	if((b = Bopen(ofile, OWRITE)) == nil)
		sysfatal("cannot write to \"%s\": %r", ofile);

	if((user = getuser()) == nil)
		user = "gre";
	if((sys = sysname()) == nil)
		sys = "gnot";
	if((arch = getenv("cputype")) == nil)
		arch = "unknown";
	if((term = getenv("terminal")) == nil)
		term = "unknown terminal type";

	Bprint(b, "process snapshot %ld %s@%s %s %ld \"%s\"\n",
		time(0), user, sys, arch, d->mtime, term);
	me = getpid();
	for(i=0; i<argc; i++) {
		if((pid = atol(argv[i])) == me)
			fprint(2, "warning: will not snapshot self\n");
		else if(p = snap(pid, 1))
			writesnap(b, p);
	}
	exits(0);
}
