#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

extern Obj runo;
#define Grav 10
#define Dt 0.01
#define Beta 0.5
int Steps = 4;

typedef struct Contact Contact;
struct Contact {
	Obj *vo, *eo;
	Vec v, n;
	double pen;
	double impn, impt;
	double targun;
};
enum { CTSBLOCK = 64 };

Contact *cts;
int icts, ncts;

static void
colldect(Obj *a, Obj *b)
{
	int i, j;
	Vec *x, *y, *z;
	double d, m;
	Poly *ap, *bp;
	
	ap = a->poly;
	bp = b->poly;
	for(i = 0; i < ap->nvert; i++){
		z = &ap->vert[i];
		m = Inf(1);
		for(j = 0; j < bp->nvert; j++){
			x = &bp->vert[j];
			y = x + 1;
			d = -(z->x - x->x) * (y->y - x->y) + (z->y - x->y) * (y->x - x->x);
			d /= vecdist(*x, *y);
			if(d < -Slop) goto next;
			if(d < m){
				if(m < 0) goto next;
				m = d;
				if(icts == ncts)
					cts = realloc(cts, sizeof(Contact) * (ncts += CTSBLOCK));
				cts[icts] = (Contact){a, b, *z, vecnormal(vecsub(*x, *y)), d, 0, 0, 0};
			}else if(d < 0) goto next;
		}
		icts++;
	next: ;
	}
}

static void
collresp(Contact *p)
{
	double μs, μd;
	Vec n, t, u, r0, r1, Δp;
	double ut, un, α, γ, γt, γn, pt, mt, mn;
	double r0n, r0t, r1n, r1t;
	double mv, me, Iv, Ie;

	n = p->n;
	t = (Vec){n.y, -n.x};
	mv = p->vo->tab->imass;
	me = p->eo->tab->imass;
	Iv = mv * p->vo->poly->invI;
	Ie = me * p->eo->poly->invI;
	r0 = vecsub(p->v, p->vo->p);
	r1 = vecsub(p->v, p->eo->p);
	Δp.x = -(t.x * p->impt + n.x * p->impn);
	Δp.y = -(t.y * p->impt + n.y * p->impn);
	p->vo->v = vecadd(p->vo->v, vecmul(Δp, mv));
	p->vo->ω -= (Δp.x * r0.y - Δp.y * r0.x) * Iv;
	p->eo->v = vecadd(p->eo->v, vecmul(Δp, -me));
	p->eo->ω += (Δp.x * r1.y - Δp.y * r1.x) * Ie;
	u.x = p->vo->v.x - p->vo->ω * r0.y - p->eo->v.x + p->eo->ω * r1.y;
	u.y = p->vo->v.y + p->vo->ω * r0.x - p->eo->v.y - p->eo->ω * r1.x;
	ut = vecdot(u, t);
	un = vecdot(u, n);
	r0t = vecdot(r0, t);
	r0n = vecdot(r0, n);
	r1t = vecdot(r1, t);
	r1n = vecdot(r1, n);
	γ = 0; /* accumulated normal impulse */
	pt = 0; /* accumulated transverse impulse */
	μs = 0.5;
	μd = 0.3;
	
	un += p->targun;
	if(un >= 0 && p->pen <= 0){
		p->impt = 0;
		p->impn = 0;
		return;
	}
	if(p->pen > 0){
		un -= Beta * p->pen / Dt;
		if(un >= 0){
			mn = mv + r0t * r0t * Iv;
			mn += me + r1t * r1t * Ie;
			γ = -un/mn;
			un = 0;
		}
	}
	while(un < 0){
		/* calculate α, the effective coefficient of friction */
		if(ut == 0){
			α = r0t * r0n * Iv + r1t * r1n * Ie;
			α /= mv + r0n * r0n * Iv + me + r1n * r1n * Ie;
			if(α > μs) α = μd;
			else if(α < -μs) α = -μd;
		}else
			α = ut < 0 ? μd : -μd;
	
		mt = α * mv + (r0n * r0n * α - r0t * r0n) * Iv;
		mt += α * me + (r1n * r1n * α - r1t * r1n) * Ie;
		mn = mv + (r0t * r0t - r0n * r0t * α) * Iv;
		mn += me + (r1t * r1t - r1n * r1t * α) * Ie;
		/* determine events which would change α */
		if(ut == 0) γt = Inf(1);
		else{
			γt = γ - ut / mt;
			if(γt < γ) γt = Inf(1);
		}
		γn = γ - un / mn;
		if(γn < γ) γn = Inf(1);
		/* choose earlier one */
		if(γt < γn){
			ut = 0;
			un += mn * (γt - γ);
			pt += (γt - γ) * α;
			γ = γt;
		}else{
			assert(γn < Inf(1));
			un = 0;
			ut += mt * (γn - γ);
			pt += (γn - γ) * α;
			γ = γn;
		}
	}
	
	p->impt = pt;
	p->impn = γ;
	Δp.x = t.x * pt + n.x * γ;
	Δp.y = t.y * pt + n.y * γ;
	p->vo->v = vecadd(p->vo->v, vecmul(Δp, mv));
	p->vo->ω -= (Δp.x * r0.y - Δp.y * r0.x) * Iv;
	p->eo->v = vecadd(p->eo->v, vecmul(Δp, -me));
	p->eo->ω += (Δp.x * r1.y - Δp.y * r1.x) * Ie;
}

