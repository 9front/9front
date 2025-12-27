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

Point2
Ptpt2(Point p)
{
	return (Point2){p.x, p.y, 1};
}

#define min(a,b)	((a)<(b)?(a):(b))
#define max(a,b)	((a)>(b)?(a):(b))
Rectangle
getbbox(Rectangle *sr, Matrix m, Point2 *dp0)
{
	Point2 p0, p1, p2, p3;
	Rectangle bbox;

	p0 = Pt2(sr->min.x + 0.5, sr->min.y + 0.5, 1);
	p0 = *dp0 = xform(p0, m);
	p1 = Pt2(sr->max.x + 0.5, sr->min.y + 0.5, 1);
	p1 = xform(p1, m);
	p2 = Pt2(sr->min.x + 0.5, sr->max.y + 0.5, 1);
	p2 = xform(p2, m);
	p3 = Pt2(sr->max.x + 0.5, sr->max.y + 0.5, 1);
	p3 = xform(p3, m);

	bbox.min.x = min(min(min(p0.x, p1.x), p2.x), p3.x);
	bbox.min.y = min(min(min(p0.y, p1.y), p2.y), p3.y);
	bbox.max.x = max(max(max(p0.x, p1.x), p2.x), p3.x);
	bbox.max.y = max(max(max(p0.y, p1.y), p2.y), p3.y);
	return bbox;
}

void
usage(void)
{
	fprint(2, "usage: %s [-Rqp] [[-s x y] [-r θ] [-t x y] [-S x y] ...]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Memimage *dst, *src;
	Matrix m;
	Mstk stk;
	Point2 Δp;
	Warp w;
	Rectangle dr, *wr;
	double x, y, θ;
	int smooth, dorepl, parallel, nproc, i;
	char *nprocs;

	memset(&stk, 0, sizeof stk);
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
	if(argc != 0)
		usage();

	if(memimageinit() != 0)
		sysfatal("memimageinit: %r");

	src = ereadmemimage(0);
	if(dorepl)
		src->flags |= Frepl;

	mkxform(m, &stk);
	dr = getbbox(&src->r, m, &Δp);
	Δp = subpt2(Δp, Ptpt2(dr.min));
	Matrix T = {
		1, 0, Δp.x,
		0, 1, Δp.y,
		0, 0, 1,
	};
	mulm(T, m);
	mkwarp(w, T);

	dr = rectaddpt(dr, subpt(src->r.min, dr.min));
	dst = eallocmemimage(dr, src->chan);
	memfillcolor(dst, DTransparent);

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
				memaffinewarp(dst, wr[i], src, src->r.min, w, smooth);
				exits(nil);
			}
		}
		while(waitpid() != -1)
			;

		free(wr);
	}else
		memaffinewarp(dst, dr, src, src->r.min, w, smooth);

	ewritememimage(1, dst);

	exits(nil);
}
