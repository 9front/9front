#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include <ctype.h>
#include "mothra.h"

int
snooptype(int fd)
{
	int pfd[2], typ, n;
	char buf[1024];

	typ = PLAIN;
	if((n = readn(fd, buf, sizeof(buf)-1)) < 0)
		return typ;
	buf[n] = 0;
	if(cistrstr(buf, "<?xml") ||
		cistrstr(buf, "<!DOCTYPE") ||
		cistrstr(buf, "<HTML") ||
		cistrstr(buf, "<head"))
		typ = HTML;
	else if(memcmp(buf, "\x1F\x8B", 2) == 0)
		typ = GUNZIP;
	else if(memcmp(buf, "\377\330\377", 3) == 0)
		typ = JPEG;
	else if(memcmp(buf, "\211PNG\r\n\032\n", 3) == 0)
		typ = PNG;
	else if(memcmp(buf, "GIF", 3) == 0)
		typ = GIF;
	else if(memcmp(buf, "BM", 2) == 0)
		typ = BMP;
	else if(memcmp(buf, "PK\x03\x04", 4) == 0)
		typ = PAGE;
	else if(memcmp(buf, "%PDF-", 5) == 0 || strstr(buf, "%!"))
		typ = PAGE;
	else if(memcmp(buf, "x T ", 4) == 0)
		typ = PAGE;
	else if(memcmp(buf, "\xF7\x02\x01\x83\x92\xC0\x1C;", 8) == 0)
		typ = PAGE;
	else if(memcmp(buf, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8) == 0)
		typ = PAGE;
	else if(memcmp(buf, "\111\111\052\000", 4) == 0) 
		typ = PAGE;
	else if(memcmp(buf, "\115\115\000\052", 4) == 0)
		typ = PAGE;
	if(pipe(pfd) >= 0){
		switch(rfork(RFFDG|RFPROC|RFNOWAIT)){
		case -1:
			break;
		case 0:
			close(pfd[0]);
			do {
				if(write(pfd[1], buf, n) != n)
					break;
			} while((n = read(fd, buf, sizeof(buf))) > 0);
			exits(nil);
		default:
			dup(pfd[0], fd);
		}
		close(pfd[1]);
		close(pfd[0]);
	}
	return typ;
}
