#include <u.h>
#include <libc.h>
#include <gio.h>

long
gread(int fd, void *bf, long len, vlong offset)
{
	ReadWriter *rd;
	long rval = 0;
	
	rd = getrdstruct(fd);
	if(rd == nil)
		return -1;
	if(rd->pread == nil)
		return -2;
	rlock(rd);
	rval = rd->pread(rd, bf, offset, len);
	runlock(rd);
	return rval;
}

vlong
gseek(int fd, vlong offset, int type)
{
	ReadWriter *rd;

	rd = getrdstruct(fd);
	if(rd == nil)
		return -1;
	wlock(rd);
	switch(type){
	case 0:
		rd->offset = (u64int)offset;
		break;
	case 1:
		rd->offset = rd->offset + (u64int)offset;
		break;
	case 2:
		rd->offset = rd->length + (u64int)offset;
		break;
	}
	wunlock(rd);
	return rd->offset;
}

