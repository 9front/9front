#include <u.h>
#include <libc.h>
#include <bio.h>
#include "diff.h"

static int
changecmp(void *a, void *b)
{
	return ((Change*)a)->a - ((Change*)b)->a;
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
	ch->a = a;
	ch->b = b;
	ch->c = c;
	ch->d = d;
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
overlaps(Change *l, Change *r)
{
	if(l == nil || r == nil)
		return 0;
	if(l->a <= r->a)
		return l->b >= r->a;
	else
		return r->b >= l->a;
}

char*
merge(Diff *l, Diff *r)
{
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
		if(il < l->nchanges)
			lc = &l->changes[il];
		if(ir < r->nchanges)
			rc = &r->changes[ir];
		if(overlaps(lc, rc)){
			/*
			 * align the edges of the chunks
			 */
			if(lc->a < rc->a){
				x = lc->c;
				δ = rc->a - lc->a;
				rc->a -= δ;
				rc->c -= δ;
			}else{
				x = rc->c;
				δ = lc->a - rc->a;
				lc->a -= δ;
				lc->c -= δ;
			}
			if(lc->b > rc->b){
				y = lc->d;
				δ = lc->b - rc->b;
				rc->b += δ;
				rc->d += δ;
			}else{
				y = rc->d;
				δ = rc->b - lc->b;
				lc->b += δ;
				lc->d += δ;
			}
			fetch(l, l->ixold, ln, x-1, l->input[0], "");
			Bprint(&stdout, "<<<<<<<<<< %s\n", l->file2);
			fetch(l, l->ixnew, lc->c, lc->d, l->input[1], "");
			Bprint(&stdout, "========== original\n");
			fetch(l, l->ixold, x, y, l->input[0], "");
			Bprint(&stdout, "========== %s\n", r->file2);
			fetch(r, r->ixnew, rc->c, rc->d, r->input[1], "");
			Bprint(&stdout, ">>>>>>>>>>\n");
			ln = y+1;
			il++;
			ir++;
			status = "conflict";
		}else if(rc == nil || (lc != nil && lc->a < rc->a)){
			fetch(l, l->ixold, ln, lc->a-1, l->input[0], "");
			fetch(l, l->ixnew, lc->c, lc->d, l->input[1], "");
			ln = lc->b+1;
			il++;
		}else if(lc == nil || (rc != nil && rc->a < lc->a)){
			fetch(l, l->ixold, ln, rc->a-1, l->input[0], "");
			fetch(r, r->ixnew, rc->c, rc->d, r->input[1], "");
			ln = rc->b+1;
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
