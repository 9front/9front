#include <u.h>
#include <libc.h>
#include <thread.h>
#include "threadimpl.h"

/* first argument goes in a register; simplest just to ignore it */
static void
launcherarm64(int, void (*f)(void *arg), void *arg)
{
	(*f)(arg);
	threadexits(nil);
}

void
_threadinitstack(Thread *t, void (*f)(void*), void *arg)
{
	uintptr *tos;

	tos = (uintptr*)&t->stk[t->stksize&~15];
	*--tos = (uintptr)arg;
	*--tos = (uintptr)f;
	*--tos = 0;	/* first arg to launcherarm64 */
	*--tos = 0;	/* place to store return PC */

	t->sched[JMPBUFPC] = (uintptr)launcherarm64+JMPBUFDPC;
	t->sched[JMPBUFSP] = (uintptr)tos;
}

