#include <u.h>
#include <libc.h>
#include <bio.h>
#include "diff.h"

/*	diff - differential file comparison
*
*	Uses an algorithm due to Harold Stone, which finds
*	a pair of longest identical subsequences in the two
*	files.
*
*	The major goal is to generate the match vector J.
*	J[i] is the index of the line in file1 corresponding
*	to line i file0. J[i] = 0 if there is no
*	such line in file1.
*
*	Lines are hashed so as to work in core. All potential
*	matches are located by sorting the lines of each file
*	on the hash (called value). In particular, this
*	collects the equivalence classes in file1 together.
*	Subroutine equiv replaces the value of each line in
*	file0 by the index of the first element of its 
*	matching equivalence in (the reordered) file1.
*	To save space equiv squeezes file1 into a single
*	array member in which the equivalence classes
*	are simply concatenated, except that their first
*	members are flagged by changing sign.
*
*	Next the indices that point into member are unsorted into
*	array class according to the original order of file0.
*
*	The cleverness lies in routine stone. This marches
*	through the lines of file0, developing a vector klist
*	of "k-candidates". At step i a k-candidate is a matched
*	pair of lines x,y (x in file0 y in file1) such that
*	there is a common subsequence of lenght k
*	between the first i lines of file0 and the first y 
*	lines of file1, but there is no such subsequence for
*	any smaller y. x is the earliest possible mate to y
*	that occurs in such a subsequence.
*
*	Whenever any of the members of the equivalence class of
*	lines in file1 matable to a line in file0 has serial number 
*	less than the y of some k-candidate, that k-candidate 
*	with the smallest such y is replaced. The new 
*	k-candidate is chained (via pred) to the current
*	k-1 candidate so that the actual subsequence can
*	be recovered. When a member has serial number greater
*	that the y of all k-candidates, the klist is extended.
*	At the end, the longest subsequence is pulled out
*	and placed in the array J by unravel.
*
*	With J in hand, the matches there recorded are
*	check'ed against reality to assure that no spurious
*	matches have crept in due to hashing. If they have,
*	they are broken, and "jackpot " is recorded--a harmless
*	matter except that a true match for a spuriously
*	mated line may now be unnecessarily reported as a change.
*
*	Much of the complexity of the program comes simply
*	from trying to minimize core utilization and
*	maximize the range of doable problems by dynamically
*	allocating what is needed and reusing what is not.
*	The core requirements for problems larger than somewhat
*	are (in words) 2*length(file0) + length(file1) +
*	3*(number of k-candidates installed),  typically about
*	6n words for files of length n. 
*/

static void	
sort(Line *a, int n)	/*shellsort CACM #201*/
{
	int m;
	Line *ai, *aim, *j, *k;
	Line w;
	int i;

	m = 0;
	for (i = 1; i <= n; i *= 2)
		m = 2*i - 1;
	for (m /= 2; m != 0; m /= 2) {
		k = a+(n-m);
		for (j = a+1; j <= k; j++) {
			ai = j;
			aim = ai+m;
			do {
				if (aim->value > ai->value ||
				   aim->value == ai->value &&
				   aim->serial > ai->serial)
					break;
				w = *ai;
				*ai = *aim;
				*aim = w;

				aim = ai;
				ai -= m;
			} while (ai > a && aim >= ai);
		}
	}
}

static void
unsort(Line *f, int l, int *b)
{
	int *a;
	int i;

	a = malloc((l+1)*sizeof(int));
	for(i=1;i<=l;i++)
		a[f[i].serial] = f[i].value;
	for(i=1;i<=l;i++)
		b[i] = a[i];
	free(a);
}

static void
prune(Diff *d)
{
	int i,j;

	for(d->pref = 0; d->pref < d->len[0] && d->pref < d->len[1] &&
		d->file[0][d->pref+1].value == d->file[1][d->pref+1].value;
		d->pref++) ;
	for(d->suff=0; d->suff < d->len[0] - d->pref && d->suff < d->len[1] - d->pref &&
		d->file[0][d->len[0] - d->suff].value == d->file[1][d->len[1] - d->suff].value;
		d->suff++) ;
	for(j=0;j<2;j++) {
		d->sfile[j] = d->file[j]+d->pref;
		d->slen[j] = d->len[j]-d->pref-d->suff;
		for(i=0;i<=d->slen[j];i++)
			d->sfile[j][i].serial = i;
	}
}

