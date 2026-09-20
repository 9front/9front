#include <u.h>
#include <libc.h>
#include <draw.h>

int
cloadimage(Image *i, Rectangle r, uchar *data, int ndata)
{
	int m, nb, miny, maxy, ncblock;

	if(!rectinrect(r, i->r)){
		werrstr("cloadimage: bad rectangle");
		return -1;
	}

	miny = r.min.y;
	m = 0;
	ncblock = _compblocksize(r, i->depth);
	while(miny != r.max.y){
		maxy = atoi((char*)data+0*12);
		nb = atoi((char*)data+1*12);
		if(maxy<=miny || r.max.y<maxy){
			werrstr("cloadimage: bad maxy %d", maxy);
			return -1;
		}
		data += 2*12;
		ndata -= 2*12;
		m += 2*12;
		if(nb<=0 || ncblock<nb || nb>ndata){
			werrstr("cloadimage: bad count %d", nb);
			return -1;
		}

		_lockdisplay(i->display);
		if(drawcmd(i->display, "blllll<", 'Y', i->id, r.min.x, miny, r.max.x, maxy, nb, data) < 0){
			_unlockdisplay(i->display);
			return -1;
		}
		_unlockdisplay(i->display);

		miny = maxy;
		data += nb;
		ndata += nb;
		m += nb;
	}
	return m;
}
