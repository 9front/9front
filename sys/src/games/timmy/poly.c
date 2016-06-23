#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include "dat.h"
#include "fns.h"

Poly *
mkpoly(int n, ...)
{
	Poly *p;
	int i;
	va_list va;
	
	p = emalloc(sizeof(Poly));
	p->nvert = n;
	p->vert = emalloc((n + 1) * sizeof(Vec));
	va_start(va, n);
	for(i = 0; i < n; i++){
		p->vert[i].x = va_arg(va, double);
		p->vert[i].y = va_arg(va, double);
	}
	p->vert[n] = p->vert[0];
	va_end(va);
	polyfix(p);
	return p;
}

void
polyfix(Poly *o)
{
	double I, A, x, y, t;
	Vec *p, *q;
	int i;

	I = 0;
	A = 0;
	x = 0;
	y = 0;
	for(i = 0; i < o->nvert; i++){
		p = &o->vert[i];
		q = p + 1;
		t = p->x * q->y - p->y * q->x;
		A += t;
		x += (p->x + q->x) * t / 3;
		y += (p->y + q->y) * t / 3;
	}
	x /= A;
	y /= A;
	for(i = 0; i <= o->nvert; i++){
		o->vert[i].x -= x;
		o->vert[i].y -= y;
	}
	for(i = 0; i < o->nvert; i++){
		p = &o->vert[i];
		q = p + 1;
		t = p->x * q->y - p->y * q->x;
		I += (p->x * (p->x + q->x) + q->x * q->x + p->y * (p->y + q->y) + q->y * q->y) * t / 6;
	}
	o->invI = A / I;
}

Poly *
polydup(Poly *p)
{
	Poly *q;
	int i;
	
	q = emalloc(sizeof(Poly));
	q->nvert = p->nvert;
	q->vert = emalloc((p->nvert + 1) * sizeof(Vec));
	for(i = 0; i <= p->nvert; i++)
		q->vert[i] = p->vert[i];
	q->invI = p->invI;
	return q;
}

void
polytrans(Poly *sp, Poly *dp, double x0, double y0, double θ)
{
	int i;
	double c, s, x, y;

	assert(sp->nvert == dp->nvert);
	c = cos(θ * DEG);
	s = sin(θ * DEG);
	for(i = 0; i <= sp->nvert; i++){
		x = sp->vert[i].x;
		y = sp->vert[i].y;
		dp->vert[i].x = x0 + x * c - y * s;
		dp->vert[i].y = y0 + x * s + y * c;
	}
	dp->invI = sp->invI;
}

void
polybbox(Poly *sp, Rectangle *t)
{
	int fx, fy, cx, cy, i;

	t->min.x = floor(sp->vert[0].x - Slop);
	t->max.x = ceil(sp->vert[0].x + Slop) + 1;
	t->min.y = floor(sp->vert[0].y - Slop);
	t->max.y = ceil(sp->vert[0].y + Slop) + 1;
	for(i = 1; i < sp->nvert; i++){
		fx = sp->vert[i].x;
		cx = ceil(fx + Slop); fx = floor(fx - Slop);
		fy = sp->vert[i].y + 1;
		cy = ceil(fy + Slop); fy = floor(fy - Slop);
		if(fx < t->min.x) t->min.x = fx;
		if(cx > t->max.x) t->max.x = cx;
		if(fy < t->min.y) t->min.y = fy;
		if(cy > t->max.y) t->max.y = cy;
	}
}

void
polydraw(Poly *p, Image *d, Image *fill, Image *line)
{
	int i;
	static int maxp;
	static Point *buf;
	
	if(p->nvert + 1 > maxp){
		maxp = p->nvert + 1;
		free(buf);
		buf = emalloc((p->nvert + 1) * sizeof(Point));
	}
	for(i = 0; i <= p->nvert; i++){
		buf[i].x = d->r.min.x + (int)(p->vert[i].x + 0.5);
		buf[i].y = d->r.min.y + (int)(p->vert[i].y + 0.5);
	}
	if(fill != nil) fillpoly(d, buf, p->nvert + 1, 0, fill, ZP);
	if(line != nil) poly(d, buf, p->nvert + 1, 0, 0, 0, line, ZP);
}

void
freepoly(Poly *p)
{
	if(p == nil) return;
	free(p->vert);
	free(p);
}

