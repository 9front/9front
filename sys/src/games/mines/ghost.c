#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include "dat.h"
#include "fns.h"

enum {
	MEmpty,
	MMine,
	MUnknown,
	NState,
};

int ghostactive;
int ghostwait;
Point ghosttarget;

static uchar ***
neighbours(uchar **f, uchar ***n)
{
	int x, y, p;

	if(n != nil){
		for(x = 0; x < MaxX; x++)
			for(y = 0; y < MaxY; y++)
				memset(n[x][y], 0, NState);
	}else{
		n = calloc(sizeof(void*), MaxX);
		for(x = 0; x < MaxX; x++){
			n[x] = calloc(sizeof(void*), MaxY);
			for(y = 0; y < MaxY; y++)
				n[x][y] = calloc(sizeof(uchar), NState);
		}
	}
	
	for(y = 0; y < MaxY; y++)
		for(x = 0; x < MaxX; x++){
			p = f[x][y];
			if(x > 0 && y > 0) n[x-1][y-1][p]++;
			if(y > 0) n[x][y-1][p]++;
			if(x < MaxX-1 && y > 0) n[x+1][y-1][p]++;
			if(x > 0) n[x-1][y][p]++;
			if(x < MaxX-1) n[x+1][y][p]++;
			if(x > 0 && y < MaxY-1) n[x-1][y+1][p]++;
			if(y < MaxY-1) n[x][y+1][p]++;
			if(x < MaxX-1 && y < MaxY-1) n[x+1][y+1][p]++;
		}
	return n;
}

static void
freeneighbours(uchar ***n)
{
	int x, y;
	
	if(n == nil)
		return;
	for(x = 0; x < MaxX; x++){
		for(y = 0; y < MaxY; y++)
			free(n[x][y]);
		free(n[x]);
	}
	free(n);
}

static int
allneighbours(uchar **f, int x, int y, int (*fun)(uchar **, int, int, void *), void *aux)
{
	int rc;

	rc = 0;
	if(x > 0 && y > 0) rc += fun(f, x-1, y-1, aux);
	if(y > 0) rc += fun(f, x, y-1, aux);
	if(x < MaxX-1 && y > 0) rc += fun(f, x+1, y-1, aux);
	if(x > 0) rc += fun(f, x-1, y, aux);
	if(x < MaxX-1) rc += fun(f, x+1, y, aux);
	if(x > 0 && y < MaxY-1) rc += fun(f, x-1, y+1, aux);
	if(y < MaxY-1) rc += fun(f, x, y+1, aux);
	if(x < MaxX-1 && y < MaxY-1) rc += fun(f, x+1, y+1, aux);
	return rc;
}

typedef struct {
	int mines, pts;
	Point pt[8];
} CList;

static int
addlist(uchar **f, int x, int y, void *aux)
{
	CList *c;
	
	c = aux;
	if(f[x][y] == MUnknown)
		c->pt[c->pts++] = (Point){x,y};
	return 0;
}

static void
mklists(uchar **f, CList **clp, int *nclp)
{
	CList *cl;
	int ncl, x, y;
	uchar ***nei;
	
	cl = nil;
	ncl = 0;
	nei = neighbours(f, nil);
	for(y = 0; y < MaxY; y++)
		for(x = 0; x < MaxX; x++)
			if(MineField[x][y].Picture <= Empty8 && nei[x][y][MUnknown] > 0){
				cl = realloc(cl, (ncl + 1) * sizeof(CList));
				memset(&cl[ncl], 0, sizeof(CList));
				cl[ncl].mines = MineField[x][y].Picture - nei[x][y][MMine];
				allneighbours(f, x, y, addlist, &cl[ncl]);
				ncl++;
			}
	freeneighbours(nei);
	*clp = cl;
	*nclp = ncl;
}

static int
ismember(CList *c, Point p)
{
	int i;
	
	for(i = 0; i < c->pts; i++)
		if(c->pt[i].x == p.x && c->pt[i].y == p.y)
			return 1;
	return 0;
}