static void
equiv(Line *a, int n, Line *b, int m, int *c)
{
	int i, j;

	i = j = 1;
	while(i<=n && j<=m) {
		if(a[i].value < b[j].value)
			a[i++].value = 0;
		else if(a[i].value == b[j].value)
			a[i++].value = j;
		else
			j++;
	}
	while(i <= n)
		a[i++].value = 0;
	b[m+1].value = 0;
	j = 0;
	while(++j <= m) {
		c[j] = -b[j].serial;
		while(b[j+1].value == b[j].value) {
			j++;
			c[j] = b[j].serial;
		}
	}
	c[j] = -1;
}

static int
newcand(Diff *d, int x, int  y, int pred)
{
	Cand *q;

	d->clist = erealloc(d->clist, (d->clen+1)*sizeof(Cand));
	q = d->clist + d->clen;
	q->x = x;
	q->y = y;
	q->pred = pred;
	return d->clen++;
}

static int
search(Diff *d, int *c, int k, int y)
{
	int i, j, l;
	int t;

	if(d->clist[c[k]].y < y)	/*quick look for typical case*/
		return k+1;
	i = 0;
	j = k+1;
	while((l=(i+j)/2) > i) {
		t = d->clist[c[l]].y;
		if(t > y)
			j = l;
		else if(t < y)
			i = l;
		else
			return l;
	}
	return l+1;
}

static int
stone(Diff *d, int *a, int n, int *b, int *c)
{
	int i, k,y;
	int j, l;
	int oldc, tc;
	int oldl;

	k = 0;
	c[0] = newcand(d, 0, 0, 0);
	for(i=1; i<=n; i++) {
		j = a[i];
		if(j==0)
			continue;
		y = -b[j];
		oldl = 0;
		oldc = c[0];
		do {
			if(y <= d->clist[oldc].y)
				continue;
			l = search(d, c, k, y);
			if(l!=oldl+1)
				oldc = c[l-1];
			if(l<=k) {
				if(d->clist[c[l]].y <= y)
					continue;
				tc = c[l];
				c[l] = newcand(d, i, y, oldc);
				oldc = tc;
				oldl = l;
			} else {
				c[l] = newcand(d, i,y,oldc);
				k++;
				break;
			}
		} while((y=b[++j]) > 0);
	}
	return k;
}

static void
unravel(Diff *d, int p)
{
	int i;
	Cand *q;

	for(i=0; i<=d->len[0]; i++) {
		if (i <= d->pref)
			d->J[i] = i;
		else if (i > d->len[0]-d->suff)
			d->J[i] = i+d->len[1] - d->len[0];
		else
			d->J[i] = 0;
	}
	for(q=d->clist+p; q->y != 0; q= d->clist + q->pred)
		d->J[q->x+d->pref] = q->y+d->pref;
}

#define BUF 4096
static int
cmp(Biobuf* b1, Biobuf* b2)
{
	int n;
	uchar buf1[BUF], buf2[BUF];
	int f1, f2;
	vlong nc = 1;
	uchar *b1s, *b1e, *b2s, *b2e;

	f1 = Bfildes(b1);
	f2 = Bfildes(b2);
	seek(f1, 0, 0);
	seek(f2, 0, 0);
	b1s = b1e = buf1;
	b2s = b2e = buf2;
	for(;;){
		if(b1s >= b1e){
			if(b1s >= &buf1[BUF])
				b1s = buf1;
			n = read(f1, b1s,  &buf1[BUF] - b1s);
			b1e = b1s + n;
		}
		if(b2s >= b2e){
			if(b2s >= &buf2[BUF])
				b2s = buf2;
			n = read(f2, b2s,  &buf2[BUF] - b2s);
			b2e = b2s + n;
		}
		n = b2e - b2s;
		if(n > b1e - b1s)
			n = b1e - b1s;
		if(n <= 0)
			break;
		if(memcmp((void *)b1s, (void *)b2s, n) != 0){
			return 1;
		}		
		nc += n;
		b1s += n;
		b2s += n;
	}
	if(b1e - b1s == b2e - b2s)
		return 0;
	return 1;	
}

