#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include "diff.h"

#define MIN(x, y)	((x) < (y) ? (x): (y))

int
readline(Biobuf *bp, char *buf, int nbuf)
{
	int c;
	char *p, *e;

	p = buf;
	e = p + nbuf-1;
	do {
		c = Bgetc(bp);
		if (c < 0) {
			if (p == buf)
				return -1;
			break;
		}
		*p++ = c;
		if (c == '\n')
			break;
	} while (p < e);
	*p = 0;
	if (c != '\n' && c >= 0) {
		do c = Bgetc(bp);
		while (c >= 0 && c != '\n');
	}
	return p - buf;
}

#define HALFLONG 16
#define low(x)	(x&((1L<<HALFLONG)-1))
#define high(x)	(x>>HALFLONG)

/*
 * hashing has the effect of
 * arranging line in 7-bit bytes and then
 * summing 1-s complement in 16-bit hunks 
 */
static int
readhash(Biobuf *bp, char *buf, int nbuf)
{
	long sum;
	unsigned shift;
	char *p;
	int len, space;

	sum = 1;
	shift = 0;
	if ((len = readline(bp, buf, nbuf)) == -1)
		return 0;
	p = buf;
	switch(bflag)	/* various types of white space handling */
	{
	case 0:
		while (len--) {
			sum += (long)*p++ << (shift &= (HALFLONG-1));
			shift += 7;
		}
		break;
	case 1:
		/*
		 * coalesce multiple white-space
		 */
		for (space = 0; len--; p++) {
			if (isspace(*p)) {
				space++;
				continue;
			}
			if (space) {
				shift += 7;
				space = 0;
			}
			sum += (long)*p << (shift &= (HALFLONG-1));
			shift += 7;
		}
		break;
	default:
		/*
		 * strip all white-space
		 */
		while (len--) {
			if (isspace(*p)) {
				p++;
				continue;
			}
			sum += (long)*p++ << (shift &= (HALFLONG-1));
			shift += 7;
		}
		break;
	}
	sum = low(sum) + high(sum);
	return ((short)low(sum) + (short)high(sum));
}

Biobuf *
prepare(Diff *d, int i, char *arg, char *orig)
{
	Line *p;
	int j, h;
	Biobuf *bp;
	char *cp, buf[MAXLINELEN];
	int nbytes;
	Rune r;

	if (i == 0) {
		d->file1 = orig;
		d->firstchange = 0;
	} else
		d->file2 = orig;
	bp = Bopen(arg, OREAD);
	if (!bp)
		sysfatal("cannot open %s: %r", arg);
	if (d->binary)
		return bp;
	nbytes = Bread(bp, buf, MIN(1024, MAXLINELEN));
	if (nbytes > 0) {
		cp = buf;
		while (cp < buf+nbytes-UTFmax) {
			/*
			 * heuristic for a binary file in the
			 * brave new UNICODE world
			 */
			cp += chartorune(&r, cp);
			if (r == 0 || (r > 0x7f && r <= 0xa0)) {
				d->binary++;
				return bp;
			}
		}
		Bseek(bp, 0, 0);
	}
	p = emalloc(3*sizeof(Line));
	for (j = 0; h = readhash(bp, buf, sizeof(buf)); p[j].value = h)
		p = erealloc(p, (++j+3)*sizeof(Line));
	d->len[i] = j;
	d->file[i] = p;
	d->input[i] = bp;
	return bp;
}

static int
squishspace(char *buf)
{
	char *p, *q;
	int space;

	for (space = 0, q = p = buf; *q; q++) {
		if (isspace(*q)) {
			space++;
			continue;
		}
		if (space && bflag == 1) {
			*p++ = ' ';
			space = 0;
		}
		*p++ = *q;
	}
	*p = 0;
	return p - buf;
}

/*
 * need to fix up for unexpected EOF's
 */
void
check(Diff *d, Biobuf *bf, Biobuf *bt)
{
	int f, t, flen, tlen;
	char fbuf[MAXLINELEN], tbuf[MAXLINELEN];

	d->ixold[0] = 0;
	d->ixnew[0] = 0;
	for (f = t = 1; f < d->len[0]; f++) {
		flen = readline(bf, fbuf, sizeof(fbuf));
		d->ixold[f] = d->ixold[f-1] + flen;		/* ftell(bf) */
		if (d->J[f] == 0)
			continue;
		do {
			tlen = readline(bt, tbuf, sizeof(tbuf));
			d->ixnew[t] = d->ixnew[t-1] + tlen;	/* ftell(bt) */
		} while (t++ < d->J[f]);
		if (bflag) {
			flen = squishspace(fbuf);
			tlen = squishspace(tbuf);
		}
		if (flen != tlen || strcmp(fbuf, tbuf))
			d->J[f] = 0;
	}
	while (t < d->len[1]) {
		tlen = readline(bt, tbuf, sizeof(tbuf));
		d->ixnew[t] = d->ixnew[t-1] + tlen;	/* fseek(bt) */
		t++;
	}
}

static void
range(int a, int b, char *separator)
{
	Bprint(&stdout, "%d", a > b ? b: a);
	if (a < b)
		Bprint(&stdout, "%s%d", separator, b);
}

