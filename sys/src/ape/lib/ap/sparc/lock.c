#define _LOCK_EXTENSION
#include "../plan9/sys9.h"
#include <lock.h>

void
lock(Lock *lk)
{
	while(tas((int*)&lk->key))
		_SLEEP(0);
}

int
canlock(Lock *lk)
{
	if(tas((int*)&lk->key))
		return 0;
	return 1;
}

void
unlock(Lock *lk)
{
	lk->key = 0;
}
