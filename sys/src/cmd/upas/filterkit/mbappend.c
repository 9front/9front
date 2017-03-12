/*
 * deliver to one's own folder with locking & logging
 */
#include "dat.h"
#include "common.h"

void
append(int fd, char *mb, char *from, long t)
{
	char *folder, *s;
	int r;

	s = ctime(t);
	folder = foldername(from, getuser(), mb);
	r = fappendfolder(0, t, folder, fd);
	if(r == 0)
		werrstr("");
	syslog(0, "mail", "mbappend %s %.28s (%s) %r", mb, s, folder);
	if(r)
		exits("fail");
}

void
usage(void)
{
	fprint(2, "usage: mbappend [-t time] [-f from] mbox [file ...]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *mb, *from;
	int fd;
	long t;

	from = nil;
	t = time(0);
	ARGBEGIN{
	case 't':
		t = strtoul(EARGF(usage()), 0, 0);
		break;
	case 'f':
		from = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;
	if(*argv == 0)
		usage();
	werrstr("");
	mb = *argv++;
	if(*argv == 0)
		append(0, mb, from, t);
	else for(; *argv; argv++){
		fd = open(*argv, OREAD);
		if(fd < 0)
			sysfatal("open: %r");
		append(fd, mb, from, t);
		close(fd);
	}
	exits("");
}