Hinge *
hingedup(Hinge *h, Obj *o)
{
	Hinge *p, **hp, *r;
	
	r = nil;
	hp = &r;
	for(; h != nil; h = h->onext){
		p = emalloc(sizeof(Hinge));
		p->p = h->p;
		p->p0 = h->p0;
		p->o = o;
		p->cnext = p->cprev = p;
		*hp = p;
		hp = &p->onext;
	}
	return r;
}

Obj *
mkobj(ObjT *t)
{
	Obj *o;
	
	o = emalloc(sizeof(Obj));
	o->tab = t;
	o->hinge = hingedup(t->hinge, o);
	o->tab->init(o);
	o->next = o->prev = o;
	return o;
}

Obj *
objdup(Obj *o)
{
	Obj *p;
	
	p = emalloc(sizeof(Obj));
	*p = *o;
	p->poly = polydup(o->poly);
	p->next = p->prev = p;
	p->hinge = hingedup(p->hinge, p);
	return p;
}

void
objcat(Obj *l, Obj *o)
{
	o->prev = l->prev;
	o->next = l;
	o->prev->next = o;
	o->next->prev = o;
}

static int
polycheck(Poly *a, Poly *b)
{
	int i, j;
	Vec *x, *y, *z;
	double d, m;

	for(i = 0; i < a->nvert; i++){
		z = &a->vert[i];
		m = Inf(1);
		for(j = 0; j < b->nvert; j++){
			x = &b->vert[j];
			y = x + 1;
			d = (z->y - x->y) * (y->x - x->x) - (z->x - x->x) * (y->y - x->y);
			d /= vecdist(*x, *y);
			if(d < -Slop) goto next;
			if(d < m){
				if(m < 0) goto next;
				m = d;
			}else if(d < 0) goto next;
		}
		return 1;
	next:;
	}
	return 0;
}

int
objcoll(Obj *a, Obj *b)
{
	if(!rectXrect(a->bbox, b->bbox)) return 0;
	if(a->poly == nil || b->poly == nil) return 0;
	return polycheck(a->poly, b->poly) || polycheck(b->poly, a->poly);
}

void
objexcise(Obj *o)
{
	Hinge *h;

	o->next->prev = o->prev;
	o->prev->next = o->next;
	o->next = o;
	o->prev = o;
	for(h = o->hinge; h != nil; h = h->onext){
		h->cprev->cnext = h->cnext;
		h->cnext->cprev = h->cprev;
		h->cprev = h;
		h->cnext = h;
	}
}

void
freeobj(Obj *o)
{
	if(o == nil) return;
	objexcise(o);
	freepoly(o->poly);
	free(o);
}

int
hinged(Obj *a, Obj *b)
{
	Hinge *k, *l, *m;
	
	if(b->hinge == nil) return 0;
	for(k = a->hinge; k != nil; k = k->onext)
		for(l = k->cnext; l != k; l = l->cnext)
			for(m = b->hinge; m != nil; m = m->onext)
				if(m == l)
					return 1;
	return 0;
}

Hinge *hinges;

void
hingepairs(Obj *l)
{
	Obj *o;
	Hinge *h, *hh;
	Hinge **p;

	hinges = nil;
	p = &hinges;
	for(o = l->next; o != l; o = o->next)
		for(h = o->hinge; h != nil; h = h->onext){
			for(hh = h->cnext; hh != h; hh = hh->cnext)
				if(hh < h)
					break;
			if(hh == h) continue;
			*p = h;
			p = &h->anext;
		}
}

void
copyhinges(Obj *sl, Obj *dl)
{
	Obj *o, *p, **ol;
	Hinge *h, *k, *l;
	int n;
	
	for(n = 0, o = sl->next; o != sl; o = o->next)
		o->idx = n++;
	ol = emalloc(sizeof(Obj *) * n);
	for(n = 0, o = dl->next; o != dl; o = o->next)
		ol[n++] = o;
	for(o = sl->next, p = dl->next; o != sl; o = o->next, p = p->next){
		for(h = o->hinge, k = p->hinge; h != nil; h = h->onext, k = k->onext){
			if(h->cnext == h) continue;
			for(l = h->cnext->o->hinge, n = 0; l != h->cnext; l = l->onext)
				n++;
			for(l = ol[h->cnext->o->idx]->hinge; n != 0; n--)
				l = l->onext;
			l->cprev->cnext = k->cnext;
			k->cnext->cprev = l->cprev;
			k->cnext = l;
			l->cprev = k;
		}
	}
	hingepairs(dl);
}
