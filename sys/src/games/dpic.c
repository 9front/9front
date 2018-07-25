#include <u.h>
#include <libc.h>
#include <draw.h>
#include <bio.h>

int dx = 64, dy = 64;
Biobuf *bi, *bo;
u32int pal[256];

u8int
get8(void)
{
	uchar v;

	if(Bread(bi, &v, 1) != 1)
		sysfatal("get8: short read");
	return v;
}

u16int
get16(void)
{
	u8int v;

	v = get8();
	return get8() << 8 | v;
}

u32int
get32(void)
{
	u16int v;

	v = get16();
	return get16() << 16 | v;
}

u32int*
unpic(void)
{
	int n, h;
	u32int *p, *d, *cols, *buf;

	dx = get16();
	dy = get16();
	cols = mallocz(dx * sizeof *cols, 1);
	buf = mallocz(dx * dy * sizeof *buf, 1);
	if(cols == nil || buf == nil)
		sysfatal("mallocz: %r");
	get32();
	for(p=cols; p<cols+dx; p++)
		*p = get32();
	for(p=cols; p<cols+dx; p++){
		Bseek(bi, *p, 0);
		for(;;){
			if((h = get8()) == 0xff)
				break;
			n = get8();
			get8();
			for(d=buf+(p-cols)+h*dx; n-->0; d+=dx)
				*d = pal[get8()];
			get8();
		}
	}
	free(cols);
	return buf;
}

u32int*
unflat(void)
{
	u32int *p;
	static u32int buf[4096];

	for(p=buf; p<buf+nelem(buf); p++)
		*p = pal[get8()];
	return buf;
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
		*p = u[2]<<16 | u[1]<<8 | u[0];
	}
	Bterm(bp);
}

void
usage(void)
{
	fprint(2, "usage: %s [-f] [-p palette] pic\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int fd, flat;
	char *p, c[9];
	u32int *buf;

	flat = 0;
	p = "/mnt/wad/playpal";
	ARGBEGIN{
	case 'f': flat = 1; break;
	case 'p': p = EARGF(usage()); break;
	default: usage();
	}ARGEND
	if(*argv == nil)
		usage();
	if((fd = open(*argv, OREAD)) < 0)
		sysfatal("open: %r");
	getpal(p);
	bi = Bfdopen(fd, OREAD);
	bo = Bfdopen(1, OWRITE);
	if(bi == nil || bo == nil)
		sysfatal("Bfdopen: %r");
	buf = flat ? unflat() : unpic();
	Bprint(bo, "%11s %11d %11d %11d %11d ",
		chantostr(c, XBGR32), 0, 0, dx, dy);
	Bwrite(bo, buf, dx * dy * sizeof *buf);
	exits(nil);
}
