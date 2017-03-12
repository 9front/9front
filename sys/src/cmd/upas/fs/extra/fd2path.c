#include <u.h>
#include <libc.h>

void
usage(void)
{
	fprint(2, "usage: fd2path path ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char buf[1024];
	int fd;

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(argc == 0){
		if(fd2path(0, buf, sizeof buf) != -1)
			fprint(2, "%s\n", buf);
	}else for(; *argv; argv++){
		fd = open(*argv, OREAD);
		if(fd != -1 && fd2path(fd, buf, sizeof buf) != -1)
			fprint(2, "%s\n", buf);
		close(fd);
	}
	exits("");
}
