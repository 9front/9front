#include <u.h>
#include <libc.h>
#include <dtracy.h>
#include <bio.h>
#include <avl.h>
#include <mp.h>
#include "dat.h"
#include "fns.h"

typedef struct ANode ANode;

struct ANode {
	Avl;
	s64int val, cnt;
	u64int sq[2];
	int keysize;
	uchar key[1];
};

Agg *aggs;
static Avltree **trees;
static ANode *key;
int interrupted;

static int
aggcmp(Avl *ap, Avl *bp)
{
	ANode *a, *b;
	
	a = (ANode *) ap;
	b = (ANode *) bp;
	return memcmp(a->key, b->key, a->keysize);
}

static void
createrecord(int type, ANode *n, s64int *q)
{
	switch(type){
	case AGGCNT: n->cnt = q[0]; break;
	case AGGSUM: case AGGMIN: case AGGMAX: n->val = q[0]; break;
	case AGGAVG: n->cnt = q[1]; n->val = q[0]; break;
	case AGGSTD: n->cnt = q[1]; n->val = q[0]; n->sq[0] = q[2]; n->sq[1] = q[3]; break;
	default: abort();
	}
}

static void
updaterecord(int type, ANode *n, s64int *q)
{
	u64int r;

	switch(type){
	case AGGCNT: n->cnt += q[0]; break;
	case AGGSUM: n->val += q[0]; break;
	case AGGAVG: n->cnt += q[1]; n->val += q[0]; break;
	case AGGSTD:
		n->cnt += q[1];
		n->val += q[0];
		r = n->sq[0] + q[2];
		if(r < q[2]) n->sq[1]++;
		n->sq[0] = r;
		n->sq[1] += q[3];
		break;
	default: abort();
	}
}


int
aggparsebuf(uchar *p, int n)
{
	uchar *e;
	Agg *a;
	u32int id;
	Avltree *tp;
	ANode *np;
	
	e = p + n;
	for(; p + 8 < e; p += a->recsize){
		id = *(u32int*)&p[4];
		if((u16int)id >= aggid){
		inval:
			fprint(2, "invalid record in aggregation buffer\n");
			return -1;
		}
		a = &aggs[(u16int)id];
		if(a->type != id>>28) goto inval;
		if(a->keysize != (id>>13&0x7ff8)) goto inval;
		if(p + a->recsize > e) goto inval;
		tp = trees[(u16int)id];
		key->keysize = a->keysize;
		memcpy(key->key, &p[8], a->keysize);
		np = (ANode *) avllookup(tp, key, 0);
		if(np == nil){
			np = emalloc(sizeof(ANode) - 1 + a->keysize);
			*np = *key;
			createrecord(a->type, np, (s64int*)&p[8+a->keysize]);
			avlinsert(tp, np);
		}else
			updaterecord(a->type, np, (s64int*)&p[8+a->keysize]);
	}
	return 0;
}

void
agginit(void)
{
	int i, m;
	
	trees = emalloc(sizeof(Avltree *) * aggid);
	m = 0;
	for(i = 0; i < aggid; i++){
		trees[i] = avlcreate(aggcmp);
		if(aggs[i].keysize > m)
			m = aggs[i].keysize;
	}
	key = emalloc(sizeof(ANode) - 1 + m);
}

int
aggnote(void *, char *note)
{
	if(strcmp(note, "interrupt") != 0 || interrupted)
		return 0;
	interrupted = 1;
	return 1;
}

void
aggkeyprint(Fmt *f, Agg *, ANode *a)
{
	fmtprint(f, "%20lld ", *(u64int*)a->key);
}

static double
variance(ANode *a)
{
	mpint *x, *y, *z;
	double r;
	
	x = vtomp(a->val, nil);
	y = uvtomp(a->sq[0], nil);
	z = vtomp(a->sq[1], nil);
	mpleft(z, 64, z);
	mpadd(z, y, y);
	vtomp(a->cnt, z);
	mpmul(x, x, x);
	mpmul(y, z, y);
	mpsub(y, x, x);
	r = mptod(x) / a->cnt;
	mpfree(x);
	mpfree(y);
	mpfree(z);
	return r;
}

void
aggvalprint(Fmt *f, int type, ANode *a)
{
	double x, s;

	switch(type){
	case AGGCNT: fmtprint(f, "%20lld", a->cnt); break;
	case AGGSUM: case AGGMIN: case AGGMAX: fmtprint(f, "%20lld", a->val); break;
	case AGGAVG: fmtprint(f, "%20g", (double)a->val / a->cnt); break;
	case AGGSTD:
		x = (double)a->val / a->cnt;
		s = variance(a);
		if(s < 0)
			fmtprint(f, "%20g %20s", x, "NaN");
		else{
			fmtprint(f, "%20g %20g", x, sqrt(s));
		}
		break;
	default:
		abort();
	}
}

void
aggdump(void)
{
	Fmt f;
	char buf[8192];
	int i;
	ANode *a;
	
	fmtfdinit(&f, 1, buf, sizeof(buf));
	for(i = 0; i < aggid; i++){
		a = (ANode *) avlmin(trees[i]);
		for(; a != nil; a = (ANode *) avlnext(a)){
			fmtprint(&f, "%s\t", aggs[i].name);
			aggkeyprint(&f, &aggs[i], a);
			aggvalprint(&f, aggs[i].type, a);
			fmtprint(&f, "\n");
		}
	}
	fmtfdflush(&f);
}
