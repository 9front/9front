#include <u.h>
#include <libc.h>

Lock l;
QLock ql;
int counter;

void
inc(int *p)
{
	int i;

	lock(&l);
	for(i = 0; i < 10000; i++)
		*p += 1;
	unlock(&l);
}
void
dec(int *p)
{
	int i;

	lock(&l);
	for(i = 0; i < 10000; i++)
		*p -= 1;
	unlock(&l);
}

void
qinc(int *p)
{
	int i;

	qlock(&ql);
	for(i = 0; i < 10000; i++)
		*p += 1;
	qunlock(&ql);
}

void
qdec(int *p)
{
	int i;

	qlock(&ql);
	for(i = 0; i < 10000; i++)
		*p -= 1;
	qunlock(&ql);
}

int
spawn(void (*f)(int *), int *p)
{
	int pid;

	pid = rfork(RFMEM|RFPROC);
	switch(pid){
	case -1:
		sysfatal("rfork");
	case 0:
		f(p);
		exits("spawn");
	default:
		return pid;
	}
}

void
main(void)
{
	int i;

	/* smoke test: lock/unlock */
	assert(canlock(&l));
	unlock(&l);
	lock(&l);
	assert(!canlock(&l));
	unlock(&l);
	assert(canlock(&l));
	unlock(&l);

	/* smoke test: qlock/qunlock */
	assert(canqlock(&ql));
	qunlock(&ql);
	qlock(&ql);
	assert(!canqlock(&ql));
	qunlock(&ql);
	assert(canqlock(&ql));
	qunlock(&ql);

	/* lock from many procs */
	for(i = 0; i < 10; i++){
		spawn(inc, &counter);
		spawn(dec, &counter);
	}
	for(i = 0; i < 10; i++){
		free(wait());
		free(wait());
	}
	assert(counter == 0);

	/* lock from many procs */
	for(i = 0; i < 10; i++){
		spawn(qinc, &counter);
		spawn(qdec, &counter);
	}
	for(i = 0; i < 10; i++){
		free(wait());
		free(wait());
	}
	assert(counter == 0);
	exits(nil);
}
