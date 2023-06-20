#include <u.h>
#include <libc.h>
#include <geometry.h>

/*
 * implicit identity origin rframes
 *
 * 	static RFrame IRF2 = {
 * 		.p  = {0,0,1},
 * 		.bx = {1,0,0},
 * 		.by = {0,1,0},
 * 	};
 * 	
 * 	static RFrame3 IRF3 = {
 * 		.p  = {0,0,0,1},
 * 		.bx = {1,0,0,0},
 * 		.by = {0,1,0,0},
 * 		.bz = {0,0,1,0},
 * 	};
 *
 * these rframes are used on every xform to keep the points in the
 * correct plane (i.e. with proper w values); they are written here as a
 * reference for future changes. the bases are ignored since they turn
 * into an unnecessary identity xform.
 *
 * the implicitness comes from the fact that using the _irf* filters
 * makes the rframexform equivalent to:
 * 	rframexform(invrframexform(p, IRF), rf);
 * and the invrframexform to:
 * 	rframexform(invrframexform(p, rf), IRF);
 */

static Point2
_irfxform(Point2 p)
{
	p.w--;
	return p;
}

static Point2
_irfxform⁻¹(Point2 p)
{
	p.w++;
	return p;
}

static Point3
_irfxform3(Point3 p)
{
	p.w--;
	return p;
}

static Point3
_irfxform3⁻¹(Point3 p)
{
	p.w++;
	return p;
}

Point2
rframexform(Point2 p, RFrame rf)
{
	Matrix m = {
		rf.bx.x, rf.by.x, 0,
		rf.bx.y, rf.by.y, 0,
		0, 0, 1,
	};
	invm(m);
	return xform(subpt2(_irfxform⁻¹(p), rf.p), m);
}

Point3
rframexform3(Point3 p, RFrame3 rf)
{
	Matrix3 m = {
		rf.bx.x, rf.by.x, rf.bz.x, 0,
		rf.bx.y, rf.by.y, rf.bz.y, 0,
		rf.bx.z, rf.by.z, rf.bz.z, 0,
		0, 0, 0, 1,
	};
	invm3(m);
	return xform3(subpt3(_irfxform3⁻¹(p), rf.p), m);
}

Point2
invrframexform(Point2 p, RFrame rf)
{
	Matrix m = {
		rf.bx.x, rf.by.x, 0,
		rf.bx.y, rf.by.y, 0,
		0, 0, 1,
	};
	return _irfxform(addpt2(xform(p, m), rf.p));
}

Point3
invrframexform3(Point3 p, RFrame3 rf)
{
	Matrix3 m = {
		rf.bx.x, rf.by.x, rf.bz.x, 0,
		rf.bx.y, rf.by.y, rf.bz.y, 0,
		rf.bx.z, rf.by.z, rf.bz.z, 0,
		0, 0, 0, 1,
	};
	return _irfxform3(addpt3(xform3(p, m), rf.p));
}
