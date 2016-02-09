#include <u.h>
#include <libc.h>
#include <gio.h>

RWLock giolock;
ReadWriter *gio_filedes[256];
uchar gio_filedes_st[256];

int
getnext(void)
{
	int i;
	for(i = 0; i < 256; i++)
		if(gio_filedes_st[i] == 0)
			break;
	if(i == 256)
		return -1;
	return i;
}

ReadWriter*
getrdstruct(int fd)
{
	rlock(&giolock);
	ReadWriter *rval;
	if(gio_filedes_st[fd] != 1)
		rval = nil;
	else
		rval = gio_filedes[fd];
	runlock(&giolock);
	return rval;
}

int
gopen(ReadWriter* rd, void *aux)
{
	int pfd;
	ReadWriter *buf;

	wlock(&giolock);
	pfd = getnext();
	if(pfd == -1){
		wunlock(&giolock);
		return -1;
	}
	buf = malloc(sizeof(ReadWriter));
	if(buf == nil)
		exits("bad malloc");
	memcpy(buf, rd, sizeof(ReadWriter));
	buf->aux = aux;
	buf->offset = 0;
	if(buf->open != nil){
		if((buf->open(buf)) != 0){
			buf->close(buf);
			free(buf);
			wunlock(&giolock);
			return -1;
		}
	}
	gio_filedes[pfd] = buf;
	gio_filedes_st[pfd] = 1;
	wunlock(&giolock);
	return pfd;
}

int
gclose(int fd)
{
	ReadWriter *bf;
	int rval = 0;

	if(gio_filedes_st[fd] == 0)
		return -1;
	wlock(&giolock);
	bf = gio_filedes[fd];
	if(bf->close != nil)
		rval = bf->close(bf);
	free(bf);
	gio_filedes_st[fd] = 0;
	gio_filedes[fd] = nil;
	wunlock(&giolock);
	return rval;
}

