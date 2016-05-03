#include <stdarg.h>
#include <errno.h>
#include "fmt.h"

extern char *_plan9err;

void
werrstr(const char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	snprint(_plan9err, 128, fmt, arg);
	va_end(arg);
	errno = EPLAN9;
}
