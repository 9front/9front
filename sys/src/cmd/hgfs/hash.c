#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

#include <mp.h>
#include <libsec.h>

int
Hfmt(Fmt *f)
{
	uchar *p;

	p = va_arg(f->args, uchar*);
	return fmtprint(f, 
		"%.2x%.2x%.2x%.2x%.2x%.2x",
		p[0], p[1], p[2], p[3], p[4], p[5]);
}

int
fhash(int fd, uchar p1[], uchar p2[], uchar h[])
{
	DigestState *ds;
	uchar buf[BUFSZ];
	int n;

	ds = nil;
	memset(h, 0, HASHSZ);
	if(memcmp(p1, p2, HASHSZ) > 0){
		ds = sha1(p2, HASHSZ, nil, ds);
		sha1(p1, HASHSZ, nil, ds);
	} else {
		ds = sha1(p1, HASHSZ, nil, ds);
		sha1(p2, HASHSZ, nil, ds);
	}
	while((n = read(fd, buf, BUFSZ)) > 0)
		sha1(buf, n, nil, ds);
	sha1(buf, 0, h, ds);

	return 0;
}

int
hex2hash(char *s, uchar *h)
{
	uchar *b;
	int n;

	b = h;
	memset(h, 0, HASHSZ);
	n = HASHSZ*2;
	while(*s && n > 0){
		if(*s >= '0' && *s <= '9')
			*h |= *s - '0';
		else if(*s >= 'a' && *s <= 'f')
			*h |= 10 + *s - 'a';
		else if(*s >= 'A' && *s <= 'F')
			*h |= 10 + *s - 'A';
		else
			break;
		if(n-- & 1)
			h++;
		else
			*h <<= 4;
		s++;
	}
	return h - b;
}

uvlong
hash2qid(uchar *h)
{
	uvlong v;
	int i;

	v = 0;
	for(i=0; i<8; i++)
		v |= (uvlong)h[i]<<(56-8*i);
	return v;
}
