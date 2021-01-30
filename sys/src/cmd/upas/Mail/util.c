#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <regexp.h>

#include "mail.h"

void *
emalloc(ulong n)
{
	void *v;
	
	v = mallocz(n, 1);
	if(v == nil)
		sysfatal("malloc: %r");
	setmalloctag(v, getcallerpc(&n));
	return v;
}

void *
erealloc(void *p, ulong n)
{
	void *v;
	
	v = realloc(p, n);
	if(v == nil)
		sysfatal("realloc: %r");
	setmalloctag(v, getcallerpc(&p));
	return v;
}

char*
estrdup(char *s)
{
	s = strdup(s);
	if(s == nil)
		sysfatal("strdup: %r");
	setmalloctag(s, getcallerpc(&s));
	return s;
}

char*
estrjoin(char *s, ...)
{
	va_list ap;
	char *r, *t, *p, *e;
	int n;

	va_start(ap, s);
	n = strlen(s) + 1;
	while((p = va_arg(ap, char*)) != nil)
		n += strlen(p);
	va_end(ap);

	r = emalloc(n);
	e = r + n;
	va_start(ap, s);
	t = strecpy(r, e, s);
	while((p = va_arg(ap, char*)) != nil)
		t = strecpy(t, e, p);
	va_end(ap);
	return r;
}

char*
esmprint(char *fmt, ...)
{
	char *s;
	va_list ap;

	va_start(ap, fmt);
	s = vsmprint(fmt, ap);
	va_end(ap);
	if(s == nil)
		sysfatal("smprint: %r");
	setmalloctag(s, getcallerpc(&fmt));
	return s;
}

char*
fslurp(int fd, int *nbuf)
{
	int n, sz, r;
	char *buf;

	n = 0;
	sz = 128;
	buf = emalloc(sz);
	while(1){
		r = read(fd, buf + n, sz - n);
		if(r == 0)
			break;
		if(r == -1)
			goto error;
		n += r;
		if(n == sz){
			sz += sz/2;
			buf = erealloc(buf, sz);
		}
	}
	buf[n] = 0;
	if(nbuf)
		*nbuf = n;
	return buf;
error:
	free(buf);
	return nil;
}

char *
rslurp(Mesg *m, char *f, int *nbuf)
{
	char *path;
	int fd;
	char *r;

	if(m == nil)
		path = estrjoin(mbox.path, "/", f, nil);
	else
		path = estrjoin(mbox.path, "/", m->name, "/", f, nil);
	fd = open(path, OREAD);
	free(path);
	if(fd == -1)
		return nil;
	r = fslurp(fd, nbuf);
	close(fd);
	return r;
}

u32int
strhash(char *s)
{
	u32int h, c;

	h = 5381;
	while(c = *s++ & 0xff)
		h = ((h << 5) + h) + c;
	return h;
}
