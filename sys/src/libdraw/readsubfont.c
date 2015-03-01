#include <u.h>
#include <libc.h>
#include <draw.h>

Subfont*
readsubfonti(Display*d, char *name, int fd, Image *ai, int dolock)
{
	char hdr[3*12+4+1];
	int n;
	uchar *p;
	Fontchar *fc;
	Subfont *f;
	Image *i;

	i = ai;
	if(i == nil){
		i = readimage(d, fd, dolock);
		if(i == nil)
			return nil;
	}
	p = nil;
	if(readn(fd, hdr, 3*12) != 3*12){
		werrstr("readsubfont: header read error: %r");
		goto Err;
	}
	n = atoi(hdr);
	if(n <= 0 || n > 0x7fff){
		werrstr("readsubfont: bad fontchar count %d", n);
		goto Err;
	}
	p = malloc(6*(n+1));
	if(p == nil)
		goto Err;
	if(readn(fd, p, 6*(n+1)) != 6*(n+1)){
		werrstr("readsubfont: fontchar read error: %r");
		goto Err;
	}
	fc = malloc(sizeof(Fontchar)*(n+1));
	if(fc == nil)
		goto Err;
	_unpackinfo(fc, p, n);
	if(dolock)
		lockdisplay(d);
	f = allocsubfont(name, n, atoi(hdr+12), atoi(hdr+24), fc, i);
	if(dolock)
		unlockdisplay(d);
	if(f == nil){
		free(fc);
		goto Err;
	}
	free(p);
	return f;
Err:
	if(ai == nil)
		freeimage(i);
	free(p);
	return nil;
}

Subfont*
readsubfont(Display *d, char *name, int fd, int dolock)
{
	return readsubfonti(d, name, fd, nil, dolock);
}
