#include <u.h>
#include <libc.h>

void (*_onexit)(void);

#pragma profile off

void
exits(char *s)
{
	if(_onexit != nil) (*_onexit)();
	_exits(s);
}
