#include <u.h>
#include <libc.h>
#include <geometry.h>

int
vfmt(Fmt *f)
{
	Point2 p;

	p = va_arg(f->args, Point2);
	return fmtprint(f, "[%g %g %g]", p.x, p.y, p.w);
}

int
Vfmt(Fmt *f)
{
	Point3 p;

	p = va_arg(f->args, Point3);
	return fmtprint(f, "[%g %g %g %g]", p.x, p.y, p.z, p.w);
}

void
GEOMfmtinstall(void)
{
	fmtinstall('v', vfmt);
	fmtinstall('V', Vfmt);
}
