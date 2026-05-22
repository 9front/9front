#include <u.h>
#include <libc.h>
#include <draw.h>
#include <geometry.h>

void
usage(void)
{
	fprint(2, "usage: %s [-s sx sy] [-t tx ty] [-r θ]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Image *dst, *src;
	Warp w;
	double sx, sy, tx, ty, θ, c, s;
	int smooth;

	sx = sy = 1;
	tx = ty = 0;
	θ = 0;
	smooth = 0;
	ARGBEGIN{
	case 's':
		sx = strtod(EARGF(usage()), nil);
		sy = strtod(EARGF(usage()), nil);
		break;
	case 't':
		tx = strtod(EARGF(usage()), nil);
		ty = strtod(EARGF(usage()), nil);
		break;
	case 'r':
		θ = strtod(EARGF(usage()), nil)*DEG;
		break;
	case 'S':
		smooth++;
		break;
	default:
		usage();
	}ARGEND;
	if(argc != 0)
		usage();

	if(initdraw(nil, nil, "warp test") < 0)
		sysfatal("initdraw: %r");

	c = cos(θ);
	s = sin(θ);
	Matrix S = {
		sx, 0, 0,
		0, sy, 0,
		0, 0, 1,
	}, T = {
		1, 0, tx,
		0, 1, ty,
		0, 0, 1,
	}, R = {
		c, -s, 0,
		s, c, 0,
		0, 0, 1,
	};

	mulm(R, S);
	mulm(T, R);

	w = mkwarp(T);

	src = readimage(display, 0, 0);
	dst = allocimage(display, src->r, src->chan, 0, DNofill);
	affinewarp(dst, dst->r, src, src->r.min, &w, smooth);
	writeimage(1, dst, 0);

	exits(nil);
}
