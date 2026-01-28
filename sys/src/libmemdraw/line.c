#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <memlayer.h>

enum
{
	Arrow1 = 8,
	Arrow2 = 10,
	Arrow3 = 3,
};

static Memimage*
membrush(int radius)
{
	static Memimage *brush;
	static int brushradius;

	if(brush==nil || brushradius!=radius){
		freememimage(brush);
		brush = allocmemimage(Rect(0, 0, 2*radius+1, 2*radius+1), memopaque->chan);
		if(brush != nil){
			memfillcolor(brush, DTransparent);	/* zeros */
			memellipse(brush, Pt(radius, radius), radius, radius, -1, memopaque, Pt(radius, radius), S);
		}
		brushradius = radius;
	}
	return brush;
}

static void
discend(Point p, int radius, Memimage *dst, Memimage *src, Point dsrc, int op)
{
	Memimage *disc;
	Rectangle r;

	disc = membrush(radius);
	if(disc != nil){
		r.min.x = p.x - radius;
		r.min.y = p.y - radius;
		r.max.x = p.x + radius+1;
		r.max.y = p.y + radius+1;
		memdraw(dst, r, src, addpt(r.min, dsrc), disc, ZP, op);
	}
}

static void
arrowend(Point tip, Point *pp, int end, int sin, int cos, int radius)
{
	int x1, x2, x3;

	/* before rotation */
	if(end == Endarrow){
		x1 = Arrow1;
		x2 = Arrow2;
		x3 = Arrow3;
	}else{
		x1 = (end>>5) & 0x1FF;	/* distance along line from end of line to tip */
		x2 = (end>>14) & 0x1FF;	/* distance along line from barb to tip */
		x3 = (end>>23) & 0x1FF;	/* distance perpendicular from edge of line to barb */
	}

	/* comments follow track of right-facing arrowhead */
	pp->x = tip.x+((2*radius+1)*sin/2-x1*cos);		/* upper side of shaft */
	pp->y = tip.y-((2*radius+1)*cos/2+x1*sin);
	pp++;
	pp->x = tip.x+((2*radius+2*x3+1)*sin/2-x2*cos);		/* upper barb */
	pp->y = tip.y-((2*radius+2*x3+1)*cos/2+x2*sin);
	pp++;
	pp->x = tip.x;
	pp->y = tip.y;
	pp++;
	pp->x = tip.x+(-(2*radius+2*x3+1)*sin/2-x2*cos);	/* lower barb */
	pp->y = tip.y-(-(2*radius+2*x3+1)*cos/2+x2*sin);
	pp++;
	pp->x = tip.x+(-(2*radius+1)*sin/2-x1*cos);		/* lower side of shaft */
	pp->y = tip.y+((2*radius+1)*cos/2-x1*sin);
}

/*
 * NOTE: Clipping is a little peculiar.  We can't use Sutherland-Cohen
 * clipping because lines are wide.  But this is probably just fine:
 * we do all math with the original p0 and p1, but clip when deciding
 * what pixels to draw.  This means the layer code can call this routine,
 * using clipr to define the region being written, and get the same set
 * of pixels regardless of the dicing.
 */