extern Hinge *hinges;

static void
hingeresp(Hinge *h)
{
	Obj *a, *b;
	Vec u, Δp, r0, r1;
	double ma, mb, Ia, Ib;
	double mxx, mxy, myy, det;
	
	a = h->o;
	b = h->cnext->o;
	ma = a->tab->imass;
	mb = b->tab->imass;
	Ia = ma * a->poly->invI;
	Ib = mb * b->poly->invI;
	r0 = vecsub(h->p, a->p);
	r1 = vecsub(h->cnext->p, b->p);
	u.x = a->v.x - a->ω * r0.y - b->v.x + b->ω * r1.y;
	u.y = a->v.y + a->ω * r0.x - b->v.y - b->ω * r1.x;
	u.x += Beta * (h->p.x - h->cnext->p.x) / Dt;
	u.y += Beta * (h->p.y - h->cnext->p.y) / Dt;
	mxx = ma + Ia * r0.x * r0.x + mb + Ib * r1.x * r1.x;
	mxy = Ia * r0.x * r0.y + Ib * r1.x * r1.y;
	myy = ma + Ia * r0.y * r0.y + mb + Ib * r1.y * r1.y;
	det = mxx * myy - mxy * mxy;
	Δp.x = (mxx * u.x + mxy * u.y) / det;
	Δp.y = (myy * u.y + mxy * u.x) / det;
	a->v = vecadd(a->v, vecmul(Δp, -ma));
	a->ω += (Δp.x * r0.y - Δp.y * r0.x) * Ia;
	b->v = vecadd(b->v, vecmul(Δp, mb));
	b->ω -= (Δp.x * r1.y - Δp.y * r1.x) * Ib;
	u.x = a->v.x - a->ω * r0.y - b->v.x + b->ω * r1.y;
	u.y = a->v.y + a->ω * r0.x - b->v.y - b->ω * r1.x;
}

void
physstep(void)
{
	Obj *o, *a, *b;
	int i, j, k;
	Hinge *p;
	
	for(k = 0; k < Steps; k++){
		for(o = runo.next; o != &runo; o = o->next)
			if(o->tab->imass != 0)
				o->v.y += Grav * Dt;
		icts = 0;
		for(a = runo.next; a != &runo; a = a->next)
			for(b = a->next; b != &runo; b = b->next){
				if(!rectXrect(a->bbox, b->bbox) || a->poly == nil || b->poly == nil || hinged(a, b)) continue;
				colldect(a, b);
				colldect(b, a);
			}
		for(j = 0; j < 10; j++){
			for(i = 0; i < icts; i++)
				collresp(&cts[i]);
			for(p = hinges; p != nil; p = p->anext)
				hingeresp(p);
		}
		for(o = runo.next; o != &runo; o = o->next)
			o->tab->move(o, o->p.x + o->v.x * Dt, o->p.y + o->v.y * Dt, o->θ + o->ω * Dt / DEG);
	}
}
