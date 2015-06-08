#include <u.h>
#include <libc.h>
#include <draw.h>

int
loadimage(Image *i, Rectangle r, uchar *data, int ndata)
{
	long dx, dy;
	int n, bpl;
	uchar *a;
	int chunk;

	chunk = i->display->bufsize - 64;

	if(!rectinrect(r, i->r)){
		werrstr("loadimage: bad rectangle");
		return -1;
	}
	bpl = bytesperline(r, i->depth);
	n = bpl*Dy(r);
	if(n > ndata){
		werrstr("loadimage: insufficient data");
		return -1;
	}
	ndata = 0;
	while(r.max.y > r.min.y){
		dy = Dy(r);
		dx = Dx(r);
		if(dy*bpl > chunk)
			dy = chunk/bpl;
		if(dy <= 0){
			dy = 1;
			dx = ((chunk*dx)/bpl) & ~7;
			n = bytesperline(Rect(r.min.x, r.min.y, r.min.x+dx, r.min.y+dy), i->depth);
			if(loadimage(i, Rect(r.min.x+dx, r.min.y, r.max.x, r.min.y+dy), data+n, bpl-n) < 0)
				return -1;
		} else
			n = dy*bpl;
		a = bufimage(i->display, 21+n);
		if(a == nil){
			werrstr("loadimage: %r");
			return -1;
		}
		a[0] = 'y';
		BPLONG(a+1, i->id);
		BPLONG(a+5, r.min.x);
		BPLONG(a+9, r.min.y);
		BPLONG(a+13, r.min.x+dx);
		BPLONG(a+17, r.min.y+dy);
		memmove(a+21, data, n);
		ndata += dy*bpl;
		data += dy*bpl;
		r.min.y += dy;
	}
	return ndata;
}
