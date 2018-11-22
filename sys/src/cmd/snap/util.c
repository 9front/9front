#include <u.h>
#include <libc.h>
#include <bio.h>
#include "snap.h"

void*
emalloc(ulong n)
{
	void *v;
	v = malloc(n);
	if(v == nil)
		sysfatal("out of memory");
	memset(v, 0, n);
	return v;
}

void*
erealloc(void *v, ulong n)
{
	v = realloc(v, n);
	if(v == nil && n != 0)
		sysfatal("out of memory");
	return v;
}

char*
estrdup(char *s)
{
	s = strdup(s);
	if(s == nil)
		sysfatal("out of memory");
	return s;
}
