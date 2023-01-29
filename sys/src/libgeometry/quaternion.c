#include <u.h>
#include <libc.h>
#include <geometry.h>

Quaternion
Quat(double r, double i, double j, double k)
{
	return (Quaternion){r, i, j, k};
}

Quaternion
Quatvec(double s, Point3 v)
{
	return (Quaternion){s, v.x, v.y, v.z};
}

Quaternion
addq(Quaternion a, Quaternion b)
{
	return Quat(a.r+b.r, a.i+b.i, a.j+b.j, a.k+b.k);
}

Quaternion
subq(Quaternion a, Quaternion b)
{
	return Quat(a.r-b.r, a.i-b.i, a.j-b.j, a.k-b.k);
}

Quaternion
mulq(Quaternion q, Quaternion r)
{
	Point3 qv, rv, tmp;

	qv = Vec3(q.i, q.j, q.k);
	rv = Vec3(r.i, r.j, r.k);
	tmp = addpt3(addpt3(mulpt3(rv, q.r), mulpt3(qv, r.r)), crossvec3(qv, rv));
	return Quatvec(q.r*r.r - dotvec3(qv, rv), tmp);
}

Quaternion
smulq(Quaternion q, double s)
{
	return Quat(q.r*s, q.i*s, q.j*s, q.k*s);
}

Quaternion
sdivq(Quaternion q, double s)
{
	return Quat(q.r/s, q.i/s, q.j/s, q.k/s);
}

double
dotq(Quaternion q, Quaternion r)
{
	return q.r*r.r + q.i*r.i + q.j*r.j + q.k*r.k;
}

Quaternion
invq(Quaternion q)
{
	double len²;

	len² = dotq(q, q);
	if(len² == 0)
		return Quat(0,0,0,0);
	return Quat(q.r/len², -q.i/len², -q.j/len², -q.k/len²);
}

double
qlen(Quaternion q)
{
	return sqrt(dotq(q, q));
}

Quaternion
normq(Quaternion q)
{
	return sdivq(q, qlen(q));
}

/*
 * based on the implementation from:
 *
 * Jonathan Blow, “Understanding Slerp, Then Not Using it”,
 * The Inner Product, April 2004.
 */
Quaternion
slerp(Quaternion q, Quaternion r, double t)
{
	Quaternion v;
	double θ, q·r;

	q·r = fclamp(dotq(q, r), -1, 1); /* stay within the domain of acos(2) */
	θ = acos(q·r)*t;
	v = normq(subq(r, smulq(q, q·r))); /* v = r - (q·r)q / |v| */
	return addq(smulq(q, cos(θ)), smulq(v, sin(θ))); /* q cos(θ) + v sin(θ) */
}

Point3
qrotate(Point3 p, Point3 axis, double θ)
{
	Quaternion qaxis, qr;

	θ /= 2;
	qaxis = Quatvec(cos(θ), mulpt3(axis, sin(θ)));
	qr = mulq(mulq(qaxis, Quatvec(0, p)), invq(qaxis)); /* qpq⁻¹ */
	return Pt3(qr.i, qr.j, qr.k, p.w);
}
