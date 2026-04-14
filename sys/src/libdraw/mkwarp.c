#include <u.h>
#include <libc.h>
#include <draw.h>
#include <geometry.h>

/* 19.13 fixed-point number operations */

#define flt2fix(n)	((long)((n)*(1<<13) + ((n) < 0? -0.5: 0.5)))

void
mkwarp(Warp w, double m0[3][3])
{
	Matrix m;

	memmove(m, m0, sizeof(Matrix));
	invm(m);

	w[0][0] = flt2fix(m[0][0]); w[0][1] = flt2fix(m[0][1]); w[0][2] = flt2fix(m[0][2]);
	w[1][0] = flt2fix(m[1][0]); w[1][1] = flt2fix(m[1][1]); w[1][2] = flt2fix(m[1][2]);
	w[2][0] = 0; w[2][1] = 0; w[2][2] = 1<<13;
}