void
_memimageline(Memimage *dst, Point p0, Point p1, int end0, int end1, int radius, Memimage *src, Point sp, Rectangle clipr, int op)
{
	int hor;
	int sin, cos, dx, dy, t;
	Rectangle oclipr, r;
	Point q, pts[10], *pp, d;

	if(radius < 0)
		return;
	if(rectclip(&clipr, dst->r) == 0)
		return;
	if(rectclip(&clipr, dst->clipr) == 0)
		return;
	d = subpt(sp, p0);
	if(rectclip(&clipr, rectsubpt(src->clipr, d)) == 0)
		return;
	if((src->flags&Frepl)==0 && rectclip(&clipr, rectsubpt(src->r, d))==0)
		return;
	hor = (abs(p1.x-p0.x) > abs(p1.y-p0.y));
	if((hor && p0.x>p1.x) || (!hor && p0.y>p1.y)){
		q = p0;
		p0 = p1;
		p1 = q;
		t = end0;
		end0 = end1;
		end1 = t;
	}

	/* handle purely vertical or horizontal lines */
	if(p0.x == p1.x || p0.y == p1.y){
		r.min = p0;
		r.max = addpt(p1, Pt(1, 1));
		if(p0.x == p1.x){
			r.min.x -= radius;
			r.max.x += radius;
			cos = 0; sin = ICOSSCALE;
		}
		else{
			r.min.y -= radius;
			r.max.y += radius;
			cos = ICOSSCALE; sin = 0;
		}
		oclipr = dst->clipr;
		dst->clipr = clipr;
		switch(end0 & 0x1F){
		case Enddisc:
			discend(p0, radius, dst, src, d, op);
			break;
		case Endarrow:
			q.x = ICOSSCALE*p0.x+ICOSSCALE/2-cos/2;
			q.y = ICOSSCALE*p0.y+ICOSSCALE/2-sin/2;
			arrowend(q, pts, end0, -sin, -cos, radius);
			_memfillpolysc(dst, pts, 5, ~0, src, addpt(pts[0], mulpt(d, ICOSSCALE)), 1, 10, op);
			if(cos)
				r.min.x = pts[0].x >> 10;
			else
				r.min.y = pts[0].y >> 10;
			break;
		}
		switch(end1 & 0x1F){
		case Enddisc:
			discend(p1, radius, dst, src, d, op);
			break;
		case Endarrow:
			q.x = ICOSSCALE*p1.x+ICOSSCALE/2+cos/2;
			q.y = ICOSSCALE*p1.y+ICOSSCALE/2+sin/2;
			arrowend(q, pts, end1, sin, cos, radius);
			_memfillpolysc(dst, pts, 5, ~0, src, addpt(pts[0], mulpt(d, ICOSSCALE)), 1, 10, op);
			if(cos)
				r.max.x = pts[0].x >> 10;
			else
				r.max.y = pts[0].y >> 10;
			break;
		}
		sp = addpt(r.min, d);
		memimagedraw(dst, r, src, sp, memopaque, sp, op);
		dst->clipr = oclipr;
		return;
	}

	/* draw thick line using polygon fill */
	icossin2(p1.x-p0.x, p1.y-p0.y, &cos, &sin);
	dx = (sin*(2*radius+1))/2;
	dy = (cos*(2*radius+1))/2;
	pp = pts;
	oclipr = dst->clipr;
	dst->clipr = clipr;
	q.x = ICOSSCALE*p0.x+ICOSSCALE/2-cos/2;
	q.y = ICOSSCALE*p0.y+ICOSSCALE/2-sin/2;
	switch(end0 & 0x1F){
	case Enddisc:
		discend(p0, radius, dst, src, d, op);
		/* fall through */
	case Endsquare:
	default:
		pp->x = q.x-dx;
		pp->y = q.y+dy;
		pp++;
		pp->x = q.x+dx;
		pp->y = q.y-dy;
		pp++;
		break;
	case Endarrow:
		arrowend(q, pp, end0, -sin, -cos, radius);
		_memfillpolysc(dst, pts, 5, ~0, src, addpt(pts[0], mulpt(d, ICOSSCALE)), 1, 10, op);
		pp[1] = pp[4];
		pp += 2;
	}
	q.x = ICOSSCALE*p1.x+ICOSSCALE/2+cos/2;
	q.y = ICOSSCALE*p1.y+ICOSSCALE/2+sin/2;
	switch(end1 & 0x1F){
	case Enddisc:
		discend(p1, radius, dst, src, d, op);
		/* fall through */
	case Endsquare:
	default:
		pp->x = q.x+dx;
		pp->y = q.y-dy;
		pp++;
		pp->x = q.x-dx;
		pp->y = q.y+dy;
		pp++;
		break;
	case Endarrow:
		arrowend(q, pp, end1, sin, cos, radius);
		_memfillpolysc(dst, pp, 5, ~0, src, addpt(pp[0], mulpt(d, ICOSSCALE)), 1, 10, op);
		pp[1] = pp[4];
		pp += 2;
	}
	_memfillpolysc(dst, pts, pp-pts, ~0, src, addpt(pts[0], mulpt(d, ICOSSCALE)), 0, 10, op);
	dst->clipr = oclipr;
	return;
}

void
memimageline(Memimage *dst, Point p0, Point p1, int end0, int end1, int radius, Memimage *src, Point sp, int op)
{
	_memimageline(dst, p0, p1, end0, end1, radius, src, sp, dst->clipr, op);
}

/*
 * Simple-minded conservative code to compute bounding box of line.
 * Result is probably a little larger than it needs to be.
 */
static void
addbbox(Rectangle *r, Point p)
{
	if(r->min.x > p.x)
		r->min.x = p.x;
	if(r->min.y > p.y)
		r->min.y = p.y;
	if(r->max.x < p.x+1)
		r->max.x = p.x+1;
	if(r->max.y < p.y+1)
		r->max.y = p.y+1;
}

int
memlineendsize(int end)
{
	int x3;

	if((end&0x3F) != Endarrow)
		return 0;
	if(end == Endarrow)
		x3 = Arrow3;
	else
		x3 = (end>>23) & 0x1FF;
	return x3;
}

static int
lmax(int a, int b)
{
	if(a > b)
		return a;
	return b;
}

Rectangle
memlinebbox(Point p0, Point p1, int end0, int end1, int radius)
{
	Rectangle r, r1;
	int extra;

	r.min.x = 10000000;
	r.min.y = 10000000;
	r.max.x = -10000000;
	r.max.y = -10000000;
	extra = lmax(memlineendsize(end0), memlineendsize(end1));
	r1 = insetrect(canonrect(Rpt(p0, p1)), -(radius+extra));
	addbbox(&r, r1.min);
	addbbox(&r, r1.max);
	return r;
}
