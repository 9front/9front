#include "Python.h"

#define _PLAN9_SOURCE
#include <u.h>
#include <lib9.h>

Threadarg *_threadarg;

extern DL_EXPORT(int) Py_Main(int, char **);

int
main(int argc, char **argv)
{
	Threadarg ta;

#if defined(T386)
	setfcr(getfcr()&~(1<<0));
#elif defined(Tarm)
	setfsr(getfsr()&~(1<<16));
#endif

	memset(&ta, 0, sizeof ta);
	_threadarg = &ta;
	if(setjmp(ta.jb)){
		(*ta.fn)(ta.arg);
		_exit(1);
	}
	return Py_Main(argc, argv);
}


char *
Py_GetPath(void)
{
	return "/sys/lib/python";
}

char *
Py_GetPrefix(void)
{
	return "/sys/lib/python";
}

char *
Py_GetExecPrefix(void)
{
    return "/sys/lib/python";
}

char *
Py_GetProgramFullPath(void)
{
    return "/bin/python";
}
