#include <u.h>
#include <libc.h>
#include <mp.h>
#include <sat.h>
#include "dat.h"
#include "fns.h"

SATSolve *sat;
int satvar = 3; /* 1 = false, 2 = true */
#define SVAR(n, i) ((n)->vars[(i) < (n)->size ? (i) : (n)->size - 1])
int nassertvar;
int *assertvar;

static int
max(int a, int b)
{
	return a < b ? b : a;
}

static int
min(int a, int b)
{
	return a > b ? b : a;
}

static void
symsat(Node *n)
{
	Symbol *s;
	int i;
	
	s = n->sym;
	assert(s->type == SYMBITS);
	n->size = s->size + ((s->flags & SYMFSIGNED) == 0);
	n->vars = emalloc(sizeof(int) * n->size);
	for(i = 0; i < s->size; i++){
		if(s->vars[i] == 0)
			s->vars[i] = satvar++;
		n->vars[i] = s->vars[i];
	}
	if((s->flags & SYMFSIGNED) == 0)
		n->vars[i] = 1;
}

static void
numsat(Node *n)
{
	mpint *m;
	int i, sz, j;
	
	m = n->num;
	assert(m != nil);
	assert(m->sign > 0);
	sz = mpsignif(m) + 1;
	n->size = sz;
	n->vars = emalloc(sizeof(int) * sz);
	for(i = 0; i < m->top; i++){
		for(j = 0; j < Dbits; j++)
			if(i * Dbits + j < sz-1)
				n->vars[i * Dbits + j] = 1 + ((m->p[i] >> j & 1) != 0);
	}
	n->vars[sz - 1] = 1;
}

static void
nodevars(Node *n, int nv)
{
	int i;

	n->size = nv;
	n->vars = emalloc(sizeof(int) * nv);
	for(i = 0; i < nv; i++)
		n->vars[i] = 1;
}

static void
assign(Node *t, Node *n)
{
	Symbol *s;
	int i;
	
	s = t->sym;
	for(i = 0; i < s->size; i++){
		if(i < n->size)
			s->vars[i] = n->vars[i];
		else
			s->vars[i] = n->vars[n->size - 1];
	}
}

static void
opeq(Node *r, Node *n1, Node *n2, int neq)
{
	int i, m, a, b, *t;

	nodevars(r, 2);
	m = max(n1->size, n2->size);
	t = malloc(m * sizeof(int));
	for(i = 0; i < m; i++){
		a = SVAR(n1, i);
		b = SVAR(n2, i);
		t[i] = satlogicv(sat, neq ? 6 : 9, a, b, 0);
	}
	if(neq)
		r->vars[0] = sator1(sat, t, m);
	else
		r->vars[0] = satand1(sat, t, m);
	free(t);
}

static void
oplogic(Node *r, Node *n1, Node *n2, int op)
{
	int m, i, a, b, *t;
	
	m = max(n1->size, n2->size);
	nodevars(r, m);
	t = r->vars;
	for(i = 0; i < m; i++){
		a = SVAR(n1, i);
		b = SVAR(n2, i);
		switch(op){
		case OPOR:
			t[i] = satorv(sat, a, b, 0);
			break;
		case OPAND:
			t[i] = satandv(sat, a, b, 0);
			break;
		case OPXOR:
			t[i] = satlogicv(sat, 6, a, b, 0);
			break;
		default: abort();
		}
	}
}

static int
tologic(Node *n)
{
	int i;

	for(i = 1; i < n->size; i++)
		if(n->vars[i] != 1)
			break;
	if(i == n->size)
		return n->vars[0];
	return sator1(sat, n->vars, n->size);
}

static void
opllogic(Node *rn, Node *n1, Node *n2, int op)
{
	int a, b;
	
	a = tologic(n1);
	b = tologic(n2);
	nodevars(rn, 2);
	switch(op){
	case OPLAND:
		rn->vars[0] = satandv(sat, a, b, 0);
		break;
	case OPLOR:
		rn->vars[0] = satorv(sat, a, b, 0);
		break;
	case OPIMP:
		rn->vars[0] = satorv(sat, -a, b, 0);
		break;
	case OPEQV:
		rn->vars[0] = satlogicv(sat, 9, a, b, 0);
		break;
	default:
		abort();
	}
}

