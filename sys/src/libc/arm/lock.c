#include <u.h>
#include <libc.h>

static long lockinit(long);

/*
 * barrier is called from atom.s and tas.s assembly
 * to execute memory barrier.
 */
long (*_barrier)(long) = lockinit;

static int
cpus(void)
{
	char buf[256], *p;
	int f, n;

	f = open("#c/sysstat", OREAD);
	if(f < 0)
		return -1;
	n = read(f, buf, sizeof(buf)-1);
	close(f);
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
	if(!_tas(&lk->val))
		return;
	/* a thousand times pretty fast */
	for(i=0; i<1000; i++){
		if(!_tas(&lk->val))
			return;
		sleep(0);
	}
	/* now nice and slow */
	for(i=0; i<1000; i++){
		if(!_tas(&lk->val))
			return;
		sleep(100);
	}
	/* take your time */
	while(_tas(&lk->val))
		sleep(1000);
}

int
canlock(Lock *lk)
{
	return _tas(&lk->val) == 0;
}

void
unlock(Lock *lk)
{
	lk->val = (*_barrier)(0);
}
