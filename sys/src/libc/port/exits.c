#include <u.h>
#include <libc.h>

void (*_onexit)(void);

_Noreturn void
exits(char *s)
{
	if(_onexit != nil) (*_onexit)();
	_exits(s);
}
