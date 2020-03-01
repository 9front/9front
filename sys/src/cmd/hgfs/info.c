#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include "dat.h"
#include "fns.h"

Revinfo*
loadrevinfo(Revlog *changelog, int rev)
{
	int c, fd;
	char *line;
	Revinfo *ri;
	vlong off;
	Biobuf *buf;

	if((fd = revlogopentemp(changelog, rev)) < 0)
		return nil;

	off = fmetaheader(fd);
	seek(fd, off, 0);

	ri = malloc(sizeof(*ri));
	memset(ri, 0, sizeof(*ri));

	ri->logoff = off;
	memmove(ri->chash, changelog->map[rev].hash, HASHSZ);

	buf = Bfdopen(fd, OREAD);
	line = Brdstr(buf, '\n', 1);
	if(line == nil)
		goto Error;
	hex2hash(line, ri->mhash);
	free(line);

	line = Brdstr(buf, '\n', 1);
	if(line == nil)
		goto Error;
	ri->who = line;

	line = Brdstr(buf, '\n', 1);
	if(line == nil)
		goto Error;
	ri->when = strtol(line, nil, 10);
	free(line);

	ri->logoff = Boffset(buf);
	for(;;){
		if((c = Bgetc(buf)) < 0)
			goto Error;
		if(c == '\n')
			break;
		do {
			if((c = Bgetc(buf)) < 0)
				goto Error;
		} while(c != '\n');
	}
	ri->loglen = Boffset(buf) - ri->logoff - 1;

	line = Brdstr(buf, '\0', 1);
	if(line == nil)
		goto Error;
	ri->why = line;

	Bterm(buf);
	close(fd);

	return ri;
Error:
	Bterm(buf);
	close(fd);
	free(ri->who);
	free(ri->why);
	free(ri);
	return nil;
}
