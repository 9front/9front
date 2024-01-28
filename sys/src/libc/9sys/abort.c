#include <u.h>
#include <libc.h>
_Noreturn void
abort(void)
{
	while(*(int*)0)
		;
}
