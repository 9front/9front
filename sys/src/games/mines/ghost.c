#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <mp.h>
#include <cursor.h>
#include "dat.h"
#include "fns.h"

enum { TURBO = 0 };

static Cursor bunny = {
	.set = {0x0,0x32,0x60,0x76,0x40,0x6e,0x3,0xec,0xf,0xfc,0x1f,0xf8,0x27,0x9c,0x3,0xc,0x33,0xcc,0x73,0xce,0x67,0x9e,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x6e,0x76},
	.clr = {0xf8,0xcd,0x90,0x89,0xa3,0x91,0xcc,0x12,0x90,0x2,0x20,0x6,0x58,0x62,0x7c,0xf2,0x4c,0x33,0x8c,0x31,0x98,0x61,0x80,0x1,0x80,0x1,0x80,0x1,0x91,0x89}
};

enum {
	MEmpty,
	MMine,
	MUnknown,
	NState,
};

typedef struct Ghost Ghost;
typedef struct CList CList;
typedef struct CGroup CGroup;

struct Ghost {
	QLock;
	Rendez;
	int active, wait;
	int pid;
	
	enum { NOTARG, LTARG, RTARG } targettype;
	Point target;
	int generation;
	
	int nmines;
	uchar *f;
	
	int w, h;
	
	int cursor;
	Point moves[3];
	int nmoves;
	Point cpos;
};

struct CList {
	int mines, pts;
	int pt[8];
	int next;
};

struct CGroup {
	CList *cl;
	int ncl;
	mpint **cnt;
	int max;
	int cur;
};

static Ghost ghost;

static uchar ***
neighbours(void)
{
	int x, y, p;
	uchar ***n;

	n = calloc(sizeof(void*), ghost.w);
	for(x = 0; x < ghost.w; x++){
		n[x] = calloc(sizeof(void*), ghost.h);
		for(y = 0; y < ghost.h; y++)
			n[x][y] = calloc(sizeof(uchar), NState);
	}
	
	for(y = 0; y < ghost.h; y++)
		for(x = 0; x < ghost.w; x++){
			p = ghost.f[x + y * ghost.w];
			if(x > 0 && y > 0) n[x-1][y-1][p]++;
			if(y > 0) n[x][y-1][p]++;
			if(x < ghost.w-1 && y > 0) n[x+1][y-1][p]++;
			if(x > 0) n[x-1][y][p]++;
			if(x < ghost.w-1) n[x+1][y][p]++;
			if(x > 0 && y < ghost.h-1) n[x-1][y+1][p]++;
			if(y < ghost.h-1) n[x][y+1][p]++;
			if(x < ghost.w-1 && y < ghost.h-1) n[x+1][y+1][p]++;
		}
	return n;
}

static void
freeneighbours(uchar ***n)
{
	int x, y;

	for(x = 0; x < ghost.w; x++){
		for(y = 0; y < ghost.h; y++)
			free(n[x][y]);
		free(n[x]);
	}
	free(n);
}

static void
printcl(CList *cl, int ncl)
{
	int i, j;
	
	for(i = 0; i < ncl; i++){
		print("%d: ", cl[i].mines);
		for(j = 0; j < cl[i].pts; j++)
			print("%d ", cl[i].pt[j]);
		print("\n");
	}
	print("--\n");
}

