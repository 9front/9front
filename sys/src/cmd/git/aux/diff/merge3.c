#include <u.h>
#include <libc.h>
#include <bio.h>
#include "diff.h"

#define min(a, b) ((a)<(b)?(a):(b))
#define max(a, b) ((a)>(b)?(a):(b))

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
		addchange(d, 1, 0, 1, d->len[1]);
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

static int
same(Diff *l, Change *lc, Diff *r, Change *rc)
{
	char lbuf[MAXLINELEN], rbuf[MAXLINELEN];
	int i, ll, rl, lx, ly, rx, ry;

	lx = lc->newx;
	ly = lc->newy;
	rx = rc->newx;
	ry = rc->newy;
	if(ly - lx != ry - rx)
		return 0;
	assert(ly <= l->len[1] && ry <= r->len[1]);
	Bseek(l->input[1], l->ixnew[lx-1], 0);
	Bseek(r->input[1], r->ixnew[rx-1], 0);
	for(i = 0; i <= (ly - lx); i++){
		ll = readline(l->input[1], lbuf, sizeof(lbuf));
		rl = readline(r->input[1], rbuf, sizeof(rbuf));
		if(ll != rl)
			return 0;
		if(memcmp(lbuf, rbuf, ll) != 0)
			return 0;
	}
	return 1;
}

char*
merge(Diff *l, Diff *r)
{
	int lx, ly, rx, ry;
	int il, ir, δ;
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
		if(lc != nil && rc != nil && overlaps(lx, ly, rx, ry)){
			/*
			 * align the edges of the chunks, expanding them
			 * so that when we compare for sameness, we are
			 * comparing same-sized chunks.
			 */
			if(lc->oldx < rc->oldx){
				δ = rc->oldx - lc->oldx;
				rc->oldx = max(rc->oldx-δ, 1);
				rc->newx = max(rc->newx-δ, 1);
			}else{
				δ = lc->oldx - rc->oldx;
				lc->oldx = max(lc->oldx-δ, 1);
				lc->newx = max(lc->newx-δ, 1);
			}
			if(lc->oldy > rc->oldy){
				δ = lc->oldy - rc->oldy;
				rc->oldy = min(rc->oldy+δ, r->len[0]);
				rc->newy = min(rc->newy+δ, r->len[1]);
			}else{
				δ = rc->oldy - lc->oldy;
				lc->oldy = min(lc->oldy+δ, l->len[0]);
				lc->newy = min(lc->newy+δ, l->len[1]);
			}
			if(same(l, lc, r, rc)){
				fetch(l, l->ixold, ln, lc->oldx-1, l->input[0], "");
				fetch(l, l->ixnew, lc->newx, lc->newy, l->input[1], "");
			}else{
				fetch(l, l->ixold, ln, lc->oldx-1, l->input[0], "");
				Bprint(&stdout, "<<<<<<<<<< %s\n", l->file2);
				fetch(l, l->ixnew, lc->newx, lc->newy, l->input[1], "");
				Bprint(&stdout, "========== original\n");
				fetch(l, l->ixold, lc->oldx, lc->oldy, l->input[0], "");
				Bprint(&stdout, "========== %s\n", r->file2);
				fetch(r, r->ixnew, rc->newx, rc->newy, r->input[1], "");
				Bprint(&stdout, ">>>>>>>>>>\n");
				status = "conflict";
			}
			ln = lc->oldy+1;
			il++;
			ir++;
		}else if(lc != nil && (rc == nil || lx < rx)){
			fetch(l, l->ixold, ln, lc->oldx-1, l->input[0], "");
			fetch(l, l->ixnew, lc->newx, lc->newy, l->input[1], "");
			ln = lc->oldy+1;
			il++;
		}else if(rc != nil && (lc == nil || rx < lx)){
			fetch(l, r->ixold, ln, rc->oldx-1, r->input[0], "");
			fetch(r, r->ixnew, rc->newx, rc->newy, r->input[1], "");
			ln = rc->oldy+1;
			ir++;
		}else
			abort();
	}
	if(ln <= l->len[0])
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
