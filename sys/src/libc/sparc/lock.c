#include <u.h>
#include <libc.h>

void
lock(Lock *lk)
{
	int i;

	/* once fast */
	if(!_tas((int*)&lk->key))
		return;
	/* a thousand times pretty fast */
	for(i=0; i<1000; i++){
		if(!_tas((int*)&lk->key))
			return;
		sleep(0);
	}
	/* now nice and slow */
	for(i=0; i<1000; i++){
		if(!_tas((int*)&lk->key))
			return;
		sleep(100);
	}
	/* take your time */
	while(_tas((int*)&lk->key))
		sleep(1000);
}

int
canlock(Lock *lk)
{
	if(_tas((int*)&lk->key))
		return 0;
	return 1;
}

void
unlock(Lock *lk)
{
	lk->key = 0;
}
