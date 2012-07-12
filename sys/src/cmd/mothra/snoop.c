#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include <ctype.h>
#include "mothra.h"

int
filetype(int fd, char *typ, int ntyp)
{
	int ifd[2], ofd[2], xfd[2], n;
	char *argv[3], buf[4096];

	typ[0] = 0;
	if((n = readn(fd, buf, sizeof(buf))) < 0)
		return -1;
	if(n == 0)
		return 0;
	if(pipe(ifd) < 0)
		return -1;
	if(pipe(ofd) < 0){
Err1:
		close(ifd[0]);
		close(ifd[1]);
		return -1;
	}
	switch(rfork(RFFDG|RFPROC|RFNOWAIT)){
	case -1:
		close(ofd[0]);
		close(ofd[1]);
		goto Err1;	
	case 0:
		dup(ifd[1], 0);
		dup(ofd[1], 1);

		close(ifd[1]);
		close(ifd[0]);
		close(ofd[1]);
		close(ofd[0]);
		close(fd);

		argv[0] = "file";
		argv[1] = "-m";
		argv[2] = 0;
		exec("/bin/file", argv);
	}
	close(ifd[1]);
	close(ofd[1]);

	if(rfork(RFFDG|RFPROC|RFNOWAIT) == 0){
		close(fd);
		close(ofd[0]);
		write(ifd[0], buf, n);
		exits(nil);
	}
	close(ifd[0]);

	if(pipe(xfd) < 0){
		close(ofd[0]);
		return -1;
	}
	switch(rfork(RFFDG|RFPROC|RFNOWAIT)){
	case -1:
		break;
	case 0:
		close(ofd[0]);
		close(xfd[0]);
		do {
			if(write(xfd[1], buf, n) != n)
				break;
		} while((n = read(fd, buf, sizeof(buf))) > 0);
		exits(nil);
	default:
		dup(xfd[0], fd);
	}
	close(xfd[0]);
	close(xfd[1]);

	if((n = readn(ofd[0], typ, ntyp-1)) < 0)
		n = 0;
	close(ofd[0]);
	while(n > 0 && typ[n-1] == '\n')
		n--;
	typ[n] = 0;
	return 0;
}

int
snooptype(int fd)
{
	static struct {
		char	*typ;
		int	val;
	} tab[] = {
	"text/plain",			PLAIN,
	"text/html",			HTML,

	"image/jpeg",			JPEG,
	"image/gif",			GIF,
	"image/png",			PNG,
	"image/bmp",			BMP,
	"image/x-icon",			ICO,

	"application/pdf",		PAGE,
	"application/postscript",	PAGE,
	"application/ghostscript",	PAGE,
	"application/troff",		PAGE,

	"image/",			PAGE,
	"text/",			PLAIN,
	"message/rfc822",		PLAIN,
	};
	char buf[128];
	int i;
	if(filetype(fd, buf, sizeof(buf)) < 0)
		return -1;
	for(i=0; i<nelem(tab); i++)
		if(strncmp(buf, tab[i].typ, strlen(tab[i].typ)) == 0)
			return tab[i].val;
	return -1;
}