void
calcdiff(Diff *d, char *f, char *fo, char *t, char *to)
{
	Biobuf *b0, *b1;
	int k;

	b0 = prepare(d, 0, f, fo);
	if (!b0)
		return;
	b1 = prepare(d, 1, t, to);
	if (!b1) {
		Bterm(b0);
		return;
	}
	if (d->binary){
		// could use b0 and b1 but this is simpler.
		if(cmp(b0, b1))
			d->bindiff = 1;
		Bterm(b0);
		Bterm(b1);
		return;
	}
	d->clen = 0;
	prune(d);
	sort(d->sfile[0], d->slen[0]);
	sort(d->sfile[1], d->slen[1]);

	d->member = (int *)d->file[1];
	equiv(d->sfile[0], d->slen[0], d->sfile[1], d->slen[1], d->member);
	d->member = erealloc(d->member, (d->slen[1]+2)*sizeof(int));

	d->class = (int *)d->file[0];
	unsort(d->sfile[0], d->slen[0], d->class);
	d->class = erealloc(d->class, (d->slen[0]+2)*sizeof(int));

	d->klist = emalloc((d->slen[0]+2)*sizeof(int));
	d->clist = emalloc(sizeof(Cand));
	k = stone(d, d->class, d->slen[0], d->member, d->klist);
	free(d->member);
	free(d->class);

	d->J = emalloc((d->len[0]+2)*sizeof(int));
	unravel(d, d->klist[k]);
	free(d->clist);
	free(d->klist);

	d->ixold = emalloc((d->len[0]+2)*sizeof(long));
	d->ixnew = emalloc((d->len[1]+2)*sizeof(long));
	Bseek(b0, 0, 0); Bseek(b1, 0, 0);
	check(d, b0, b1);
}

static void
output(Diff *d)
{
	int m, i0, i1, j0, j1;

	if(d->bindiff)
		print("binary files %s %s differ\n", d->file1, d->file2);
	if(d->binary)
		return;
	m = d->len[0];
	d->J[0] = 0;
	d->J[m+1] = d->len[1]+1;
	if (mode != 'e') {
		for (i0 = 1; i0 <= m; i0 = i1+1) {
			while (i0 <= m && d->J[i0] == d->J[i0-1]+1)
				i0++;
			j0 = d->J[i0-1]+1;
			i1 = i0-1;
			while (i1 < m && d->J[i1+1] == 0)
				i1++;
			j1 = d->J[i1+1]-1;
			d->J[i1] = j1;
			change(d, i0, i1, j0, j1);
		}
	} else {
		for (i0 = m; i0 >= 1; i0 = i1-1) {
			while (i0 >= 1 && d->J[i0] == d->J[i0+1]-1 && d->J[i0])
				i0--;
			j0 = d->J[i0+1]-1;
			i1 = i0+1;
			while (i1 > 1 && d->J[i1-1] == 0)
				i1--;
			j1 = d->J[i1-1]+1;
			d->J[i1] = j1;
			change(d, i1 , i0, j1, j0);
		}
	}
	if (m == 0)
		change(d, 1, 0, 1, d->len[1]);
	flushchanges(d);
}

void
diffreg(char *f, char *fo, char *t, char *to)
{
	Diff d;

	memset(&d, 0, sizeof(d));
	calcdiff(&d, f, fo, t, to);
	output(&d);
	freediff(&d);
}

void
freediff(Diff *d)
{
	if(d->input[0] != nil)
		Bterm(d->input[0]);
	if(d->input[1] != nil)
		Bterm(d->input[1]);
	free(d->J);
	free(d->ixold);
	free(d->ixnew);
}