static void
merge(CList *cl, int *nclp)
{
	int i, j, k, l;

start:
	for(i = 0; i < *nclp; i++)
		for(j = 0; j < *nclp; j++){
			if(i == j) continue;
			for(k = 0; k < cl[i].pts; k++)
				if(!ismember(&cl[j], cl[i].pt[k]))
					goto next;		
			for(k = l = 0; k < cl[j].pts; k++)
				if(!ismember(&cl[i], cl[j].pt[k]))
					cl[j].pt[l++] = cl[j].pt[k];
			cl[j].pts = l;
			cl[j].mines -= cl[i].mines;
			if(l == 0){
				memcpy(&cl[j], &cl[j+1], (*nclp - j - 1) * sizeof(CList));
				(*nclp)--;
			}
			goto start;
		next: ;
		}	
}

static void
ghostfind(void)
{
	int x, y, i, j, n;
	uchar **field;
	CList *cl;
	int ncl;
	Point pd;
	int d, min;

	field = calloc(sizeof(uchar*), MaxX);
	for(x = 0; x < MaxX; x++){
		field[x] = calloc(sizeof(uchar), MaxY);
		for(y = 0; y < MaxY; y++)
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
				field[x][y] = MEmpty;
				break;
			case Mark:
				field[x][y] = MMine;
				break;
			default:
				field[x][y] = MUnknown;
			}
	}
	
	mklists(field, &cl, &ncl);
	merge(cl, &ncl);
	ghostactive = -1;
	min = 0;
	for(i = 0; i < ncl; i++)
		if(cl[i].mines == 0 || cl[i].mines == cl[i].pts)
			for(j = 0; j < cl[i].pts; j++){
				pd = subpt(addpt(addpt(mulpt(cl[i].pt[j], 16), Pt(12+8, 57+8)), Origin), LastMouse.xy);
				d = pd.x * pd.x + pd.y * pd.y;
				if(ghostactive < 0 || d < min){
					ghostactive = 1 + (cl[i].mines == cl[i].pts);
					ghosttarget = cl[i].pt[j];
					min = d;
				}
				field[cl[i].pt[j].x][cl[i].pt[j].y] = cl[i].mines == cl[i].pts ? MMine : MEmpty;
			}
	if(ghostactive < 0){
		n = 0;
		for(x = 0; x < MaxX; x++)
			for(y = 0; y < MaxY; y++)
				if(field[x][y] == MUnknown)
					n++;
		if(n == 0) goto done;
		n = lrand() % n;
		for(x = 0; x < MaxX; x++)
			for(y = 0; y < MaxY; y++)
				if(field[x][y] == MUnknown && n-- == 0){
					ghostactive = 1;
					ghosttarget = Pt(x, y);
					goto done;
				}
	done:;
	}
	for(x = 0; x < MaxX; x++)
		free(field[x]);
	free(field);
	free(cl);
}

void
GhostMode(void)
{
	Point p, q;
	double d;

	if(ghostwait > 0){
		ghostwait--;
		return;
	}
	if(Status != Game){
		ghostactive = 0;
		p = Pt(Origin.x +  MaxX * 8 + 12, Origin.y + 28);
		if(ptinrect(LastMouse.xy, insetrect(Rpt(p, p), -4))){
			InitMineField();
			eresized(0);
		}
		goto move;
	}
	if(!ghostactive)
		ghostfind();
	if(ghostactive > 0){
		p = addpt(addpt(mulpt(ghosttarget, 16), Pt(12+8, 57+8)), Origin);
		if(ptinrect(LastMouse.xy, insetrect(Rpt(p, p), -4))){
			switch(ghostactive){
			case 1: LeftClick(ghosttarget); break;
			case 2: RightClick(ghosttarget); break;
			}
			if(Status != Game) ghostwait = 100;
			DrawButton(Status);
			flushimage(display, 1);
			ghostactive = 0;
			return;
		}
	move:
		q = subpt(p, LastMouse.xy);
		d = hypot(q.x, q.y);
		d = 2 / d * (1 + d / (400 + d));
		LastMouse.xy.x += ceil(q.x * d);
		LastMouse.xy.y += ceil(q.y * d);
		emoveto(LastMouse.xy);
	}
}

void
GhostReset(void)
{
	ghostactive = 0;
	ghostwait = 0;
}
