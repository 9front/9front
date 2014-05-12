#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <html.h>
#include "dat.h"

char *url = "";
int aflag;
int width = 70;
char *defcharset = "latin1";

int
uhtml(int fd)
{
	int p[2];

	if(pipe(p) < 0)
		return fd;
	switch(fork()){
	case -1:
		break;
	case 0:
		dup(fd, 0);
		dup(p[1], 1);
		close(p[1]);
		close(p[0]);
		execl("/bin/uhtml", "uhtml", "-c", defcharset, nil);
		execl("/bin/cat", "cat", nil);
		exits("exec");
	default:
		dup(p[0], fd);
		break;
	}
	close(p[0]);
	close(p[1]);
	return fd;
}

void
usage(void)
{
	fprint(2, "usage: htmlfmt [-c charset] [-u URL] [-a] [-l length] [file ...]\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{
	int i, fd;
	char *err, *file;
	char errbuf[ERRMAX];

	ARGBEGIN{
	case 'a':
		aflag++;
		break;
	case 'c':
		defcharset = EARGF(usage());
		break;
	case 'l': case 'w':
		err = EARGF(usage());
		width = atoi(err);
		if(width <= 0)
			usage();
		break;
	case 'u':
		url = EARGF(usage());
		aflag++;
		break;
	default:
		usage();
	}ARGEND

	err = nil;
	file = "<stdin>";
	if(argc == 0)
		err = loadhtml(uhtml(0));
	else
		for(i=0; err==nil && i<argc; i++){
			file = argv[i];
			fd = open(file, OREAD);
			if(fd < 0){
				errstr(errbuf, sizeof errbuf);
				err = errbuf;
				break;
			}
			err = loadhtml(uhtml(fd));
			close(fd);
			if(err)
				break;
		}
	if(err)
		fprint(2, "htmlfmt: processing %s: %s\n", file, err);
	exits(err);
}
