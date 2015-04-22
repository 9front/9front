#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

Revinfo*
loadrevinfo(Revlog *changelog, int rev)
{
	char buf[BUFSZ], *p, *e;
	int fd, line, eof, inmsg, n;
	Revinfo *ri;
	vlong off;

	if((fd = revlogopentemp(changelog, rev)) < 0)
		return nil;

	off = fmetaheader(fd);
	seek(fd, off, 0);

	ri = malloc(sizeof(*ri));
	memset(ri, 0, sizeof(*ri));

	memmove(ri->chash, changelog->map[rev].hash, HASHSZ);

	eof = 0;
	line = 0;
	inmsg = 0;
	p = buf;
	e = buf + BUFSZ;
	while(eof == 0){
		if((n = read(fd, p, e - p)) < 0)
			break;
		if(n == 0){
			eof = 1;
			*p = '\n';
			n++;
		}
		p += n;
		while((p > buf) && (e = memchr(buf, '\n', p - buf))){
			*e++ = 0;

			switch(line++){
			case 0:
				hex2hash(buf, ri->mhash);
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
					ri->why = realloc(ri->why, n + strlen(buf)+2);
					if(n > 0) ri->why[n++] = '\n';
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
