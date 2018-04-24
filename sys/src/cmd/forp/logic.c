#include <u.h>
#include <libc.h>
#include <mp.h>
#include <sat.h>
#include "dat.h"
#include "fns.h"

extern int satvar;

int
satand1(SATSolve *sat, int *a, int n)
{
	int i, j, r;
	int *b;

	if(n < 0)
		for(n = 0; a[n] != 0; n++)
			;
	r = 2;
	for(i = j = 0; i < n; i++){
		if(a[i] == 1 || a[i] == -2)
			return 1;
		if(a[i] == 2 || a[i] == -1)
			j++;
		else
			r = a[i];
	}
	if(j >= n - 1) return r;
	r = satvar++;
	b = malloc(sizeof(int) * (n+1));
	for(i = j = 0; i < n; i++){
		if(a[i] == 2 || a[i] == -1)
			continue;
		b[j++] = -a[i];
		sataddv(sat, -r, a[i], 0);
	}
	b[j++] = r;
	satadd1(sat, b, j);
	return r;
}

int
satandv(SATSolve *sat, ...)
{
	int r;
	va_list va;
	
	va_start(va, sat);
	satvafix(va);
	r = satand1(sat, (int*)va, -1);
	va_end(va);
	return r;
}

int
sator1(SATSolve *sat, int *a, int n)
{
	int i, j, r;
	int *b;

	if(n < 0)
		for(n = 0; a[n] != 0; n++)
			;
	r = 1;
	for(i = j = 0; i < n; i++){
		if(a[i] == 2 || a[i] == -1)
			return 2;
		if(a[i] == 1 || a[i] == -2)
			j++;
		else
			r = a[i];
	}
	if(j >= n-1) return r;
	r = satvar++;
	b = malloc(sizeof(int) * (n+1));
	for(i = j = 0; i < n; i++){
		if(a[i] == 1 || a[i] == -2)
			continue;
		b[j++] = a[i];
		sataddv(sat, r, -a[i], 0);
	}
	b[j++] = -r;
	satadd1(sat, b, j);
	return r;
}

int
satorv(SATSolve *sat, ...)
{
	va_list va;
	int r;
	
	va_start(va, sat);
	satvafix(va);
	r = sator1(sat, (int*)va, -1);
	va_end(va);
	return r;
}

typedef struct { u8int x, m; } Pi;
static Pi *π;
static int nπ;
static u64int *πm;

static void
pimp(u64int op, int n)
{
	int i, j, k;
	u8int δ;

	nπ = 0;
	for(i = 0; i < 1<<n; i++)
		if((op >> i & 1) != 0){
			π = realloc(π, sizeof(Pi) * (nπ + 1));
			π[nπ++] = (Pi){i, 0};
		}
	for(i = 0; i < nπ; i++){
		for(j = 0; j < i; j++){
			δ = π[i].x ^ π[j].x;
			if(δ == 0 || (δ & δ - 1) != 0 || π[i].m != π[j].m) continue;
			if(((π[i].m | π[j].m) & δ) != 0) continue;
			if(π[nπ-1].x == (π[i].x & π[j].x) && π[nπ-1].m == (π[i].m | δ)) continue;
			π = realloc(π, sizeof(Pi) * (nπ + 1));
			π[nπ++] = (Pi){π[i].x & π[j].x, π[i].m | δ};
		}
	}
	for(i = k = 0; i < nπ; i++){
		for(j = i+1; j < nπ; j++)
			if((π[i].m & ~π[j].m) == 0 && (π[i].x & ~π[j].m) == π[j].x)
				break;
		if(j == nπ)
			π[k++] = π[i];
	}
	nπ = k;
	assert(nπ <= 1<<n);
}

static void
pimpmask(void)
{
	int i, j;
	u64int m;

	πm = realloc(πm, sizeof(u64int) * nπ);
	for(i = 0; i < nπ; i++){
		m = 0;
		for(j = π[i].m; ; j = j - 1 & π[i].m){
			m |= 1ULL<<(π[i].x | j);
			if(j == 0) break;
		}
		πm[i] = m;
	}
}

