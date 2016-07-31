#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>
#include "SConn.h"
#include "secstore.h"

void *
emalloc(ulong n)
{
	void *p = malloc(n);

	if(p == nil)
		sysfatal("emalloc");
	memset(p, 0, n);
	return p;
}

void *
erealloc(void *p, ulong n)
{
	if ((p = realloc(p, n)) == nil)
		sysfatal("erealloc");
	return p;
}

char *
estrdup(char *s)
{
	if ((s = strdup(s)) == nil)
		sysfatal("estrdup");
	return s;
}

static char *
illegal(char *f)
{
	syslog(0, LOG, "illegal name: %s", f);
	return nil;
}

char *
validatefile(char *f)
{
	char *p;

	if(f == nil || *f == '\0')
		return nil;
	if(strcmp(f, "..") == 0 || strlen(f) >= 250)
		return illegal(f);
	for(p = f; *p; p++)
		if(*p < 040 || *p == '/')
			return illegal(f);
	return f;
}