static void
mklists(CList **clp, int *nclp)
{
	CList *c, *cl;
	int ncl, x, y;
	uchar ***nei;
	
	cl = nil;
	ncl = 0;
	nei = neighbours();
	for(y = 0; y < ghost.h; y++)
		for(x = 0; x < ghost.w; x++)
			if(MineField[x][y].Picture <= Empty8 && nei[x][y][MUnknown] > 0){
				cl = realloc(cl, (ncl + 1) * sizeof(CList));
				c = &cl[ncl++];
				memset(c, 0, sizeof(CList));
				c->mines = MineField[x][y].Picture - nei[x][y][MMine];
				if(x > 0 && y > 0 && ghost.f[(x-1)+(y-1)*ghost.w] == MUnknown)
					c->pt[c->pts++] = (x-1)+(y-1)*ghost.w;
				if(y > 0 && ghost.f[(x)+(y-1)*ghost.w] == MUnknown)
					c->pt[c->pts++] = (x)+(y-1)*ghost.w;
				if(x < ghost.w-1 && y > 0 && ghost.f[(x+1)+(y-1)*ghost.w] == MUnknown)
					c->pt[c->pts++] = (x+1)+(y-1)*ghost.w;
				if(x > 0 && ghost.f[(x-1)+(y)*ghost.w] == MUnknown)
					c->pt[c->pts++] = (x-1)+(y)*ghost.w;
				if(x < ghost.w-1 && ghost.f[(x+1)+(y)*ghost.w] == MUnknown)
					c->pt[c->pts++] = (x+1)+(y)*ghost.w;
				if(x > 0 && y < ghost.h-1 && ghost.f[(x-1)+(y+1)*ghost.w] == MUnknown)
					c->pt[c->pts++] = (x-1)+(y+1)*ghost.w;
				if(y < ghost.h-1 && ghost.f[(x)+(y+1)*ghost.w] == MUnknown)
					c->pt[c->pts++] = (x)+(y+1)*ghost.w;
				if(x < ghost.w-1 && y < ghost.h-1 && ghost.f[(x+1)+(y+1)*ghost.w] == MUnknown)
					c->pt[c->pts++] = (x+1)+(y+1)*ghost.w;
			}
	freeneighbours(nei);
	*clp = cl;
	*nclp = ncl;
}

static int
ismember(CList *c, int p)
{
	int i;
	
	for(i = 0; i < c->pts; i++)
		if(c->pt[i] == p)
			return 1;
	return 0;
}

static void
splitknown(CList **clp, int *nclp, int i, int *lastq)
{
	int j;
	CList *cl;
	int ncl;
	
	cl = *clp;
	ncl = *nclp;
	cl = realloc(cl, sizeof(CList) * (ncl + cl[i].pts - 1));
	memset(cl + ncl, 0, sizeof(CList) * (cl[i].pts - 1));
	for(j = 1; j < cl[i].pts; j++){
		cl[ncl - 1 + j].mines = cl[i].pts == cl[i].mines;
		cl[ncl - 1 + j].pts = 1;
		cl[ncl - 1 + j].pt[0] = cl[i].pt[j];
		cl[*lastq].next = ncl - 1 + j;
		*lastq = ncl - 1 + j;
	}
	cl[i].mines = cl[i].pts == cl[i].mines;
	*nclp += cl[i].pts - 1;
	cl[i].pts = 1;
	*clp = cl;
}

static int
merge(CList **clp, int *nclp, int start, int split)
{
	int i, j, k, l, p;
	CList *cl, *q, *r;
	int qi, lastq;
	int zero;

	cl = *clp;
	qi = -1;
	lastq = -1;
	zero = 0;
	for(i = 0; i < *nclp; i++)
		if(cl[i].pts == 0 || start >= 0 && start != i)
			cl[i].next = -2;
		else{
			cl[i].next = -1;
			if(lastq >= 0)
				cl[lastq].next = i;
			else
				qi = i;
			lastq = i;
		}
	while(qi >= 0){
		q = &cl[qi];
		for(i = 0; i < *nclp; i++){
			r = &cl[i];
			if(r == q || r->pts < q->pts) continue;
			for(j = k = 0; j < q->pts; j++, k++){
				p = q->pt[j];
				if(r->pt[r->pts - 1] < p)
					goto next;
				while(r->pt[k] < p)
					k++;
				if(r->pt[k] > p)
					goto next;
			}
			for(j = k = l = 0; j < q->pts; j++, k++){
				p = q->pt[j];
				while(r->pt[k] < p)
					r->pt[l++] = r->pt[k++];
			}
			while(k < r->pts)
				r->pt[l++] = r->pt[k++];
			r->pts = l;
			r->mines -= q->mines;
			if(r->mines < 0 || r->mines > r->pts){
				*clp = cl;
				return -1;
			}
			if(r->pts == 0 && r->mines == 0)
				zero++;
			if(r->next == -2 && r->pts > 0){
				r->next = -1;
				cl[lastq].next = i;
				lastq = i;
			}
			if(split && r->pts > 1 && (r->pts == r->mines || r->mines == 0)){
				splitknown(&cl, nclp, i, &lastq);
				q = &cl[qi];
			}
		next: ;
		}
		qi = q->next;
		q->next = -2;
	}
	if(zero != 0){
		for(i = 0, j = 0; i < *nclp; i++)
			if(cl[i].mines != 0 || cl[i].pts != 0)
				cl[j++] = cl[i];
		*nclp = j;
	}
	*clp = cl;
	return 0;
}

