#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

void *
emalloc(u32int size)
{
	void *v;
	
	v = malloc(size);
	if(v == nil)
		sysfatal("%r");
	setmalloctag(v, getcallerpc(&size));
	return v;
}

void *
emallocz(u32int size)
{
	void *v;
	
	v = emalloc(size);
	memset(v, 0, size);
	setmalloctag(v, getcallerpc(&size));
	return v;
}

void *
erealloc(void *old, u32int size)
{
	void *v;
	
	v = realloc(old, size);
	if(v == nil)
		sysfatal("%r");
	setrealloctag(v, getcallerpc(&old));
	return v;
}