static void
opcom(Node *r, Node *n1)
{
	int i;
	
	nodevars(r, n1->size);
	for(i = 0; i < n1->size; i++)
		r->vars[i] = -n1->vars[i];
}

static void
opneg(Node *r, Node *n1)
{
	int i, c;
	
	nodevars(r, n1->size);
	c = 2;
	for(i = 0; i < n1->size; i++){
		r->vars[i] = satlogicv(sat, 9, n1->vars[i], c, 0);
		if(i < n1->size - 1)
			c = satandv(sat, -n1->vars[i], c, 0);
	}
}

static void
opnot(Node *r, Node *n1)
{
	nodevars(r, 2);
	r->vars[0] = -tologic(n1);
}

static void
opadd(Node *rn, Node *n1, Node *n2, int sub)
{
	int i, m, c, a, b;
	
	m = max(n1->size, n2->size) + 1;
	nodevars(rn, m);
	c = 1 + sub;
	sub = 1 - 2 * sub;
	for(i = 0; i < m; i++){
		a = SVAR(n1, i);
		b = SVAR(n2, i) * sub;
		rn->vars[i] = satlogicv(sat, 0x96, c, a, b, 0);
		c = satlogicv(sat, 0xe8, c, a, b, 0);
	}
}

static void
oplt(Node *rn, Node *n1, Node *n2, int le)
{
	int i, m, a, b, t, *r;
	
	nodevars(rn, 2);
	m = max(n1->size, n2->size);
	r = emalloc(sizeof(int) * (m + le));
	t = 2;
	for(i = m; --i >= 0; ){
		if(i == m - 1){
			a = SVAR(n2, i);
			b = SVAR(n1, i);
		}else{
			a = SVAR(n1, i);
			b = SVAR(n2, i);
		}
		r[i] = satandv(sat, -a, b, t, 0);
		t = satlogicv(sat, 0x90, a, b, t, 0);
	}
	if(le)
		r[m] = t;
	rn->vars[0] = sator1(sat, r, m + le);
}

static void
opidx(Node *rn, Node *n1, Node *n2, Node *n3)
{
	int i, j, k, s;
	
	k = mptoi(n2->num);
	if(n3 == nil) j = k;
	else j = mptoi(n3->num);
	if(j > k){
		nodevars(rn, 1);
		return;
	}
	s = k - j + 1;
	nodevars(rn, s + 1);
	for(i = 0; i < s; i++)
		rn->vars[i] = SVAR(n1, j + i);
}

static void
oprsh(Node *rn, Node *n1, Node *n2)
{
	int i, j, a, b, q;

	nodevars(rn, n1->size);
	memcpy(rn->vars, n1->vars, sizeof(int) * n1->size);
	for(i = 0; i < n2->size; i++){
		if(n2->vars[i] == 1) continue;
		if(n2->vars[i] == 2){
			for(j = 0; j < n1->size; j++)
				rn->vars[j] = SVAR(rn, j + (1<<i));
			continue;
		}
		for(j = 0; j < n1->size; j++){
			a = rn->vars[j];
			b = SVAR(rn, j + (1<<i));
			q = n2->vars[i];
			rn->vars[j] = satlogicv(sat, 0xca, a, b, q, 0);
		}
	}
}

static void
oplsh(Node *rn, Node *n1, Node *n2, uint sz)
{
	int i, j, a, b, q;
	u32int m;
	
	m = 0;
	for(i = n2->size; --i >= 0; )
		m = m << 1 | n2->vars[i] != m;
	m += n1->size;
	if(m > sz) m = sz;
	nodevars(rn, m);
	for(i = 0; i < m; i++)
		rn->vars[i] = SVAR(n1, i);
	for(i = 0; i < n2->size; i++){
		if(n2->vars[i] == 1) continue;
		if(n2->vars[i] == 2){
			for(j = m; --j >= 0; )
				rn->vars[j] = j >= 1<<i ? rn->vars[j - (1<<i)] : 1;
			continue;
		}
		for(j = m; --j >= 0; ){
			a = rn->vars[j];
			b = j >= 1<<i ? rn->vars[j - (1<<i)] : 1;
			q = n2->vars[i];
			rn->vars[j] = satlogicv(sat, 0xca, a, b, q, 0);
		}
	}	
}

