#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

Revinfo*
loadrevinfo(Revlog *changelog, int rev)
{
	char buf[BUFSZ], *p, *e;
	int fd, line, inmsg, n;
	Revinfo *ri;
	vlong off;

	if((fd = revlogopentemp(changelog, rev)) < 0)
		return nil;

	seek(fd, 0, 2);
	write(fd, "\n", 1);
	seek(fd, 0, 0);

	ri = malloc(sizeof(*ri));
	memset(ri, 0, sizeof(*ri));

	memmove(ri->chash, changelog->map[rev].hash, HASHSZ);

	off = 0;
	line = 0;
	inmsg = 0;
	p = buf;
	e = buf + BUFSZ;
	while((n = read(fd, p, e - p)) > 0){
		p += n;
		while((p > buf) && (e = memchr(buf, '\n', p - buf))){
			*e++ = 0;

			switch(line++){
			case 0:
				strhash(buf, ri->mhash);
				break;
			case 1:
				ri->who = strdup(buf);
				break;
			case 2:
				ri->when = strtol(buf, nil, 10);
				break;
			case 3:
				ri->logoff = off;
			default:
				if(!inmsg){
					if(*buf == 0){
						ri->loglen = off - ri->logoff;
						inmsg = 1;
					}
				} else {
					n = ri->why ? strlen(ri->why) : 0;
					ri->why = realloc(ri->why, n + strlen(buf)+1);
					strcpy(ri->why + n, buf);
				}
			}
			n = e - buf;
			p -= n;
			if(p > buf)
				memmove(buf, e, p - buf);
			off += n;
		}
		e = buf + BUFSZ;
	}
	close(fd);

	return ri;
}
