#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <ttf.h>
#include <ctype.h>

void
elidenl(char **sp, int *np)
{
	char *s, *e, *t;
	int nl;
	
	s = t = *sp;
	e = *sp + *np;
	for(; s < e; s++){
		if(isspace(*s)){
			nl = 0;
			do
				nl += *s == '\n';
			while(++s < e && isspace(*s));
			if(nl <= 1)
				*t++ = ' ';
			else while(nl-- > 0)
				*t++ = '\n';
			if(s == e) break;
		}
		*t++ = *s;
	}
}

int
readtext(char *fn, char **sp, int newline)
{
	int fd, n, a, rc;
	char *s;
	enum {B = 4096};
	
	fd = open(fn, OREAD);
	if(fd < 0) sysfatal("open: %r");
	s = nil;
	n = 0;
	a = 0;
	for(;;){
		if(n + B > a)
			s = realloc(s, a += B);
		if(s == nil) sysfatal("realloc: %r");
		rc = read(fd, &s[n], B);
		if(rc < 0) sysfatal("read: %r");
		if(rc == 0) break;
		n += rc;
	}
	if(newline)
		elidenl(&s, &n);
	s = realloc(s, n);
	if(s == nil) sysfatal("realloc: %r");
	*sp = s;
	return n;
}

void
usage(void)
{
	fprint(2, "usage: %s [ -lrjn ] [ -s scale ] [ -w width ] [ -h height ] [ -p ppem ] font [ text ]\n", argv0);
	exits("usage");
}

int
number(char *s)
{
	char *p;
	int n;
	
	n = strtol(s, &p, 0);
	if(p == s || *p != 0 || n <= 0)
		usage();
	return n;	
}

TTBitmap *
scaledown(TTBitmap *b, int scale)
{
	TTBitmap *a;
	int i, j;
	
	a = ttfnewbitmap(b->width / scale * 8, b->height / scale);
	if(a == nil) sysfatal("ttfnewbitmap: %r");
	for(j = 0; j < b->height; j++)
		for(i = 0; i < b->width; i++){
			if((b->bit[j * b->stride + (i>>3)] >> 7 - (i & 7) & 1) != 0)
				a->bit[j/scale * a->stride + i/scale]++;
			if(j % scale == scale - 1 && i % scale == scale - 1)
				a->bit[j/scale * a->stride + i/scale] = ~((a->bit[j/scale * a->stride + i/scale] * 255 + scale * scale / 2) / (scale * scale));
		}
	return a;
}

void
cropwrite(TTBitmap *b)
{
	int l, r, u, d, i;
	
	l = 0;
	r = b->stride;
	u = 0;
	d = b->height;
	for(; l < r; l++)
		for(i = u; i < d; i++)
			if(b->bit[i * b->stride + l] != 0xff)
				goto right;
right:
	for(; l < r; r--)
		for(i = u; i < d; i++)
			if(b->bit[i * b->stride + r - 1] != 0xff)
				goto up;
up:
	for(; u < d; u++)
		for(i = l; i < r; i++)
			if(b->bit[u * b->stride + i] != 0xff)
				goto down;
down:
	for(; u < d; d--)
		for(i = l; i < r; i++)
			if(b->bit[(d - 1) * b->stride + i] != 0xff)
				goto out;
out:
	print("%11s %11d %11d %11d %11d ", "k8", 0, 0, r - l, d - u);
	for(i = u; i < d; i++)
		write(1, b->bit + i * b->stride + l, r - l);

}

void
main(int argc, char **argv)
{
	static int flags, scale, width, height, ppem, newline, crop;
	char *font, *txtfn, *txt;
	int txtn;
	TTFont *f;
	TTBitmap *b;
	TTBitmap *d;

	width = 640;
	height = 480;
	ppem = 18;
	scale = 1;
	ARGBEGIN {
	case 'l': break;
	case 'r': flags |= TTFRALIGN; break;
	case 'c': flags |= TTFCENTER; break;
	case 'j': flags |= TTFJUSTIFY; break;
	case 'p': ppem = number(EARGF(usage())); break;
	case 's': scale = number(EARGF(usage())); break;
	case 'w': width = number(EARGF(usage())); break;
	case 'h': height = number(EARGF(usage())); break;
	case 'n': newline++; break;
	case 'C': crop++; break;
	} ARGEND;
	if((uint)(argc - 1) > 1) usage();
	font = argv[0];
	txtfn = argc > 1 ? argv[1] : "/fd/0";
	txtn = readtext(txtfn, &txt, newline);
	f = ttfopen(font, ppem * scale, 0);
	if(f == nil) sysfatal("ttfopen: %r");
	b = ttfrender(f, txt, txt + txtn, width * scale, height * scale, flags, nil);
	if(b == nil) sysfatal("ttfrender: %r");
	d = scaledown(b, scale);
	if(crop) cropwrite(d);
	else{
		print("%11s %11d %11d %11d %11d ", "k8", 0, 0, d->width/8, d->height);
		write(1, d->bit, d->stride * d->height);
	}
}