static int
popcnt(u64int m)
{
	m = (m & 0x5555555555555555ULL) + (m >> 1 & 0x5555555555555555ULL);
	m = (m & 0x3333333333333333ULL) + (m >> 2 & 0x3333333333333333ULL);
	m = (m & 0x0F0F0F0F0F0F0F0FULL) + (m >> 4 & 0x0F0F0F0F0F0F0F0FULL);
	m = (m & 0x00FF00FF00FF00FFULL) + (m >> 8 & 0x00FF00FF00FF00FFULL);
	m = (m & 0x0000FFFF0000FFFFULL) + (m >> 16 & 0x0000FFFF0000FFFFULL);
	m = (u32int)m + (u32int)(m >> 32);
	return m;
}

static u64int
pimpcover(u64int op, int)
{
	int i, j, maxi, p, maxp;
	u64int cov, yes, m;
	
	yes = 0;
	cov = op;
	for(i = 0; i < nπ; i++){
		if((yes & 1<<i) != 0) continue;
		m = πm[i];
		for(j = 0; j < nπ; j++){
			if(j == i) continue;
			m &= ~πm[j];
			if(m == 0) break;
		}
		if(j == nπ){
			yes |= 1<<i;
			cov &= ~πm[i];
		}
	}
	while(cov != 0){
		j = popcnt(~cov & cov - 1);
		maxi = -1;
		maxp = 0;
		for(i = 0; i < nπ; i++){
			if((πm[i] & 1<<j) == 0) continue;
			if((p = popcnt(πm[i] & cov)) > maxp)
				maxi = i, maxp = p;
		}
		assert(maxi >= 0);
		yes |= 1<<maxi;
		cov &= ~πm[maxi];
	}
	return yes;
}

static void
pimpsat(SATSolve *sat, u64int yes, int *a, int n, int r)
{
	int i, j, k;
	int *cl;

	cl = emalloc(sizeof(int) * (n + 1));
	while(yes != 0){
		i = popcnt(~yes & yes - 1);
		yes &= yes - 1;
		k = 0;
		cl[k++] = r;
		for(j = 0; j < n; j++)
			if((π[i].m & 1<<j) == 0)
				cl[k++] = (π[i].x >> j & 1) != 0 ? -a[j] : a[j];
//		for(i = 0; i < k; i++) print("%d ", cl[i]); print("\n");
		satadd1(sat, cl, k);
	}
	free(cl);
}

int
satlogic1(SATSolve *sat, u64int op, int *a, int n)
{
	int i, j, o, r;
	int s;

	if(n < 0)
		for(n = 0; a[n] != 0; n++)
			;
	assert(op >> (1<<n) == 0);
	s = 0;
	j = -1;
	for(i = n; --i >= 0; ){
		if((uint)(a[i] + 2) > 4){
			if(j >= 0) break;
			j = i;
		}
		s = s << 1 | a[i] == 2 | a[i] == -1;
	}
	if(i < 0){
		if(j < 0) return 1 + (op >> s & 1);
		o = op >> s & 1 | op >> s + (1<<j) - 1 & 2;
		switch(o){
		case 0: return 1;
		case 1: return -a[j];
		case 2: return a[j];
		case 3: return 2;
		}
	}
	r = satvar++;
	pimp(op, n);
	pimpmask();
	pimpsat(sat, pimpcover(op, n), a, n, r);
	op ^= (u64int)-1 >> 64-(1<<n);
	pimp(op, n);
	pimpmask();
	pimpsat(sat, pimpcover(op, n), a, n, -r);
	return r;
}

int
satlogicv(SATSolve *sat, u64int op, ...)
{
	va_list va;
	int r;
	
	va_start(va, op);
	satvafix(va);
	r = satlogic1(sat, op, (int*)va, -1);
	va_end(va);
	return r;
}