static void
optern(Node *rn, Node *n1, Node *n2, Node *n3, uint sz)
{
	uint m;
	int i, a, b, q;
	
	m = n2->size;
	if(n3->size > m) m = n3->size;
	if(m > sz) m = sz;
	nodevars(rn, m);
	q = tologic(n1);
	for(i = 0; i < m; i++){
		a = SVAR(n3, i);
		b = SVAR(n2, i);
		rn->vars[i] = satlogicv(sat, 0xca, a, b, q, 0);
	}
}

static int *
opmul(int *n1v, int s1, int *n2v, int s2)
{
	int i, k, t, s;
	int *r, *q0, *q1, *z, nq0, nq1, nq;

	s1--;
	s2--;
	r = emalloc(sizeof(int) * (s1 + s2 + 2));
	nq = 2 * (min(s1, s2) + 2);
	q0 = emalloc(nq * sizeof(int));
	q1 = emalloc(nq * sizeof(int));
	nq0 = nq1 = 0;
	for(k = 0; k <= s1 + s2 + 1; k++){
		if(k == s1 || k == s1 + s2 + 1){ assert(nq0 < nq); q0[nq0++] = 2; }
		if(k == s2){ assert(nq0 < nq); q0[nq0++] = 2; }
		for(i = max(0, k - s2); i <= k && i <= s1; i++){
			assert(nq0 < nq);
			t = satandv(sat, n1v[i], n2v[k - i], 0);
			q0[nq0++] = i == s1 ^ k-i == s2 ? -t : t;
		}
		assert(nq0 > 0);
		while(nq0 > 1){
			if(nq0 == 2){
				t = satlogicv(sat, 0x6, q0[0], q0[1], 0);
				s = satandv(sat, q0[0], q0[1], 0);
				q0[0] = t;
				assert(nq1 < nq);
				q1[nq1++] = s;
				break;
			}
			t = satlogicv(sat, 0x96, q0[nq0-3], q0[nq0-2], q0[nq0-1], 0);
			s = satlogicv(sat, 0xe8, q0[nq0-3], q0[nq0-2], q0[nq0-1], 0);
			q0[nq0-3] = t;
			nq0 -= 2;
			assert(nq1 < nq);
			q1[nq1++] = s;
		}
		r[k] = q0[0];
		z=q0, q0=q1, q1=z;
		nq0 = nq1;
		nq1 = 0;
	}
	free(q0);
	free(q1);
	return r;
}

static void
opabs(Node *q, Node *n)
{
	int i;
	int s, c;

	nodevars(q, n->size + 1);
	s = n->vars[n->size - 1];
	c = s;
	for(i = 0; i < n->size; i++){
		q->vars[i] = satlogicv(sat, 0x96, n->vars[i], s, c, 0);
		c = satandv(sat, -n->vars[i], c, 0);
	}
}

static void
opdiv(Node *q, Node *r, Node *n1, Node *n2)
{
	Node *s;
	int i, s1, sr,zr;
	
	if(q == nil) q = node(ASTTEMP);
	if(r == nil) r = node(ASTTEMP);
	nodevars(q, n1->size);
	nodevars(r, n2->size);
	for(i = 0; i < n1->size; i++)
		q->vars[i] = satvar++;
	for(i = 0; i < n2->size; i++)
		r->vars[i] = satvar++;
	s = node(ASTBIN, OPEQ, node(ASTBIN, OPADD, node(ASTBIN, OPMUL, q, n2), r), n1); convert(s, -1); assume(s);
	s = node(ASTBIN, OPLT, node(ASTUN, OPABS, r), node(ASTUN, OPABS, n2)); convert(s, -1); assume(s);
	s1 = n1->vars[n1->size - 1];
	sr = r->vars[r->size - 1];
	zr = -sator1(sat, r->vars, r->size);
	sataddv(sat, zr, sr, -s1, 0);
	sataddv(sat, zr, -sr, s1, 0);
}

