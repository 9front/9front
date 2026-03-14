#include <u.h>
#include <libc.h>
#include <geometry.h>

Point2 ZP2;
Point3 ZP3;

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
	return (Point2){a.x+b.x, a.y+b.y, a.w+b.w};
}

Point2
subpt2(Point2 a, Point2 b)
{
	return (Point2){a.x-b.x, a.y-b.y, a.w-b.w};
}

Point2
mulpt2(Point2 p, double s)
{
	return (Point2){p.x*s, p.y*s, p.w*s};
}

Point2
divpt2(Point2 p, double s)
{
	return (Point2){p.x/s, p.y/s, p.w/s};
}

Point2
lerp2(Point2 a, Point2 b, double t)
{
	t = fclamp(t, 0, 1);
	return (Point2){
		flerp(a.x, b.x, t),
		flerp(a.y, b.y, t),
		flerp(a.w, b.w, t)
	};
}

Point2
berp2(Point2 a, Point2 b, Point2 c, Point3 bc)
{
	return addpt2(addpt2(
		mulpt2(a, bc.x),
		mulpt2(b, bc.y)),
		mulpt2(c, bc.z));
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
		return (Point2){0,0,0};
	return (Point2){v.x/len, v.y/len, 0};
}

Point2
centroid(Point2 *pts, ulong npts)
{
	Point2 p;
	ulong i;

	p = pts[0];
	for(i = 1; i < npts; i++)
		p = addpt2(p, pts[i]);
	return divpt2(p, npts);
}

/*
 * based on the implementation from:
 *
 * Dmitry V. Sokolov, “Tiny Renderer: Lesson 2”,
 * https://github.com/ssloy/tinyrenderer/wiki/Lesson-2:-Triangle-rasterization-and-back-face-culling
 */
Point3
barycoords(Point2 p0, Point2 p1, Point2 p2, Point2 p)
{
	Point2 p0p1 = subpt2(p1, p0);
	Point2 p0p2 = subpt2(p2, p0);
	Point2 pp0  = subpt2(p0, p);

	Point3 v = crossvec3(Vec3(p0p2.x, p0p1.x, pp0.x), Vec3(p0p2.y, p0p1.y, pp0.y));

	/* handle degenerate triangles—i.e. the ones where every point lies on the same line */
	if(fabs(v.z) < 1)
		return Pt3(-1,-1,-1,1);
	return Pt3(1 - (v.x + v.y)/v.z, v.y/v.z, v.x/v.z, 1);
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
	return (Point3){a.x+b.x, a.y+b.y, a.z+b.z, a.w+b.w};
}

Point3
subpt3(Point3 a, Point3 b)
{
	return (Point3){a.x-b.x, a.y-b.y, a.z-b.z, a.w-b.w};
}

Point3
mulpt3(Point3 p, double s)
{
	return (Point3){p.x*s, p.y*s, p.z*s, p.w*s};
}

Point3
divpt3(Point3 p, double s)
{
	return (Point3){p.x/s, p.y/s, p.z/s, p.w/s};
}

Point3
lerp3(Point3 a, Point3 b, double t)
{
	t = fclamp(t, 0, 1);
	return (Point3){
		flerp(a.x, b.x, t),
		flerp(a.y, b.y, t),
		flerp(a.z, b.z, t),
		flerp(a.w, b.w, t)
	};
}

Point3
berp3(Point3 a, Point3 b, Point3 c, Point3 bc)
{
	return addpt3(addpt3(
		mulpt3(a, bc.x),
		mulpt3(b, bc.y)),
		mulpt3(c, bc.z));
}

double
dotvec3(Point3 a, Point3 b)
{
	return a.x*b.x + a.y*b.y + a.z*b.z;
}

Point3
crossvec3(Point3 a, Point3 b)
{
	return (Point3){
		a.y*b.z - a.z*b.y,
		a.z*b.x - a.x*b.z,
		a.x*b.y - a.y*b.x,
		0
	};
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
		return (Point3){0,0,0,0};
	return (Point3){v.x/len, v.y/len, v.z/len, 0};
}

Point3
centroid3(Point3 *pts, ulong npts)
{
	Point3 p;
	ulong i;

	p = pts[0];
	for(i = 1; i < npts; i++)
		p = addpt3(p, pts[i]);
	return divpt3(p, npts);
}

int
lineXsphere(Point3 *rp, Point3 p0, Point3 p1, Point3 c, double r, int isaray)
{
	Point3 u, dp;
	double u·dp, Δ, d;

	u = normvec3(subpt3(p1, p0));
	dp = subpt3(p1, c);
	u·dp = dotvec3(u, dp);
	if(isaray && u·dp > 0)	/* ignore what's behind */
		return 0;

	Δ = u·dp*u·dp - dotvec3(dp, dp) + r*r;
	if(Δ < 0)		/* no intersection */
		return 0;
	else if(Δ == 0){	/* tangent */
		if(rp != nil){
			d = -u·dp;
			rp[0] = addpt3(p0, mulpt3(u, d));
		}
		return 1;
	}else{			/* secant */
		if(rp != nil){
			d = -u·dp + sqrt(Δ);
			rp[0] = addpt3(p0, mulpt3(u, d));
			d = -u·dp - sqrt(Δ);
			rp[1] = addpt3(p0, mulpt3(u, d));
		}
		return 2;
	}
}

/*
 * p is the point to test
 * p0 and p1 are the centers of the circles at each end of the cylinder
 * r is the radius of these circles
 */
int
ptincylinder(Point3 p, Point3 p0, Point3 p1, double r)
{
	Point3 p01, p0p, p1p;
	double h;

	p01 = subpt3(p1, p0);
	p0p = subpt3(p, p0);
	p1p = subpt3(p, p1);
	h = vec3len(p01);

	if(h == 0)
		return 0;

	return dotvec3(p0p, p01) >= 0 &&
		dotvec3(p1p, p01) <= 0 &&
		vec3len(crossvec3(p0p, p01))/h <= r;
}

/*
 * p is the point to test
 * p0 is the apex
 * p1 is the center of the base
 * br is the radius of the base
 */
int
ptincone(Point3 p, Point3 p0, Point3 p1, double br)
{
	Point3 p01, p0p;
	double h, d, r;

	p01 = subpt3(p1, p0);
	p0p = subpt3(p, p0);
	h = vec3len(p01);
	d = dotvec3(p0p, normvec3(p01));

	if(h == 0 || d < 0 || d > h)
		return 0;

	r = d/h * br;
	return vec3len(crossvec3(p0p, p01))/h <= r;
}