static void
countref(CList *cl, int ncl, uchar *ref)
{
	int i, j;
	
	memset(ref, 0, ghost.w * ghost.h);
	for(i = 0; i < ncl; i++)
		for(j = 0; j < cl[i].pts; j++)
			ref[cl[i].pt[j]]++;
}

static int *
clgroup(CList *cl, int ncl, uchar *ref)
{
	int i, j, k, og, ng;
	int *g;
	
	g = calloc(ncl, sizeof(int));
	for(i = 0; i < ncl; i++)
		g[i] = i;
start:
	for(i = 0; i < ncl; i++)
		for(j = 0; j < cl[i].pts; j++){
			if(ref[cl[i].pt[j]] == 1) continue;
			for(k = 0; k < ncl; k++)
				if(g[i] != g[k] && ismember(&cl[k], cl[i].pt[j]))
					break;
			if(k == ncl) continue;
			if(g[i] < g[k]){
				og = g[i];
				ng = g[k];
			}else{
				og = g[k];
				ng = g[i];
			}
			for(k = 0; k < ncl; k++)
				if(g[k] == og)
					g[k] = ng;
			goto start;
		}
	return g;
}

static int
groupmin(int *g, int ncl)
{
	int i, j, og, ng;
	u32int *bs;

	bs = calloc(sizeof(u32int), ncl + 31 >> 5);
	for(i = 0; i < ncl; i++)
		bs[g[i]>>5] |= 1<<(g[i] & 31);
	for(i = 0; i < ncl; i++){
		for(j = 0; j < g[i]; j++)
			if((bs[j>>5] & 1<<(j & 31)) == 0)
				break;
		if(j == g[i]) continue;
		og = g[i];
		ng = j;
		for(j = 0; j < ncl; j++)
			if(g[j] == og)
				g[j] = ng;
		bs[og>>5] &= ~(1<<(og & 31));
		bs[ng>>5] |= 1<<(ng & 31);
	}
	og = 0;
	for(i = 0; i < ncl; i++)
		if(g[i] + 1 > og)
			og = g[i] + 1;
	free(bs);
	return og;
}

static int
mkgroups(CList *cl, int ncl, uchar *ref, CGroup **gp)
{
	CGroup *g, *p;
	int i, j, k, ng;
	int *gr;
	
	gr = clgroup(cl, ncl, ref);
	ng = groupmin(gr, ncl);
	g = calloc(sizeof(CGroup), ng);
	for(i = 0; i < ng; i++){
		p = &g[i];
		for(j = 0; j < ncl; j++)
			if(gr[j] == i)
				p->ncl++;
		p->cl = calloc(sizeof(CList), p->ncl);
		for(j = 0, k = 0; j < ncl; j++)
			if(gr[j] == i)
				p->cl[k++] = cl[j];
	}
	free(gr);
	*gp = g;
	return ng;
}

static void
freegroups(CGroup *g, int ng)
{
	int i, j;
	
	for(i = 0; i < ng; i++){
		for(j = 0; j <= g[i].max; j++)
			mpfree(g[i].cnt[j]);
		free(g[i].cl);
		free(g[i].cnt);
	}
	free(g);
}

static void
binom(mpint *acc, mpint *temp, int n, int k)
{
	int i;
	
	for(i = 0; i < k; i++){
		itomp(n - i, temp);
		mpmul(acc, temp, acc);
	}
	for(i = 2; i <= k; i++){
		itomp(i, temp);
		mpdiv(acc, temp, acc, nil);
	}
}

