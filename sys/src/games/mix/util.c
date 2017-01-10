#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <avl.h>
#include <bio.h>
#include "mix.h"

static char buf[1024];

char*
strskip(char *s) {
	while(isspace(*s))
		s++;
	return s;
}

char*
strim(char *s)
{
	char *t;

	if(*s == '\0')
		return s;

	t = s + strlen(s) - 1;
	while(isspace(*t) && t > s)
		t--;
	t[1] = '\0';
	return s;
}

void
yyerror(char *s, ...)
{
	char *bp;
	va_list a;

	bp = seprint(buf, buf+1024, "Assembly error: %s:%d: ", filename, line);
	va_start(a, s);
	bp = vseprint(bp, buf+1024, s, a);
	va_end(a);
	*bp++ = '\n';
	write(2, buf, bp - buf);
	longjmp(errjmp, 1);
}

void
vmerror(char *s, ...)
{
	char *bp;
	va_list a;

	bp = seprint(buf, buf+1024, "VM error at %d: ", curpc);
	va_start(a, s);
	bp = vseprint(bp, buf+1024, s, a);
	va_end(a);
	*bp++ = '\n';
	write(2, buf, bp - buf);
	longjmp(errjmp, 1);
}

void
error(char *s, ...)
{
	char *bp;
	va_list a;

	va_start(a, s);
	bp = vseprint(buf, buf+1024, s, a);
	va_end(a);
	*bp++ = '\n';
	write(2, buf, bp - buf);
	exits("error");
}

void*
emalloc(ulong s)
{
	void *v;

	v = malloc(s);
	if(v == nil)
		error("Error allocating %lud: %r\n", s);
	setmalloctag(v, getcallerpc(&s));
	return v;
}

void*
emallocz(ulong s)
{
	void *v;

	v = malloc(s);
	if(v == nil)
		error("Error allocating %lud: %r", s);
	memset(v, 0, s);
	return v;
}

void*
erealloc(void *p, ulong s)
{
	void *v;

	v = realloc(p, s);
	if(v == nil)
		error("Error re-allocating %lud: %r", s);
	setrealloctag(v, getcallerpc(&s));
	return v;
}

char*
estrdup(char *s)
{
	char *n;

	n = strdup(s);
	if(n == nil)
		error("Error duplicating string %s: %r", s);
	setmalloctag(n, getcallerpc(&s));
	return n;
}

void*
bsearch(void *k, void *a, long n, int w, int (*cmp)(void*, void*))
{
	void *e;
	int c;

	while(n > 0) {
		e = (char*)a + w*(n/2);
		c = cmp(k, e);
		if(c == 0)
			return e;

		if(n == 1)
			break;

		if(c < 0)
			n /= 2;
		else {
			a = e;
			n -= n/2;
		}
	}
	return nil;
}
