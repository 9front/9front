#include <u.h>
#include <libc.h>

#define PUT16(p, u) ((p)[1] = (u)>>8, (p)[0] = (u))
#define PUT24(p, u) ((p)[2] = (u)>>16, (p)[1] = (u)>>8, (p)[0] = (u))
#define PUT32(p, u) ((p)[3] = (u)>>24, (p)[2] = (u)>>16, (p)[1] = (u)>>8, (p)[0] = (u))

typedef struct Desc Desc;
struct Desc
{
	int	rate;
	int	channels;
	int	bits;
	Rune	fmt;
};

Desc
mkdesc(char *f)
{
	Desc d;
	Rune r;
	char *p;

	memset(&d, 0, sizeof(d));
	p = f;
	while(*p != 0){
		p += chartorune(&r, p);
		switch(r){
		case L'r':
			d.rate = strtol(p, &p, 10);
			break;
		case L'c':
			d.channels = strtol(p, &p, 10);
			break;
		case L'm':
			r = L'µ';
		case L's':
		case L'u':
		case L'a':
		case L'µ':
			d.fmt = r;
			d.bits = strtol(p, &p, 10);
			break;
		default:
			goto Bad;
		}
	}
	if(d.rate <= 0)
		goto Bad;
	if(d.bits <= 0 || d.bits > 16)
		goto Bad;
	if(d.bits == 16 && d.fmt != 's')
		goto Bad;
	return d;
Bad:
	sysfatal("bad format: %s", f);
}

int
codec(Rune r)
{
	switch(r){
	case 'u':
		return 0x0;
	case 's':
		return 0x04;
	case L'µ':
		return 0x06;
	case 'a':
		return 0x07;
	default:
		sysfatal("bad format");
	}
}

void
usage(void)
{
	fprint(2, "usage: %s fmt\n", argv0);
	sysfatal("usage");
}

void
main(int argc, char **argv)
{
	Desc o;
	char magic[] = "Creative Voice File\x1a";
	uchar buf[16+8192];
	long n;
	int c;
	enum{ hdrsz = 0x1a, ver = 0x010a };

	ARGBEGIN{
	default:
		usage();
		break;
	}ARGEND;
	if(argc < 1)
		usage();

	o = mkdesc(argv[0]);
	write(1, magic, sizeof magic - 1);
	PUT16(buf, hdrsz);
	PUT16(buf+2, ver);
	PUT16(buf+4, ~ver + 0x1234);
	write(1, buf, 2+2+2);

	while((n = read(0, buf+16, sizeof buf-16)) > 0){
		buf[0] = 0x9;
		PUT24(buf+1, n+12);
		PUT32(buf+4, o.rate);
		buf[8] = o.bits;
		buf[9] = o.channels;
		c = codec(o.fmt);
		PUT16(buf+10, c);
		PUT32(buf+12, 0x0);
		write(1, buf, 16+n);
	}
	buf[0] = 0;
	write(1, buf, 1);
	exits(nil);
}
