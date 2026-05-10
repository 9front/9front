#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>

Memimage*
creadmemimage(int fd)
{
	char hdr[5*12+1];
	Rectangle r;
	int m, nb, miny, maxy, new, ldepth, ncblock;
	uchar *buf;
	Memimage *i;
	ulong chan;

	if(readn(fd, hdr, 5*12) != 5*12){
		werrstr("creadmemimage: short header");
		return nil;
	}

	/*
	 * distinguish new channel descriptor from old ldepth.
	 * channel descriptors have letters as well as numbers,
	 * while ldepths are a single digit formatted as %-11d.
	 */
	new = 0;
	for(m=0; m<10; m++){
		if(hdr[m] != ' '){
			new = 1;
			break;
		}
	}
	if(hdr[11] != ' '){
		werrstr("creadmemimage: bad format");
		return nil;
	}
	if(new){
		hdr[11] = '\0';
		if((chan = strtochan(hdr)) == 0){
			werrstr("creadmemimage: bad channel string %s", hdr);
			return nil;
		}
	}else{
		ldepth = ((int)hdr[10])-'0';
		if(ldepth<0 || ldepth>3){
			werrstr("creadmemimage: bad ldepth %d", ldepth);
			return nil;
		}
		chan = drawld2chan[ldepth];
	}
	r.min.x=atoi(hdr+1*12);
	r.min.y=atoi(hdr+2*12);
	r.max.x=atoi(hdr+3*12);
	r.max.y=atoi(hdr+4*12);
	if(r.min.x>r.max.x || r.min.y>r.max.y){
		werrstr("creadmemimage: bad rectangle");
		return nil;
	}

	i = allocmemimage(r, chan);
	if(i == nil)
		return nil;
	ncblock = _compblocksize(r, i->depth);
	buf = malloc(ncblock);
	if(buf == nil)
		goto Errout;
	miny = r.min.y;
	while(miny != r.max.y){
		if(readn(fd, hdr, 2*12) != 2*12){
		Shortread:
			werrstr("creadmemimage: short read");
		Errout:
			freememimage(i);
			free(buf);
			return nil;
		}
		maxy = atoi(hdr+0*12);
		nb = atoi(hdr+1*12);
		if(maxy<=miny || r.max.y<maxy){
			werrstr("creadmemimage: bad maxy %d", maxy);
			goto Errout;
		}
		if(nb<=0 || ncblock<nb){
			werrstr("creadmemimage: bad count %d", nb);
			goto Errout;
		}
		if(readn(fd, buf, nb)!=nb)
			goto Shortread;
		if(!new)	/* old image: flip the data bits */
			_twiddlecompressed(buf, nb);
		if(cloadmemimage(i, Rect(r.min.x, miny, r.max.x, maxy), buf, nb) < 0){
			werrstr("creadmemimage: %r");
			goto Errout;
		}
		miny = maxy;
	}
	free(buf);
	return i;
}
