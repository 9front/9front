#include <u.h>
#include <libc.h>
#include <geometry.h>

/* 2D */

Point2
centroid(Triangle2 t)
{
	return divpt2(addpt2(t.p0, addpt2(t.p1, t.p2)), 3);
}

/*
 * based on the implementation from:
 *
 * Dmitry V. Sokolov, “Tiny Renderer: Lesson 2”,
 * https://github.com/ssloy/tinyrenderer/wiki/Lesson-2:-Triangle-rasterization-and-back-face-culling
 */
Point3
barycoords(Triangle2 t, Point2 p)
{
	Point2 p0p1 = subpt2(t.p1, t.p0);
	Point2 p0p2 = subpt2(t.p2, t.p0);
	Point2 pp0  = subpt2(t.p0, p);

	Point3 v = crossvec3(Vec3(p0p2.x, p0p1.x, pp0.x), Vec3(p0p2.y, p0p1.y, pp0.y));

	/* handle degenerate triangles—i.e. the ones where every point lies on the same line */
	if(fabs(v.z) < 1)
		return Pt3(-1,-1,-1,1);
	return Pt3(1 - (v.x + v.y)/v.z, v.y/v.z, v.x/v.z, 1);
}

/* 3D */

Point3
centroid3(Triangle3 t)
{
	return divpt3(addpt3(t.p0, addpt3(t.p1, t.p2)), 3);
}
