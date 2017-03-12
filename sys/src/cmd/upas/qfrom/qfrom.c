/*
 * quote from lines without messing with character encoding.
 *	(might rather just undo the character encoding and use sed.)
 */

#include <u.h>
#include <libc.h>
#include <bio.h>

void
qfrom(int fd)
{
	Biobuf b, bo;
	char *s;
	int l;

	if(Binit(&b, fd, OREAD) == -1)
		sysfatal("Binit: %r");
	if(Binit(&bo, 1, OWRITE) == -1)
		sysfatal("Binit: %r");
	
	while(s = Brdstr(&b, '\n', 0)){
		l = Blinelen(&b);
		if(l >= 5)
		if(memcmp(s, "From ", 5) == 0)
			Bputc(&bo, ' ');
		Bwrite(&bo, s, l);
		free(s);
	}
	Bterm(&b);
	Bterm(&bo);
}
	
void
usage(void)
{
	fprint(2, "usage: qfrom [files...]\n");
	exits("");
}

void
main(int argc, char **argv)
{
	int fd;

	ARGBEGIN{
	default:
		usage();
	}ARGEND
	
	if(*argv == 0){
		qfrom(0);
		exits("");
	}
	for(; *argv; argv++){
		fd = open(*argv, OREAD);
		if(fd == -1)
			sysfatal("open: %r");
		qfrom(fd);
		close(fd);
	}
	exits("");
}
