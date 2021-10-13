#include <u.h>
#include <libc.h>
#include <thread.h>
#include "threadimpl.h"

int _threaddebuglevel;

void
_threadprint(char *fmt, ...)
{
	char buf[128];
	va_list arg;
	Fmt f;
	Proc *p;

	fmtfdinit(&f, 2, buf, sizeof buf);

	p = _threadgetproc();
	if(p==nil)
		fmtprint(&f, "noproc ");
	else if(p->thread)
		fmtprint(&f, "%d.%d ", p->pid, p->thread->id);
	else
		fmtprint(&f, "%d._ ", p->pid);

	va_start(arg, fmt);
	fmtvprint(&f, fmt, arg);
	va_end(arg);
	fmtprint(&f, "\n");
	fmtfdflush(&f);
}

void
_threadassert(char *s)
{
	_threadprint("%s: assertion failed", s);
	abort();
}
