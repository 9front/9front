#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

void
objpolyinit(Obj *o)
{
	o->poly = polydup(o->tab->poly);
}

void
objpolymove(Obj *o, double x, double y, double θ)
{
	Hinge *h;
	double c, s;

	o->p.x = x;
	o->p.y = y;
	o->θ = θ;
	polytrans(o->tab->poly, o->poly, x, y, θ);
	polybbox(o->poly, &o->bbox);
	if(o->hinge != nil){
		c = cos(θ * DEG);
		s = sin(θ * DEG);
		for(h = o->hinge; h != nil; h = h->onext){
			h->p.x = c * h->p0.x - s * h->p0.y + x;
			h->p.y = s * h->p0.x + c * h->p0.y + y;
		}
	}
}

void
objpolydraw(Obj *o, Image *i)
{
	Hinge *h;
	Point p;

	polydraw(o->poly, i, o->tab->fill, o->tab->line);
	for(h = o->hinge; h != nil; h = h->onext){
		p.x = i->r.min.x + (int)(h->p.x + 0.5);
		p.y = i->r.min.y + (int)(h->p.y + 0.5);
		ellipse(i, p, 2, 2, 0, display->black, ZP);
	}
}

void
mkobjpoly(ObjT *t, Poly *p, Image *fill, Image *line, double imass)
{
	t->init = objpolyinit;
	t->draw = objpolydraw;
	t->move = objpolymove;
	t->poly = p;
	t->line = line;
	t->fill = fill;
	t->imass = imass;
}

void
addhinge(ObjT *t, double x, double y)
{
	Hinge *h, **hp;
	
	h = emalloc(sizeof(Hinge));
	h->p.x = x;
	h->p.y = y;
	h->p0 = h->p;
	h->cnext = h->cprev = h;
	for(hp = &t->hinge; *hp != nil; hp = &(*hp)->onext)
		;
	*hp = h;
}

Poly *
mkball(int n)
{
	Poly *p;
	int i;
	
	p = emalloc(sizeof(Poly));
	p->nvert = n;
	p->vert = emalloc(sizeof(Vec) * (n + 1));
	for(i = 0; i < n; i++){
		p->vert[i].x = 10 * cos(2 * PI * i / n);
		p->vert[i].y = 10 * sin(2 * PI * i / n);
	}
	p->vert[n] = p->vert[0];
	polyfix(p);
	return p;
}

ObjT tdomino, tboard, thboard, tball, tfix;

void
simpleinit(void)
{
	mkobjpoly(&tdomino, mkpoly(4, 0.0, 0.0, 10.0, 0.0, 10.0, 40.0, 0.0, 40.0), rgb(0xFF0000FF), display->black, 10);
	mkobjpoly(&tboard, mkpoly(4, 0.0, 0.0, 100.0, 0.0, 100.0, 6.0, 0.0, 6.0), rgb(0x663300FF), nil, 0);
	mkobjpoly(&thboard, mkpoly(4, 0.0, 0.0, 100.0, 0.0, 100.0, 6.0, 0.0, 6.0), rgb(0x884400FF), nil, 0.5);
	addhinge(&thboard, 48.0, 0.0);
	addhinge(&thboard, -48.0, 0.0);
	mkobjpoly(&tball, mkball(17), rgb(0x00FF00FF), nil, 3);
	mkobjpoly(&tfix, mkpoly(3, 0.0, 0.0, 10.0, -17.3, 20.0, 0.0), rgb(0x663300FF), display->black, 0);
	addhinge(&tfix, 0.0, 0.0);
	addtray(&tdomino, &tboard, &thboard, &tfix, &tball, nil);
}
