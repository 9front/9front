#include	<u.h>
#include	<libc.h>
#include	<bio.h>

void
Berror(Biobufhdr *bp, char *fmt, ...)
{
	va_list va;
	char buf[ERRMAX];

	if(bp->errorf == nil)
		return;
	
	va_start(va, fmt);
	vsnprint(buf, ERRMAX, fmt, va);
	va_end(va);
	bp->errorf(buf);
}

static void
Bpanic(char *s)
{
	sysfatal("%s", s);
}

void
Blethal(Biobufhdr *bp, void (*errorf)(char *))
{
	if(errorf == nil)
		errorf = Bpanic;

	bp->errorf = errorf;
}