static void countcomb(CList *cl, int ncl, mpint **);

static void
cltry(CList *cl, int ncl, int p, int v, mpint **cnt)
{
	CList *cm;
	int ncm;
	
	cm = calloc(ncl + 1, sizeof(CList));
	memcpy(cm, cl, ncl * sizeof(CList));
	memset(&cm[ncl], 0, sizeof(CList));
	cm[ncl].mines = v;
	cm[ncl].pts = 1;
	cm[ncl].pt[0] = p;
	ncm = ncl + 1;
	if(merge(&cm, &ncm, ncl, 1) < 0){
		free(cm);
		return;
	}
	countcomb(cm, ncm, cnt);
	free(cm);
}

static void
countcomb(CList *cl, int ncl, mpint **cnt)
{
	uchar *ref;
	int p, i, j, nm;
	mpint *rc, *c0;
	
	ref = calloc(ghost.w, ghost.h);
	countref(cl, ncl, ref);
	for(i = 0; i < ncl; i++)
		for(j = 0; j < cl[i].pts; j++){
			p = cl[i].pt[j];
			if(ref[p] > 1){
				cltry(cl, ncl, p, 0, cnt);
				cltry(cl, ncl, p, 1, cnt);
				free(ref);
				return;
			}
		}
	rc = itomp(1, nil);
	c0 = mpnew(0);
	nm = 0;
	for(i = 0; i < ncl; i++){
		nm += cl[i].mines;
		binom(rc, c0, cl[i].pts, cl[i].mines);
	}
	if(mpcmp(rc, mpzero) != 0)
		if(cnt[nm] == nil)
			cnt[nm] = rc;
		else{
			mpadd(cnt[nm], rc, cnt[nm]);
			mpfree(rc);
		}
	else
		mpfree(rc);
	mpfree(c0);
	free(ref);
}

static mpint *
totcomb(int tot, int nf, CGroup *g, int ng)
{
	int i, j, s, carry;
	mpint *r, *v, *t, *m;
	
	s = 0;
	for(i = 0; i < ng; i++){
		for(j = 0; j <= g[i].max; j++)
			if(g[i].cnt[j] != nil)
				break;
		if(j > g[i].max)
			return mpnew(0);
		g[i].cur = j;
		s += j;
	}
	if(s > tot)
		return mpnew(0);
	r = mpnew(0);
	v = mpnew(0);
	t = mpnew(0);
	do{
		itomp(1, v);
		s = 0;
		for(i = 0; i < ng; i++){
			m = g[i].cnt[g[i].cur];
			mpmul(v, m, v);
			s += g[i].cur;
		}
		if(s <= tot){
			binom(v, t, nf, tot - s);
			mpadd(r, v, r);
		}
		for(i = 0; i < ng; i++){
			carry = 0;
			do
				if(++g[i].cur > g[i].max){
					g[i].cur = 0;
					carry = 1;
				}
			while(g[i].cnt[g[i].cur] == nil);
			if(!carry) break;
		}
	}while(i < ng);
	mpfree(v);
	mpfree(t);
	return r;
}

static int
freefields(uchar *ref)
{
	int nf, p;

	nf = 0;
	for(p = 0; p < ghost.w * ghost.h; p++)
		if(ref[p] == 0 && ghost.f[p] == MUnknown)
			nf++;
	return nf;
}

static int
basecounts(CGroup *g, int ng)
{
	int i, j;
	int allmax;
	CGroup *p;

	allmax = 0;
	for(i = 0; i < ng; i++){
		p = &g[i];
		p->max = 0;
		for(j = 0; j < p->ncl; j++)
			p->max += p->cl[j].mines;
		p->cnt = calloc(p->max + 1, sizeof(mpint *));
		countcomb(p->cl, p->ncl, p->cnt);
		if(p->max > allmax) allmax = p->max;
	}
	return allmax;
}

