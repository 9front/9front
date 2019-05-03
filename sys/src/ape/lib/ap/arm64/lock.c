#define _LOCK_EXTENSION
#include "../plan9/sys9.h"
#include <lock.h>

extern int	tas(int*);
extern unsigned long long _barrier(unsigned long long);

void
lock(Lock *lk)
{
	int i;

	/* once fast */
	if(!tas(&lk->val))
		return;
	/* a thousand times pretty fast */
	for(i=0; i<1000; i++){
		if(!tas(&lk->val))
			return;
		_SLEEP(0);
	}
	/* now nice and slow */
	for(i=0; i<1000; i++){
		if(!tas(&lk->val))
			return;
		_SLEEP(100);
	}
	/* take your time */
	while(tas(&lk->val))
		_SLEEP(1000);
}

int
canlock(Lock *lk)
{
	return tas(&lk->val) == 0;
}

void
unlock(Lock *lk)
{
	lk->val = _barrier(0);
}
