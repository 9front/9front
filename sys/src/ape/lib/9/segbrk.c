#include <lib9.h>

extern void*	_SEGBRK(void*, void*);

void*
segbrk(void *saddr, void *addr)
{
	return _SEGBRK(saddr, addr);
}