void
fetch(Diff *d, long *f, int a, int b, Biobuf *bp, char *s)
{
	char buf[MAXLINELEN];
	int maxb, len;

	if(a <= 1)
		a = 1;
	if(bp == d->input[0])
		maxb = d->len[0];
	else
		maxb = d->len[1];
	if(b > maxb)
		b = maxb;
	if(a > maxb)
		return;
	Bseek(bp, f[a-1], 0);
	while (a++ <= b) {
		len = readline(bp, buf, sizeof(buf));
		if(len == 0 || buf[len-1] != '\n'){
			Bprint(&stdout, "%s%s\n", s, buf);
			Bprint(&stdout, "\\ No newline at end of file\n");
		}else
			Bprint(&stdout, "%s%s", s, buf);
	}
}

void
change(Diff *df, int a, int b, int c, int d)
{
	char verb;
	char buf[4];
	Change *ch;

	if (a > b && c > d)
		return;
	anychange = 1;
	if (mflag && df->firstchange == 0) {
		if(mode) {
			buf[0] = '-';
			buf[1] = mode;
			buf[2] = ' ';
			buf[3] = '\0';
		} else {
			buf[0] = '\0';
		}
		Bprint(&stdout, "diff %s%s %s\n", buf, df->file1, df->file2);
		df->firstchange = 1;
	}
	verb = a > b ? 'a': c > d ? 'd': 'c';
	switch(mode) {
	case 'e':
		range(a, b, ",");
		Bputc(&stdout, verb);
		break;
	case 0:
		range(a, b, ",");
		Bputc(&stdout, verb);
		range(c, d, ",");
		break;
	case 'n':
		Bprint(&stdout, "%s:", df->file1);
		range(a, b, ",");
		Bprint(&stdout, " %c ", verb);
		Bprint(&stdout, "%s:", df->file2);
		range(c, d, ",");
		break;
	case 'f':
		Bputc(&stdout, verb);
		range(a, b, " ");
		break;
	case 'c':
	case 'a':
	case 'u':
		if(df->nchanges%1024 == 0)
			df->changes = erealloc(df->changes, (df->nchanges+1024)*sizeof(df->changes[0]));
		ch = &df->changes[df->nchanges++];
		ch->oldx = a;
		ch->oldy = b;
		ch->newx = c;
		ch->newy = d;
		return;
	}
	Bputc(&stdout, '\n');
	if (mode == 0 || mode == 'n') {
		fetch(df, df->ixold, a, b, df->input[0], "< ");
		if (a <= b && c <= d)
			Bprint(&stdout, "---\n");
	}
	fetch(df, df->ixnew, c, d, df->input[1], mode == 0 || mode == 'n' ? "> ": "");
	if (mode != 0 && mode != 'n' && c <= d)
		Bprint(&stdout, ".\n");
}

enum
{
	Lines = 3,	/* number of lines of context shown */
};

int
changeset(Diff *d, int i)
{
	while(i < d->nchanges && d->changes[i].oldy + 1 + 2*Lines > d->changes[i+1].oldx)
		i++;
	if(i < d->nchanges)
		return i+1;
	return d->nchanges;
}

void
flushchanges(Diff *df)
{
	vlong a, b, c, d, at, hdr;
	vlong i, j;

	if(df->nchanges == 0)
		return;

	hdr = 0;
	for(i=0; i < df->nchanges; ){
		j = changeset(df, i);
		a = df->changes[i].oldx - Lines;
		b = df->changes[j-1].oldy + Lines;
		c = df->changes[i].newx - Lines;
		d = df->changes[j-1].newy + Lines;
		if(a < 1)
			a = 1;
		if(c < 1)
			c = 1;
		if(b > df->len[0])
			b = df->len[0];
		if(d > df->len[1])
			d = df->len[1];
		if(mode == 'a'){
			a = 1;
			b = df->len[0];
			c = 1;
			d = df->len[1];
			j = df->nchanges;
		}
		if(mode == 'u'){
			if(!hdr){
				Bprint(&stdout, "--- %s\n", df->file1);
				Bprint(&stdout, "+++ %s\n", df->file2);
				hdr = 1;
			}
			Bprint(&stdout, "@@ -%lld,%lld +%lld,%lld @@\n", a, b-a+1, c, d-c+1);
		}else{
			Bprint(&stdout, "%s:", df->file1);
			range(a, b, ",");
			Bprint(&stdout, " - ");
			Bprint(&stdout, "%s:", df->file2);
			range(c, d, ",");
			Bputc(&stdout, '\n');
		}
		at = a;
		for(; i<j; i++){
			fetch(df, df->ixold, at, df->changes[i].oldx-1, df->input[0], mode == 'u' ? " " : "  ");
			fetch(df, df->ixold, df->changes[i].oldx, df->changes[i].oldy, df->input[0], mode == 'u' ? "-" : "- ");
			fetch(df, df->ixnew, df->changes[i].newx, df->changes[i].newy, df->input[1], mode == 'u' ? "+" : "+ ");
			at = df->changes[i].oldy+1;
		}
		fetch(df, df->ixold, at, b, df->input[0], mode == 'u' ? " " : "  ");
	}
	df->nchanges = 0;
}