static void
fieldcombs(CList *cl, int ncl, int *xp, int *yp)
{
	uchar *ref;
	int i, k, p;
	int nf, ng;
	int allmax;
	int ref1;
	mpint *min, *v, **alt, **backup;
	CList *l;
	CGroup *g, *gp;

	ref = calloc(ghost.w, ghost.h);
	countref(cl, ncl, ref);
	nf = freefields(ref);
	ng = mkgroups(cl, ncl, ref, &g);
	allmax = basecounts(g, ng);
	alt = calloc(allmax + 1, sizeof(mpint *));
	
	*xp = -1;
	*yp = -1;
	min = mpnew(0);
	
	for(gp = g; gp < g + ng; gp++)
		for(l = gp->cl; l < gp->cl + gp->ncl; l++){
			ref1 = 0;
			for(i = 0; i < l->pts; i++){
				p = l->pt[i];
				if(ref[p] == 0xff)
					continue;
				if(ref[p] == 1 && ref1++ != 0)
					continue;
				ref[p] = 0xff;
				
				cltry(gp->cl, gp->ncl, p, 1, alt);
				backup = gp->cnt;
				gp->cnt = alt;
				v = totcomb(ghost.nmines, nf, g, ng);
				if(*xp < 0 || mpcmp(v, min) < 0){
					mpassign(v, min);
					*xp = p % ghost.w;
					*yp = p / ghost.w;
				}
				
				gp->cnt = backup;
				for(k = 0; k <= gp->max; k++)
					mpfree(alt[k]);
				memset(alt, 0, (gp->max + 1) * sizeof(mpint *));
				mpfree(v);
			}
		}
	if(nf > 0){
		v = totcomb(ghost.nmines - 1, nf - 1, g, ng);
		if(*xp < 0 || mpcmp(v, min) < 0){
			i = nrand(nf);
			for(p = 0; p < ghost.w * ghost.h; p++)
				if(ref[p] == 0 && ghost.f[p] == MUnknown && i-- == 0)
					break;
			mpassign(v, min);
			*xp = p % ghost.w;
			*yp = p / ghost.w;
		}
		mpfree(v);
	}
	mpfree(min);
	free(alt);
	free(ref);
	freegroups(g, ng);
}

static void
ghostprep(CList **clp, int *nclp)
{
	int x, y;

	ghost.w = MaxX;
	ghost.h = MaxY;
	ghost.nmines = MinesRemain;
	ghost.f = calloc(ghost.w, ghost.h);
	for(x = 0; x < ghost.w; x++){
		for(y = 0; y < ghost.h; y++)
			switch(MineField[x][y].Picture){
			case Empty0:
			case Empty1:
			case Empty2:
			case Empty3:
			case Empty4:
			case Empty5:
			case Empty6:
			case Empty7:
			case Empty8:
				ghost.f[x + y * ghost.w] = MEmpty;
				break;
			case Mark:
				ghost.f[x + y * ghost.w] = MMine;
				break;
			default:
				ghost.f[x + y * ghost.w] = MUnknown;
			}
	}
	mklists(clp, nclp);
}

static int
ghostfind(CList *cl, int ncl, Point *targp)
{
	int i, j, x, y;
	Point p, pd;
	int d, min;
	Point targ;
	int rt;
	
	merge(&cl, &ncl, -1, 0);
	min = 0;
	rt = NOTARG;
	targ = ghost.cpos;
	for(i = 0; i < ncl; i++)
		if(cl[i].mines == 0 || cl[i].mines == cl[i].pts)
			for(j = 0; j < cl[i].pts; j++){
				p = Pt(cl[i].pt[j]%ghost.w, cl[i].pt[j]/ghost.w);
				pd = subpt(addpt(addpt(mulpt(p, 16), Pt(12+8, 57+8)), Origin), targ);
				d = pd.x * pd.x + pd.y * pd.y;
				if(rt == NOTARG || d < min){
					rt = cl[i].mines == cl[i].pts ? RTARG : LTARG;
					*targp = p;
					min = d;
				}
				ghost.f[cl[i].pt[j]] = cl[i].mines == cl[i].pts ? MMine : MEmpty;
			}
	if(rt == NOTARG){
		fieldcombs(cl, ncl, &x, &y);
		if(x >= 0){
			rt = LTARG;
			*targp = Pt(x, y);
		}
	}
	free(ghost.f);
	free(cl);
	return rt;
}

