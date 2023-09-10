#include <u.h>
#include <libc.h>
#include <bio.h>
#include "diff.h"

static int
changecmp(void *a, void *b)
{
	return ((Change*)a)->oldx - ((Change*)b)->oldx;
}

static void
addchange(Diff *df, int a, int b, int c, int d)
{
	Change *ch;

	if (a > b && c > d)
		return;
	if(df->nchanges%1024 == 0)
		df->changes = erealloc(df->changes, (df->nchanges+1024)*sizeof(df->changes[0]));
	ch = &df->changes[df->nchanges++];
	ch->oldx = a;
	ch->oldy = b;
	ch->newx = c;
	ch->newy = d;
}

static void
collect(Diff *d)
{
	int m, i0, i1, j0, j1;

	m = d->len[0];
	d->J[0] = 0;
	d->J[m+1] = d->len[1]+1;
	for (i0 = m; i0 >= 1; i0 = i1-1) {
		while (i0 >= 1 && d->J[i0] == d->J[i0+1]-1 && d->J[i0])
			i0--;
		j0 = d->J[i0+1]-1;
		i1 = i0+1;
		while (i1 > 1 && d->J[i1-1] == 0)
			i1--;
		j1 = d->J[i1-1]+1;
		d->J[i1] = j1;
		addchange(d, i1 , i0, j1, j0);
	}
	if (m == 0)
		change(d, 1, 0, 1, d->len[1]);
	qsort(d->changes, d->nchanges, sizeof(Change), changecmp);
}

static int
overlaps(int lx, int ly, int rx, int ry)
{
	if(lx <= rx)
		return ly >= rx;
	else
		return ry >= lx;
}

char*
merge(Diff *l, Diff *r)
{
	int lx, ly, rx, ry;
	int il, ir, x, y, δ;
	Change *lc, *rc;
	char *status;
	vlong ln;

	il = 0;
	ir = 0;
	ln = 0;
	status = nil;
	collect(l);
	collect(r);
	while(il < l->nchanges || ir < r->nchanges){
		lc = nil;
		rc = nil;
		lx = -1;
		ly = -1;
		rx = -1;
		ry = -1;
		if(il < l->nchanges){
			lc = &l->changes[il];
			lx = (lc->oldx < lc->oldy) ? lc->oldx : lc->oldy;
			ly = (lc->oldx < lc->oldy) ? lc->oldy : lc->oldx;
		}
		if(ir < r->nchanges){
			rc = &r->changes[ir];
			rx = (rc->oldx < rc->oldy) ? rc->oldx : rc->oldy;
			ry = (rc->oldx < rc->oldy) ? rc->oldy : rc->oldx;
		}
		if(l != nil && r != nil && overlaps(lx, ly, rx, ry)){
			/*
			 * align the edges of the chunks
			 */
			if(lc->oldx < rc->oldx){
				x = lc->newx;
				δ = rc->oldx - lc->oldx;
				rc->oldx -= δ;
				rc->newx -= δ;
			}else{
				x = rc->newx;
				δ = lc->oldx - rc->oldx;
				lc->oldx -= δ;
				lc->newx -= δ;
			}
			if(lc->oldy > rc->oldy){
				y = lc->newy;
				δ = lc->oldy - rc->oldy;
				rc->oldy += δ;
				rc->newy += δ;
			}else{
				y = rc->newy;
				δ = rc->oldy - lc->oldy;
				lc->oldy += δ;
				lc->newy += δ;
			}
			fetch(l, l->ixold, ln, x-1, l->input[0], "");
			Bprint(&stdout, "<<<<<<<<<< %s\n", l->file2);
			fetch(l, l->ixnew, lc->newx, lc->newy, l->input[1], "");
			Bprint(&stdout, "========== original\n");
			fetch(l, l->ixold, x, y, l->input[0], "");
			Bprint(&stdout, "========== %s\n", r->file2);
			fetch(r, r->ixnew, rc->newx, rc->newy, r->input[1], "");
			Bprint(&stdout, ">>>>>>>>>>\n");
			ln = y+1;
			il++;
			ir++;
			status = "conflict";
abort();
		}else if(rc == nil || (lc != nil && lx < rx)){
			fetch(l, l->ixold, ln, lc->oldx-1, l->input[0], "");
			fetch(l, l->ixnew, lc->newx, lc->newy, l->input[1], "");
			ln = lc->oldy+1;
			il++;
		}else if(lc == nil || (rc != nil && rx < lx)){
			fetch(l, l->ixold, ln, rc->oldx-1, l->input[0], "");
			fetch(r, r->ixnew, rc->newx, rc->newy, r->input[1], "");
			ln = rc->oldy+1;
			ir++;
		}else
			abort();
	}
	if(ln < l->len[0])
		fetch(l, l->ixold, ln, l->len[0], l->input[0], "");
	return status;
}

void
usage(void)
{
	fprint(2, "usage: %s theirs base ours\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	Diff l, r;
	char *x;

	ARGBEGIN{
	default:
		usage();
	}ARGEND;

	if(argc != 3)
		usage();
	Binit(&stdout, 1, OWRITE);
	memset(&l, 0, sizeof(l));
	memset(&r, 0, sizeof(r));
	calcdiff(&l, argv[1], argv[1], argv[0], argv[0]);
	calcdiff(&r, argv[1], argv[1], argv[2], argv[2]);
	if(l.binary || r.binary)
		sysfatal("cannot merge binaries");
	x = merge(&l, &r);
	freediff(&l);
	freediff(&r);
	exits(x);
}
