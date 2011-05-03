#define _PLAN9_SOURCE
#include <u.h>
#include <lib9.h>
#include <qlock.h>

/*
 * Initialization.
 */
static void
PyThread__init_thread(void)
{
}

/*
 * Thread support.
 */
long
PyThread_start_new_thread(void (*func)(void *), void *arg)
{
	dprintf(("PyThread_start_new_thread called\n"));
	if (!initialized)
		PyThread_init_thread();
	switch(rfork(RFPROC|RFMEM)){
	case -1:
		printf("rfork: %r\n");
		return -1;
	case 0:
		_threadarg->fn = func;
		_threadarg->arg = arg;
		longjmp(_threadarg->jb, 1);
	default:
		return 0;
	}
}

long
PyThread_get_thread_ident(void)
{
	if (!initialized)
		PyThread_init_thread();
	return getpid();
}

void
PyThread_exit_thread(void)
{
	if(initialized)
		_exit(0);
	exit(0);
}

void
PyThread__exit_thread(void)
{
	_exit(0);
}

#ifndef NO_EXIT_PROG
static
void do_PyThread_exit_prog(int status, int no_cleanup)
{
	/*
	 * BUG BUG BUG 
	 */

	dprintf(("PyThread_exit_prog(%d) called\n", status));
	if (!initialized)
		if (no_cleanup)
			_exit(status);
		else
			exit(status);
}

void
PyThread_exit_prog(int status)
{
	do_PyThread_exit_prog(status, 0);
}

void
PyThread__exit_prog(int status)
{
	do_PyThread_exit_prog(status, 1);
}
#endif /* NO_EXIT_PROG */

/*
 * Lock support.
 */
PyThread_type_lock
PyThread_allocate_lock(void)
{
	QLock *lk;

	dprintf(("PyThread_allocate_lock called\n"));
	if (!initialized)
		PyThread_init_thread();

	lk = malloc(sizeof(*lk));
	memset(lk, 0, sizeof(*lk));
	dprintf(("PyThread_allocate_lock() -> %p\n", lk));
	return lk;
}

void
PyThread_free_lock(PyThread_type_lock lock)
{
	dprintf(("PyThread_free_lock(%p) called\n", lock));
	free(lock);
}

int
PyThread_acquire_lock(PyThread_type_lock lock, int waitflag)
{
	int success;

	dprintf(("PyThread_acquire_lock(%p, %d) called\n", lock, waitflag));
	if(lock == nil)
		success = 0;
	else if(waitflag){
		qlock(lock);
		success = 1;
	}else
		success = canqlock(lock);
	dprintf(("PyThread_acquire_lock(%p, %d) -> %d\n", lock, waitflag, success));
	return success;
}

void
PyThread_release_lock(PyThread_type_lock lock)
{
	dprintf(("PyThread_release_lock(%p) called\n", lock));
	qunlock(lock);
}

