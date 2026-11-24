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
same(Diff *l, int lx, int ly, Diff *r, int rx, int ry)
{
	char lbuf[MAXLINELEN], rbuf[MAXLINELEN];
	int i, ll, rl;

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
	Change *lc, *le, *rc, *re, ml, mr;
	int lx, ly, rx, ry, δ;
	char *status;
	vlong ln;

	ln = 0;
	status = nil;
	collect(l);
	collect(r);
	lc = l->changes;
	le = lc + l->nchanges;
	rc = r->changes;
	re = rc + r->nchanges;
	while(lc != le || rc != re){
		/*
		 * internally we keep 1-based indexes, and represent
		 * empty ranges with x=start, y=start-1; fix that up
		 * when assigning to lx, ly.
		 */
		lx = (lc == le) ? -1 : lc->oldx;
		ly = (lc == le) ? -1 : max(lc->oldx, lc->oldy);
		rx = (rc == re) ? -1 : rc->oldx;
		ry = (rc == re) ? -1 : max(rc->oldx, rc->oldy);
		if(lc != le && rc != re && overlaps(lx, ly, rx, ry)){
			/*
			 * align the edges of the chunks, expanding them
			 * so that when we compare for sameness, we are
			 * comparing same-sized chunks.
			 */
			ml = *lc;
			mr = *rc;
Realign:
			if(ml.oldx < mr.oldx){
				δ = mr.oldx - ml.oldx;
				mr.oldx = max(mr.oldx-δ, 1);
				mr.newx = max(mr.newx-δ, 1);
			}else{
				δ = ml.oldx - mr.oldx;
				ml.oldx = max(ml.oldx-δ, 1);
				ml.newx = max(ml.newx-δ, 1);
			}
			if(ml.oldy > mr.oldy){
				δ = ml.oldy - mr.oldy;
				mr.oldy = min(mr.oldy+δ, r->len[0]);
				mr.newy = min(mr.newy+δ, r->len[1]);
			}else{
				δ = mr.oldy - ml.oldy;
				ml.oldy = min(ml.oldy+δ, l->len[0]);
				ml.newy = min(ml.newy+δ, l->len[1]);
			}

			/* 
			 * if the resulting ranges overlap subsequent
			 * chunks, expand to cover those chunks and
			 * try again.
			 */
			if(lc+1 != le && ml.oldy >= (lc+1)->oldx){
				ml.oldy = max(ml.oldy, (lc+1)->oldy);
				ml.newy = max(ml.newy, (lc+1)->newy);
				lc++;
				goto Realign;
			}
			if(rc+1 != re && mr.oldy >= (rc+1)->oldx){
				mr.oldy = max(mr.oldy, (rc+1)->oldy);
				mr.newy = max(mr.newy, (rc+1)->newy);
				rc++;
				goto Realign;
			}
			if(same(l, ml.newx, ml.newy, r, mr.newx, mr.newy)){
				fetch(l, l->ixold, ln, ml.oldx-1, l->input[0], "");
				fetch(l, l->ixnew, ml.newx, ml.newy, l->input[1], "");
			}else{
				fetch(l, l->ixold, ln, ml.oldx-1, l->input[0], "");
				Bprint(&stdout, "<<<<<<<<<< %s\n", l->file2);
				fetch(l, l->ixnew, ml.newx, ml.newy, l->input[1], "");
				Bprint(&stdout, "========== original\n");
				fetch(l, l->ixold, ml.oldx, ml.oldy, l->input[0], "");
				Bprint(&stdout, "========== %s\n", r->file2);
				fetch(r, r->ixnew, mr.newx, mr.newy, r->input[1], "");
				Bprint(&stdout, ">>>>>>>>>>\n");
				status = "conflict";
			}
			ln = ml.oldy+1;
			lc++;
			rc++;
		}else if(lc != le && (rc == re || lx < rx)){
			fetch(l, l->ixold, ln, lc->oldx-1, l->input[0], "");
			fetch(l, l->ixnew, lc->newx, lc->newy, l->input[1], "");
			ln = lc->oldy+1;
			lc++;
		}else if(rc != re && (lc == le || rx < lx)){
			fetch(l, r->ixold, ln, rc->oldx-1, r->input[0], "");
			fetch(r, r->ixnew, rc->newx, rc->newy, r->input[1], "");
			ln = rc->oldy+1;
			rc++;
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
