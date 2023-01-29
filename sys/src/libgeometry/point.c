#include <u.h>
#include <libc.h>
#include <geometry.h>

/* 2D */

Point2
Pt2(double x, double y, double w)
{
	return (Point2){x, y, w};
}

Point2
Vec2(double x, double y)
{
	return (Point2){x, y, 0};
}

Point2
addpt2(Point2 a, Point2 b)
{
	return Pt2(a.x+b.x, a.y+b.y, a.w+b.w);
}

Point2
subpt2(Point2 a, Point2 b)
{
	return Pt2(a.x-b.x, a.y-b.y, a.w-b.w);
}

Point2
mulpt2(Point2 p, double s)
{
	return Pt2(p.x*s, p.y*s, p.w*s);
}

Point2
divpt2(Point2 p, double s)
{
	return Pt2(p.x/s, p.y/s, p.w/s);
}

Point2
lerp2(Point2 a, Point2 b, double t)
{
	t = fclamp(t, 0, 1);
	return Pt2(
		flerp(a.x, b.x, t),
		flerp(a.y, b.y, t),
		flerp(a.w, b.w, t)
	);
}

double
dotvec2(Point2 a, Point2 b)
{
	return a.x*b.x + a.y*b.y;
}

double
vec2len(Point2 v)
{
	return sqrt(dotvec2(v, v));
}

Point2
normvec2(Point2 v)
{
	double len;

	len = vec2len(v);
	if(len == 0)
		return Pt2(0,0,0);
	return Pt2(v.x/len, v.y/len, 0);
}

/*
 * the edge function, from:
 *
 * Juan Pineda, “A Parallel Algorithm for Polygon Rasterization”,
 * Computer Graphics, Vol. 22, No. 8, August 1988
 *
 * comparison of a point p with an edge [e0 e1]
 * p to the right: +
 * p to the left: -
 * p on the edge: 0
 */
int
edgeptcmp(Point2 e0, Point2 e1, Point2 p)
{
	Point3 e0p, e01, r;

	p = subpt2(p, e0);
	e1 = subpt2(e1, e0);
	e0p = Vec3(p.x,p.y,0);
	e01 = Vec3(e1.x,e1.y,0);
	r = crossvec3(e0p, e01);

	/* clamp to avoid overflow */
	return fclamp(r.z, -1, 1); /* e0.x*e1.y - e0.y*e1.x */
}

/*
 * (PNPOLY) algorithm by W. Randolph Franklin
 */
int
ptinpoly(Point2 p, Point2 *pts, ulong npts)
{
	int i, j, c;

	for(i = c = 0, j = npts-1; i < npts; j = i++)
		if(p.y < pts[i].y != p.y < pts[j].y &&
			p.x < (pts[j].x - pts[i].x) * (p.y - pts[i].y)/(pts[j].y - pts[i].y) + pts[i].x)
		c ^= 1;
	return c;
}

/* 3D */

Point3
Pt3(double x, double y, double z, double w)
{
	return (Point3){x, y, z, w};
}

Point3
Vec3(double x, double y, double z)
{
	return (Point3){x, y, z, 0};
}

Point3
addpt3(Point3 a, Point3 b)
{
	return Pt3(a.x+b.x, a.y+b.y, a.z+b.z, a.w+b.w);
}

Point3
subpt3(Point3 a, Point3 b)
{
	return Pt3(a.x-b.x, a.y-b.y, a.z-b.z, a.w-b.w);
}

Point3
mulpt3(Point3 p, double s)
{
	return Pt3(p.x*s, p.y*s, p.z*s, p.w*s);
}

Point3
divpt3(Point3 p, double s)
{
	return Pt3(p.x/s, p.y/s, p.z/s, p.w/s);
}

Point3
lerp3(Point3 a, Point3 b, double t)
{
	t = fclamp(t, 0, 1);
	return Pt3(
		flerp(a.x, b.x, t),
		flerp(a.y, b.y, t),
		flerp(a.z, b.z, t),
		flerp(a.w, b.w, t)
	);
}

double
dotvec3(Point3 a, Point3 b)
{
	return a.x*b.x + a.y*b.y + a.z*b.z;
}

Point3
crossvec3(Point3 a, Point3 b)
{
	return Pt3(
		a.y*b.z - a.z*b.y,
		a.z*b.x - a.x*b.z,
		a.x*b.y - a.y*b.x,
		0
	);
}

double
vec3len(Point3 v)
{
	return sqrt(dotvec3(v, v));
}

Point3
normvec3(Point3 v)
{
	double len;

	len = vec3len(v);
	if(len == 0)
		return Pt3(0,0,0,0);
	return Pt3(v.x/len, v.y/len, v.z/len, 0);
}
