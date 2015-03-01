#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>

Memsubfont*
openmemsubfont(char *name)
{
	Memsubfont *sf;
	Memimage *i;
	Fontchar *fc;
	int fd, n;
	char hdr[3*12+4+1];
	uchar *p;

	fd = open(name, OREAD);
	if(fd < 0)
		return nil;
	p = nil;
	i = readmemimage(fd);
	if(i == nil)
		goto Err;
	if(readn(fd, hdr, 3*12) != 3*12){
		werrstr("openmemsubfont: header read error: %r");
		goto Err;
	}
	n = atoi(hdr);
	if(n <= 0 || n > 0x7fff){
		werrstr("openmemsubfont: bad fontchar count %d", n);
		goto Err;
	}
	p = malloc(6*(n+1));
	if(p == nil)
		goto Err;
	if(readn(fd, p, 6*(n+1)) != 6*(n+1)){
		werrstr("openmemsubfont: fontchar read error: %r");
		goto Err;
	}
	fc = malloc(sizeof(Fontchar)*(n+1));
	if(fc == nil)
		goto Err;
	_unpackinfo(fc, p, n);
	sf = allocmemsubfont(name, n, atoi(hdr+12), atoi(hdr+24), fc, i);
	if(sf == nil){
		free(fc);
		goto Err;
	}
	close(fd);
	free(p);
	return sf;
Err:
	close(fd);
	free(p);
	freememimage(i);
	return nil;
}
