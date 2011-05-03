
#include "Python.h"

#ifndef PLATFORM
#define PLATFORM "plan9"
#endif

const char *
Py_GetPlatform(void)
{
	return PLATFORM;
}