static void
killchild(void)
{
	postnote(PNPROC, ghost.pid, "kill");
}

void
GhostInit(void)
{
	CList *cl;
	int ncl;
	int rt;
	Point p;
	int rc;
	int g;

	ghost.Rendez.l = &ghost;
	if(rc = rfork(RFPROC|RFMEM), rc == 0){
		qlock(&ghost);
		for(;;){
			while(!ghost.active || ghost.targettype != NOTARG)
				rsleep(&ghost);
			g = ghost.generation;
			ghostprep(&cl, &ncl);
			qunlock(&ghost);
			rt = ghostfind(cl, ncl, &p);
			qlock(&ghost);
			if(ghost.generation == g){
				ghost.targettype = rt;
				ghost.target = p;
			}
		}
	}else{
		ghost.pid = rc;
		atexit(killchild);
	}
}

int
GhostCheck(Point p)
{
	int i;
	
	for(i = 0; i < ghost.nmoves; i++)
		if(eqpt(ghost.moves[i], p))
			return 1;
	return 0;
}

static void
move(Point p)
{
	if(ghost.nmoves == nelem(ghost.moves))
		memmove(ghost.moves + 1, ghost.moves, sizeof(ghost.moves) - sizeof(Point));
	else
		memmove(ghost.moves + 1, ghost.moves, ghost.nmoves++ * sizeof(Point));
	ghost.moves[0] = p;
	ghost.cpos = p;
	emoveto(p);
}

static void
approach(Point p)
{
	Point q;
	double d;

	q = subpt(p, ghost.cpos);
	d = hypot(q.x, q.y);
	if(d == 0)
		return;
	d = 2 / d * 2 * (1 + d / (400 + d));
	move(addpt(ghost.cpos, Pt(ceil(q.x * d), ceil(q.y * d))));
}

void
GhostMode(void)
{
	Point p;

	if(!ghost.cursor){
		esetcursor(&bunny);
		ghost.cursor = 1;
	}
	if(ghost.wait > 0){
		if(Status == Oops)
			move(addpt(ghost.cpos, Pt(nrand(20) - 10, nrand(20) - 10)));
		ghost.wait--;
		return;
	}
	if(Status != Game){
		ghost.active = 0;
		p = Pt(Origin.x + MaxX * 8 + 12, Origin.y + 28);
		if(TURBO || ptinrect(ghost.cpos, insetrect(Rpt(p, p), -4))){
			InitMineField();
			qlock(&ghost);
			ghost.active = 0;
			ghost.targettype = NOTARG;
			ghost.generation++;
			qunlock(&ghost);
			eresized(0);
		}else
			approach(p);
		return;
	}
	qlock(&ghost);
	if(!ghost.active){
		ghost.active = 1;
		rwakeup(&ghost);
	}
	if(ghost.targettype != NOTARG){
		p = addpt(addpt(mulpt(ghost.target, 16), Pt(12+8, 57+8)), Origin);
		if(TURBO || ptinrect(ghost.cpos, insetrect(Rpt(p, p), -4))){
			switch(ghost.targettype){
			case LTARG: LeftClick(ghost.target); break;
			case RTARG: RightClick(ghost.target); break;
			}
			ghost.targettype = NOTARG;
			rwakeup(&ghost);
			qunlock(&ghost);
			if(Status != Game && !TURBO) ghost.wait = 100;
			DrawButton(Status);
			flushimage(display, 1);
			return;
		}else{
			qunlock(&ghost);
			approach(p);
		}
	}else
		qunlock(&ghost);
}

void
GhostReset(int deltarg)
{
	if(ghost.cursor){
		esetcursor(nil);
		ghost.cursor = 0;
	}
	ghost.cpos = LastMouse.xy;
	qlock(&ghost);
	ghost.active = 0;
	ghost.wait = 0;
	ghost.targettype = NOTARG;
	if(deltarg)
		ghost.generation++;
	qunlock(&ghost);
}
