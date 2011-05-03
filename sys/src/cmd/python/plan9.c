#include "Python.h"

#define _PLAN9_SOURCE
#include <u.h>
#include <lib9.h>

#if defined(T386)
#define	FPINVAL	(1<<0)
#else
Error define FPINVAL for your arch. grep /$cputype/include/u.h
#endif

Threadarg *_threadarg;

extern DL_EXPORT(int) Py_Main(int, char **);

int
main(int argc, char **argv)
{
	Threadarg ta;

	setfcr(getfcr()&~FPINVAL);
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
