#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include "galaxy.h"

Vector o, gv;
double
	d = 100, drand,
	sz = 25, szrand,
	v, vrand,
	av, avrand;
int new, c = 1;

void quadcalc(Body*, QB, double){}
Image *randcol(void){ return nil; }

void
usage(void)
{
	fprint(2, "Usage: %s [-d dist[±r]]\n\t[-s size[±r]] [-v vel[±r]]\n\t[-av angvel[±r]] [-gv xdir,ydir]\n\t[-o xoff,yoff] [-f file]\n\t[-sq] [-i] size\n", argv0);
	exits("usage");
}

Vector
polar(double ang, double mag)
{
	Vector v;

	v.x = cos(ang)*mag;
	v.y = sin(ang)*mag;
	return v;
}

Vector
getvec(char *str)
{
	Vector v;

	v.x = strtod(str, &str);
	if(*str != ',')
		usage();
	v.y = strtod(str+1, nil);
	return v;
}

double
getvals(char *str, double *rand)
{
	Rune r;
	double val;
	int i;

	val = strtod(str, &str);
	i = chartorune(&r, str);
	if(r == L'±')
		*rand = strtod(str+i, nil);
	else
		*rand = 0;
	return val;
}

#define RAND(r)	((r)*(frand()*2 - 1))

void
mkbodies(double lim)
{
	Body *b;
	Vector p;
	double x, y;

	for(x = -lim/2; x < lim/2; x += d)
	for(y = -lim/2; y < lim/2; y += d) {
		p.x = x + RAND(drand);
		p.y = y + RAND(drand);
		if(c)
		if(hypot(p.x, p.y) > lim/2)
			continue;
		b = body();
		b->Vector = p;
		b->v = polar(frand()*π2, v+RAND(vrand));
		b->v.x += gv.x - p.y*(av + RAND(avrand))/1000;
		b->v.y += gv.y + p.x*(av + RAND(avrand))/1000;
		b->size = sz + RAND(szrand);
	}
}

void
main(int argc, char **argv)
{
	static Biobuf bout;
	Body *b;
	double lim;
	int fd;
	char *a;

	srand(truerand());
	fmtinstall('B', Bfmt);
	glxyinit();

	ARGBEGIN {
	case 'f':
		fd = open(EARGF(usage()), OREAD);
		if(fd < 0)
			sysfatal("Could not open file %s: %r", *argv);
		readglxy(fd);
		close(fd);
		break;
	case 'i':
		readglxy(0);
		break;
	case 's':
		a = EARGF(usage());
		switch(a[0]) {
		case 'q':
			if(a[1] != '\0')
				usage();
			c = 0;
			break;
		default:
			sz = getvals(a, &szrand);
			break;
		}
		break;
	case 'a':
		a = EARGF(usage());
		if(a[0] != 'v' || a[1] != '\0')
			usage();
		argc--;
		argv++;
		av = getvals(*argv, &avrand);
		break;
	case 'g':
		a = EARGF(usage());
		if(a[0] != 'v' || a[1] != '\0')
			usage();
		argc--;
		argv++;
		gv = getvec(*argv);
		break;
	case 'v':
		v = getvals(EARGF(usage()), &vrand);
		break;
	case 'o':
		o = getvec(EARGF(usage()));
		break;
	case 'd':
		d = getvals(EARGF(usage()), &drand);
		break;
	} ARGEND

	if(argc != 1)
		usage();

	new = glxy.nb;
	lim = strtod(*argv, nil);
	mkbodies(lim);

	Binit(&bout, 1, OWRITE);
	for(b = glxy.a; b < glxy.a + new; b++)
		Bprint(&bout, "%B\n", b);

	for(b = glxy.a+new; b < glxy.a+glxy.nb; b++) {
		b->x += o.x;
		b->y += o.y;
		Bprint(&bout, "%B\n", b);
	}
	Bterm(&bout);

	exits(nil);
}
