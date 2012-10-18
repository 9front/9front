#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>

Memimage*
rot90(Memimage *m)
{
	int line, bpp, x, y, dx, dy;
	ulong chan;
	uchar *s, *d;
	Memimage *w;

	bpp = (m->depth+7)/8;
	chan = m->chan;
	switch(chan){
	case GREY1:
	case GREY2:
	case GREY4:
		if((w = allocmemimage(m->r, GREY8)) == nil)
			sysfatal("allocmemimage: %r");
		memimagedraw(w, w->r, m, m->r.min, nil, ZP, S);
		freememimage(m);
		m = w;
		break;
	}

	dx = Dx(m->r);
	dy = Dy(m->r);
	if((w = allocmemimage(Rect(m->r.min.x, m->r.min.y, 
		m->r.min.x+dy, m->r.min.y+dx), m->chan)) == nil)
		sysfatal("allocmemimage: %r");
	line = w->width*sizeof(ulong);
	for(y=0; y<dy; y++){
		s = byteaddr(m, addpt(m->r.min, Pt(0, y)));
		d = byteaddr(w, addpt(w->r.min, Pt(dy-y-1, 0)));
		for(x=0; x<dx; x++){
			switch(bpp){
			case 4:
				d[3] = s[3];
			case 3:
				d[2] = s[2];
			case 2:
				d[1] = s[1];
			case 1:
				d[0] = s[0];
			}
			s += bpp;
			d += line;
		}
	}
	freememimage(m);
	if(w->chan != chan){
		if((m = allocmemimage(w->r, chan)) == nil)
			sysfatal("allocmemimage: %r");
		memimagedraw(m, m->r, w, w->r.min, nil, ZP, S);
		freememimage(w);
		w = m;
	}
	return w;
}

Memimage*
upsidedown(Memimage *m)
{
	uchar *s, *d, *t;
	int w, y, dy;

	dy = Dy(m->r);
	w = m->width * sizeof(ulong);
	if((t = malloc(w)) == nil)
		sysfatal("malloc: %r");
	for(y=0; y<dy/2; y++){
		s = byteaddr(m, addpt(m->r.min, Pt(0, y)));
		d = byteaddr(m, addpt(m->r.min, Pt(0, dy-y-1)));
		memmove(t, d, w);
		memmove(d, s, w);
		memmove(s, t, w);
	}
	free(t);
	return m;
}

void
usage(void)
{
	fprint(2, "usage: %s [ -r degree ] [ -u | -l ] [ file ]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Memimage *m;
	int fd, r;
	char f;

	f = 0;
	r = 0;
	fd = 0;
	ARGBEGIN {
	case 'u':
		f = 'u';
		break;
	case 'l':
		f = 'l';
		break;
	case 'r':
		r = atoi(EARGF(usage()));
		break;
	default:
		usage();
	} ARGEND;
	if(*argv){
		fd = open(*argv, OREAD);
		if(fd < 0)
			sysfatal("open: %r");
	}
	memimageinit();
	if((m = readmemimage(fd)) == nil)
		sysfatal("readmemimage: %r");
	if(f == 'u' || f == 'l'){
		m = upsidedown(m);
		if(f == 'l')
			r = 180;
	}
	switch(r % 360){
	case 270:
		m = rot90(m);
	case 180:
		m = rot90(m);
	case 90:
		m = rot90(m);
		break;
	}
	if(writememimage(1, m) < 0)
		sysfatal("writememimage: %r");
	exits(0);
}

