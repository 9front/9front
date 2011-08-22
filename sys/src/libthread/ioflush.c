#include <u.h>
#include <libc.h>
#include <thread.h>
#include "threadimpl.h"

long
_ioflush(va_list *)
{
	return 0;
}

int
ioflush(Ioproc *io)
{
	return iocall(io, _ioflush);
}
