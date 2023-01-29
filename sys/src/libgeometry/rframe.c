#include <u.h>
#include <libc.h>
#include <geometry.h>

Point2
rframexform(Point2 p, RFrame rf)
{
	Matrix m = {
		rf.bx.x, rf.bx.y, -dotvec2(rf.bx, rf.p),
		rf.by.x, rf.by.y, -dotvec2(rf.by, rf.p),
		0, 0, 1,
	};
	return xform(p, m);
}

Point3
rframexform3(Point3 p, RFrame3 rf)
{
	Matrix3 m = {
		rf.bx.x, rf.bx.y, rf.bx.z, -dotvec3(rf.bx, rf.p),
		rf.by.x, rf.by.y, rf.by.z, -dotvec3(rf.by, rf.p),
		rf.bz.x, rf.bz.y, rf.bz.z, -dotvec3(rf.bz, rf.p),
		0, 0, 0, 1,
	};
	return xform3(p, m);
}

Point2
invrframexform(Point2 p, RFrame rf)
{
	Matrix m = {
		rf.bx.x, rf.bx.y, -dotvec2(rf.bx, rf.p),
		rf.by.x, rf.by.y, -dotvec2(rf.by, rf.p),
		0, 0, 1,
	};
	invm(m);
	return xform(p, m);
}

Point3
invrframexform3(Point3 p, RFrame3 rf)
{
	Matrix3 m = {
		rf.bx.x, rf.bx.y, rf.bx.z, -dotvec3(rf.bx, rf.p),
		rf.by.x, rf.by.y, rf.by.z, -dotvec3(rf.by, rf.p),
		rf.bz.x, rf.bz.y, rf.bz.z, -dotvec3(rf.bz, rf.p),
		0, 0, 0, 1,
	};
	invm3(m);
	return xform3(p, m);
}
