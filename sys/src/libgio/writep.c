#include <u.h>
#include <libc.h>
#include <gio.h>

long
gwrite(int fd, void *buf, long len, vlong offset)
{
	ReadWriter *rd;
	long rval;

	rd = getrdstruct(fd);
	if(rd == nil)
		return -1;
	if(rd->pwrite == nil)
		return -2;
	wlock(rd);
	rval = rd->pwrite(rd, buf, len, offset);
	wunlock(rd);
	return rval;
}

