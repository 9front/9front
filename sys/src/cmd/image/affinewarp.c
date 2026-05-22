#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "fns.h"

typedef struct Mstk Mstk;
struct Mstk
{
	Matrix *items;
	ulong size;
};

void
pushmat(Mstk *stk, Matrix m)
{
	if(stk->size % 4 == 0)
		stk->items = erealloc(stk->items, (stk->size + 4)*sizeof(Matrix));
	memmove(stk->items[stk->size++], m, sizeof(Matrix));
}

void
popmat(Mstk *stk, Matrix m)
{
	memmove(m, stk->items[--stk->size], sizeof(Matrix));
	if(stk->size == 0){
		free(stk->items);
		stk->items = nil;
	}
}

void
mkrotation(Matrix m, double θ)
{
	double c, s;

	c = cos(θ);
	s = sin(θ);
	Matrix R = {
		c, -s, 0,
		s,  c, 0,
		0,  0, 1,
	};
	memmove(m, R, sizeof(Matrix));
}

void
mkscale(Matrix m, double sx, double sy)
{
	Matrix S = {
		sx, 0, 0,
		0, sy, 0,
		0, 0, 1,
	};
	memmove(m, S, sizeof(Matrix));
}

void
mktranslation(Matrix m, double tx, double ty)
{
	Matrix T = {
		1, 0, tx,
		0, 1, ty,
		0, 0, 1,
	};
	memmove(m, T, sizeof(Matrix));
}

void
mkshear(Matrix m, double shx, double shy)
{
	Matrix Sxy = {
		1, shx, 0,
		shy, 1, 0,
		0,   0, 1,
	};
	memmove(m, Sxy, sizeof(Matrix));
}

void
mkxform(Matrix m, Mstk *stk)
{
	Matrix t;

	identity(m);
	while(stk->size > 0){
		popmat(stk, t);
		mulm(m, t);
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-Rqp] [[-s x y] [-r θ] [-t x y] [-S x y] ...] [minx miny maxx maxy]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Memimage *dst, *src;
	Matrix m;
	Mstk stk;
	Warp w;
	Rectangle dr, *wr;
	double x, y, θ;
	int smooth, dorepl, parallel, nproc, i;
	char *nprocs;

	memset(&stk, 0, sizeof stk);
	dr = ZR;
	dorepl = 0;
	smooth = 0;
	parallel = 0;
	ARGBEGIN{
	case 's':
		x = strtod(EARGF(usage()), nil);
		y = strtod(EARGF(usage()), nil);
		mkscale(m, x, y);
		pushmat(&stk, m);
		break;
	case 'r':
		θ = strtod(EARGF(usage()), nil)*DEG;
		mkrotation(m, θ);
		pushmat(&stk, m);
		break;
	case 't':
		x = strtod(EARGF(usage()), nil);
		y = strtod(EARGF(usage()), nil);
		mktranslation(m, x, y);
		pushmat(&stk, m);
		break;
	case 'S':
		x = strtod(EARGF(usage()), nil);
		y = strtod(EARGF(usage()), nil);
		mkshear(m, x, y);
		pushmat(&stk, m);
		break;
	case 'R':
		dorepl++;
		break;
	case 'q':
		smooth++;
		break;
	case 'p':
		parallel++;
		break;
	default:
		usage();
	}ARGEND;
	if(argc == 4){
		dr.min.x = strtol(argv[0], nil, 10);
		dr.min.y = strtol(argv[1], nil, 10);
		dr.max.x = strtol(argv[2], nil, 10);
		dr.max.y = strtol(argv[3], nil, 10);
	}else if(argc != 0)
		usage();

	if(memimageinit() != 0)
		sysfatal("memimageinit: %r");

	src = ereadmemimage(0);
	if(dorepl)
		src->flags |= Frepl;

	if(badrect(dr))
		dr = src->r;
	dst = eallocmemimage(dr, src->chan);
	memfillcolor(dst, DTransparent);

	mkxform(m, &stk);
	w = mkwarp(m);

	if(parallel){
		nprocs = getenv("NPROC");
		if(nprocs == nil || (nproc = strtoul(nprocs, nil, 10)) < 2)
			nproc = 1;
		free(nprocs);

		wr = emalloc(nproc*sizeof(Rectangle));
		initworkrects(wr, nproc, &dr);

		for(i = 0; i < nproc; i++){
			switch(rfork(RFPROC|RFMEM)){
			case -1:
				sysfatal("rfork: %r");
			case 0:
				memaffinewarp(dst, wr[i], src, src->r.min, &w, smooth);
				exits(nil);
			}
		}
		while(waitpid() != -1)
			;

		free(wr);
	}else
		memaffinewarp(dst, dr, src, src->r.min, &w, smooth);

	ewritememimage(1, dst);

	exits(nil);
}
