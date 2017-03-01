#include <u.h>
#include <libc.h>
#include <draw.h>
#include "galaxy.h"

void
growquads(void)
{
	quads.max *= 2;
	quads.a = realloc(quads.a, sizeof(Quad) * quads.max);
	if(quads.a == nil)
		sysfatal("could not realloc quads: %r");
}

Quad*
quad(Body *b)
{
	Quad *q;

	if(quads.l == quads.max)
		return nil;

	q = quads.a + quads.l++;
	memset(q->c, 0, sizeof(QB)*4);
	q->x = b->x;
	q->y = b->y;
	q->mass = b->mass;
	return q;
}

int
quadins(Body *nb, double size)
{
	QB *qb;
	Quad *q;
	Body *b;
	double mass, qx, qy;
	uint qxy;
	int d;

	if(space.t == EMPTY) {
		space.t = BODY;
		space.b = nb;
		return 0;
	}

	d = 0;
	qb = &space;
	qx = 0.0;
	qy = 0.0;
	for(;;) {
		if(qb->t == BODY) {
			b = qb->b;
			qxy = b->x < qx ? 0 : 1;
			qxy |= b->y < qy ? 0 : 2;
			qb->t = QUAD;
			if((qb->q = quad(b)) == nil)
				return -1;
			qb->q->c[qxy].t = BODY;
			qb->q->c[qxy].b = b;
		}

		q = qb->q;
		mass = q->mass + nb->mass;
		q->x = (q->x*q->mass + nb->x*nb->mass) / mass;
		q->y = (q->y*q->mass + nb->y*nb->mass) / mass;
		q->mass = mass;

		qxy = nb->x < qx ? 0 : 1;
		qxy |= nb->y < qy ? 0 : 2;
		if(q->c[qxy].t == EMPTY) {
			q->c[qxy].t = BODY;
			q->c[qxy].b = nb;
			STATS(if(d > quaddepth) quaddepth = d;)
			return 0;
		}

		STATS(d++;)
		qb = &q->c[qxy];
		size /= 2;
		qx += qxy&1 ? size/2 : -size/2;
		qy += qxy&2 ? size/2 : -size/2;
	}
}

void
quadcalc(Body *b, QB qb, double size)
{
	double fx÷❨m₁m₂❩, fy÷❨m₁m₂❩, dx, dy, h, G÷h³;

	for(;;) switch(qb.t) {
	default:
		sysfatal("quadcalc: No such type");
	case EMPTY:
		return;
	case BODY:
		if(qb.b == b)
			return;
		dx = qb.b->x - b->x;
		dy = qb.b->y - b->y;
		h = hypot(hypot(dx, dy), ε);
		G÷h³ = G / (h*h*h);
		fx÷❨m₁m₂❩ = dx * G÷h³;
		fy÷❨m₁m₂❩ = dy * G÷h³;
		b->newa.x += fx÷❨m₁m₂❩ * qb.b->mass;
		b->newa.y += fy÷❨m₁m₂❩ * qb.b->mass;
		STATS(calcs++;)
		return;
	case QUAD:
		dx = qb.q->x - b->x;
		dy = qb.q->y - b->y;
		h = hypot(dx, dy);
		if(h != 0.0 && size/h < θ) {
			h = hypot(h, ε);
			G÷h³ = G / (h*h*h);
			fx÷❨m₁m₂❩ = dx * G÷h³;
			fy÷❨m₁m₂❩ = dy * G÷h³;
			b->newa.x += fx÷❨m₁m₂❩ * qb.q->mass;
			b->newa.y += fy÷❨m₁m₂❩ * qb.q->mass;
			STATS(calcs++;)
			return;
		}
		size /= 2;
		quadcalc(b, qb.q->c[0], size);
		quadcalc(b, qb.q->c[1], size);
		quadcalc(b, qb.q->c[2], size);
		qb = qb.q->c[3];
		break;	/* quadcalc(q->q[3], b, size); */
	}
}

void
quadsinit(void)
{
	quads.a = calloc(5, sizeof(Body));
	if(quads.a == nil)
		sysfatal("could not allocate quads: %r");
	quads.l = 0;
	quads.max = 5;
}
