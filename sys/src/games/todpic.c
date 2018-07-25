#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <bio.h>

int wofs;
u32int pal[256], bg = 0x00ffff;
Biobuf *bp;

#define abs(x) ((x) < 0 ? -(x) : (x))

void
put8(u8int v)
{
	if(Bwrite(bp, &v, sizeof v) != sizeof v)
		sysfatal("put8: short write");
}

void
put16(u16int v)
{
	put8(v);
	put8(v >> 8);
}

void
put32(u32int v)
{
	put16(v);
	put16(v >> 16);
}

int
pali(u32int v)
{
	int i, Δ, Δ´;
	u32int *p;

	i = 0;
	Δ = abs((char)v - (char)*pal)
		+ abs((char)(v >> 8) - (char)(*pal >> 8))
		+ abs((char)(v >> 16) - (char)(*pal >> 16));
	for(p=pal; p<pal+nelem(pal); p++){
		Δ´ = abs((char)v - (char)*p)
			+ abs((char)(v >> 8) - (char)(*p >> 8))
			+ abs((char)(v >> 16) - (char)(*p >> 16));
		if(Δ´ < Δ){
			Δ = Δ´;
			i = p - pal;
			if(Δ == 0)
				break;
		}
	}
	return i;
}

void
topic(Memimage *i)
{
	int w, h, dx, dy;
	uchar *np, *b, *buf, *p, *pp;
	u32int v;

	p = i->data->bdata;
	dx = Dx(i->r);
	dy = Dy(i->r);
	if(dy > 254)
		sysfatal("topic: invalid pic height");
	put16(dx);
	put16(dy);
	put16(wofs ? dx / 2 - 1 : i->r.min.x);
	put16(wofs ? dy - 5 : i->r.min.y);
	if(i->r.min.x != 0)
		dx = i->width;
	buf = mallocz((5 * dy / 2 + 5) * dx, 1);
	if(buf == nil)
		sysfatal("mallocz: %r");
	for(w=dx, b=buf; w>0; w--, p+=3){
		put32(b - buf + 8 + dx * 4);
		for(h=0, np=b+1, pp=p; h<dy; h++, pp+=dx*3){
			v = pp[2] << 16 | pp[1] << 8 | pp[0];
			if(v == bg){
				if(b - np - 2 > 0){
					*np = b - np - 2;
					*b++ = 0;
					np = b + 1;
				}
				continue;
			}
			if(b - np - 2 < 0){
				*b++ = h;
				b++;
				*b++ = 0;
			}
			*b++ = pali(v);
		}
		if(b - np - 2 >= 0){
			*np = b - np - 2;
			*b++ = 0;
		}
		*b++ = 0xff;
	}
	Bwrite(bp, buf, b - buf);
	free(buf);
}

void
toflat(Memimage *i)
{
	int n;
	uchar *p;

	if(Dx(i->r) != 64 || Dy(i->r) != 64)
		sysfatal("toflat: invalid flatpic dimensions");
	p = i->data->bdata;
	n = 64*64;
	while(n-- > 0){
		put8(pali(p[2] << 16 | p[1] << 8 | p[0]));
		p += 4;
	}
}

static Memimage*
iconv(Memimage *i)
{
	Memimage *ni;

	if(i->chan == RGB24)
		return i;
	if((ni = allocmemimage(i->r, RGB24)) == nil)
		sysfatal("allocmemimage: %r");
	memimagedraw(ni, ni->r, i, i->r.min, nil, i->r.min, S);
	freememimage(i);
	return ni;
}

void
getpal(char *f)
{
	uchar u[3];
	u32int *p;
	Biobuf *bp;

	if((bp = Bopen(f, OREAD)) == nil)
		sysfatal("getpal: %r");
	for(p=pal; p<pal+nelem(pal); p++){
		if(Bread(bp, u, 3) != 3)
			sysfatal("getpal: short read: %r");
		*p = u[0]<<16 | u[1]<<8 | u[2];
	}
	Bterm(bp);
}

void
usage(void)
{
	fprint(2, "usage: %s [-fw] [-b bgcol] [-p palette] [image]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int fd, flat;
	char *p;
	Memimage *i;

	fd = 0;
	flat = 0;
	p = "/mnt/wad/playpal";
	ARGBEGIN{
	case 'b': bg = strtoul(EARGF(usage()), nil, 0); break;
	case 'f': flat = 1; break;
	case 'p': p = EARGF(usage()); break;
	case 'w': wofs = 1; break;
	default: usage();
	}ARGEND
	if(*argv != nil)
		if((fd = open(*argv, OREAD)) < 0)
			sysfatal("open: %r");
	getpal(p);
	if((bp = Bfdopen(1, OWRITE)) == nil)
		sysfatal("Bfdopen: %r");
	memimageinit();
	if((i = readmemimage(fd)) == nil)
		sysfatal("readmemimage: %r");
	(flat ? toflat : topic)(iconv(i));
	exits(nil);
}