void
convert(Node *n, uint sz)
{
	if(n->size > 0) return;
	switch(n->type){
	case ASTTEMP:
		assert(n->size > 0);
		break;
	case ASTSYM:
		symsat(n);
		break;
	case ASTNUM:
		numsat(n);
		break;
	case ASTBIN:
		if(n->op == OPASS){
			if(n->n1 == nil || n->n1->type != ASTSYM)
				error(n, "convert: '%ε' invalid lval", n->n1);
			convert(n->n2, n->n1->sym->size);
			assert(n->n2->size > 0);
			assign(n->n1, n->n2);
			break;
		}
		switch(n->op){
		case OPAND: case OPOR: case OPXOR:
		case OPADD: case OPSUB: case OPLSH:
		case OPCOMMA:
			convert(n->n1, sz);
			convert(n->n2, sz);
			break;
		default:
			convert(n->n1, -1);
			convert(n->n2, -1);
		}
		assert(n->n1->size > 0);
		assert(n->n2->size > 0);
		switch(n->op){
		case OPCOMMA: n->size = n->n2->size; n->vars = n->n2->vars; break;
		case OPEQ: opeq(n, n->n1, n->n2, 0); break;
		case OPNEQ: opeq(n, n->n1, n->n2, 1); break;
		case OPLT: oplt(n, n->n1, n->n2, 0); break;
		case OPLE: oplt(n, n->n1, n->n2, 1); break;
		case OPGT: oplt(n, n->n2, n->n1, 0); break;
		case OPGE: oplt(n, n->n2, n->n1, 1); break;
		case OPXOR: case OPAND: case OPOR: oplogic(n, n->n1, n->n2, n->op); break;
		case OPLAND: case OPLOR: case OPIMP: case OPEQV: opllogic(n, n->n1, n->n2, n->op); break;
		case OPADD: opadd(n, n->n1, n->n2, 0); break;
		case OPSUB: opadd(n, n->n1, n->n2, 1); break;
		case OPLSH: oplsh(n, n->n1, n->n2, sz); break;
		case OPRSH: oprsh(n, n->n1, n->n2); break;
		case OPMUL: n->vars = opmul(n->n1->vars, n->n1->size, n->n2->vars, n->n2->size); n->size = n->n1->size + n->n2->size; break;
		case OPDIV: opdiv(n, nil, n->n1, n->n2); break;
		case OPMOD: opdiv(nil, n, n->n1, n->n2); break;
		default:
			error(n, "convert: unimplemented op %O", n->op);
		}
		break;
	case ASTUN:
		convert(n->n1, sz);
		switch(n->op){
		case OPCOM: opcom(n, n->n1); break;
		case OPNEG: opneg(n, n->n1); break;
		case OPNOT: opnot(n, n->n1); break;
		case OPABS: opabs(n, n->n1); break;
		default:
			error(n, "convert: unimplemented op %O", n->op);
		}
		break;
	case ASTIDX:
		if(n->n2->type != ASTNUM || n->n3 != nil && n->n3->type != ASTNUM)
			error(n, "non-constant in indexing expression");
		convert(n->n1, n->n3 != nil ? mptoi(n->n3->num) - mptoi(n->n2->num) + 1 : 1);
		opidx(n, n->n1, n->n2, n->n3);
		break;
	case ASTTERN:
		convert(n->n1, -1);
		convert(n->n2, sz);
		convert(n->n3, sz);
		optern(n, n->n1, n->n2, n->n3, sz);
		break;
	default:
		error(n, "convert: unimplemented %α", n->type);
	}
}

void
assume(Node *n)
{
	assert(n->size > 0);
	satadd1(sat, n->vars, n->size);
}

void
obviously(Node *n)
{
	assertvar = realloc(assertvar, sizeof(int) * (nassertvar + 1));
	assert(assertvar != nil);
	assertvar[nassertvar++] = -tologic(n);
}

void
cvtinit(void)
{
	sat = sataddv(nil, -1, 0);
	sataddv(sat, 2, 0);
}
