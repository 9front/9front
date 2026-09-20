#include <u.h>
#include <libc.h>
#include <draw.h>

int
unloadimage(Image *i, Rectangle r, uchar *data, int ndata)
{
	int bpl, n, chunk, dx, dy;
	uchar *start;
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

		_lockdisplay(d);
		if(drawcmd(d, "blPll", 'r', i->id, &r.min, r.min.x+dx, r.min.y+dy) < 0){
			_unlockdisplay(d);
			werrstr("unloadimage: %r");
			return -1;
		}
		_unlockdisplay(d);

		if(flushimage(d, 0) < 0)
			return -1;

		_lockdisplay(d);
		if(read(d->fd, data, n) < 0){
			_unlockdisplay(d);
			return -1;
		}
		_unlockdisplay(d);

		data += bpl*dy;
		r.min.y += dy;
	}
	return data - start;
}
