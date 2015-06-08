#include <u.h>
#include <libc.h>
#include <draw.h>

int
unloadimage(Image *i, Rectangle r, uchar *data, int ndata)
{
	int bpl, n, chunk, dx, dy;
	uchar *a, *start;
	Display *d;

	if(!rectinrect(r, i->r)){
		werrstr("unloadimage: bad rectangle");
		return -1;
	}
	bpl = bytesperline(r, i->depth);
	if(ndata < bpl*Dy(r)){
		werrstr("unloadimage: buffer too small");
		return -1;
	}
	start = data;
	d = i->display;
	chunk = d->bufsize;
	flushimage(d, 0);	/* make sure subsequent flush is for us only */
	while(r.min.y < r.max.y){
		dx = Dx(r);
		dy = chunk/bpl;
		if(dy <= 0){
			dy = 1;
			dx = ((chunk*dx)/bpl) & ~7;
			n = bytesperline(Rect(r.min.x, r.min.y, r.min.x+dx, r.min.y+dy), i->depth);
			if(unloadimage(i, Rect(r.min.x+dx, r.min.y, r.max.x, r.min.y+dy), data+n, bpl-n) < 0)
				return -1;
		} else {
			if(dy > Dy(r))
				dy = Dy(r);
			n = bpl*dy;
		}
		a = bufimage(d, 1+4+4*4);
		if(a == nil){
			werrstr("unloadimage: %r");
			return -1;
		}
		a[0] = 'r';
		BPLONG(a+1, i->id);
		BPLONG(a+5, r.min.x);
		BPLONG(a+9, r.min.y);
		BPLONG(a+13, r.min.x+dx);
		BPLONG(a+17, r.min.y+dy);
		if(flushimage(d, 0) < 0)
			return -1;
		if(read(d->fd, data, n) < 0)
			return -1;
		data += bpl*dy;
		r.min.y += dy;
	}
	return data - start;
}
