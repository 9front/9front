#define _LOCK_EXTENSION
#include "../plan9/sys9.h"
#include <lock.h>

int tas(int*);	/* tas.s */

static long lockinit(long);

/*
 * barrier is called from tas.s assembly
 * to execute memory barrier.
 */
long (*_barrier)(long) = lockinit;

static int
cpus(void)
{
	char buf[256], *p;
	int f, n;

	f = _OPEN("#c/sysstat", 0);
	if(f < 0)
		return -1;
	n = _READ(f, buf, sizeof(buf)-1);
	_CLOSE(f);
	if(n <= 0)
		return -1;
	buf[n] = '\0';
	n = 0;
	p = buf;
	while(*p != '\0'){
		if(*p == '\n')
			n++;
		p++;
	}
	return n;
}

long _dmb(long);

static long
_nop(long r0)
{
	return r0;
}

static long
lockinit(long r0)
{
	if(cpus() > 1)
		_barrier = _dmb;
	else
		_barrier = _nop;
	return (*_barrier)(r0);
}

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
	lk->val = (*_barrier)(0);
}
