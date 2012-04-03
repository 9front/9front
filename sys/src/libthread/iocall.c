#include <u.h>
#include <libc.h>
#include <thread.h>
#include "threadimpl.h"

long
iocall(Ioproc *io, long (*op)(va_list*), ...)
{
	Iocall r;

	r.op = op;
	va_start(r.arg, op);
	if(sendp(io->c, &r) < 0){
		werrstr("interrupted");
		return -1;
	}
	while(recv(io->creply, nil) < 0){
		if(io->ctl < 0)
			continue;
		if(canqlock(io)){
			if(++io->intr == 1)
				write(io->ctl, "interrupt", 9);
			qunlock(io);
		}
	}
	va_end(r.arg);
	if(r.ret < 0)
		errstr(r.err, sizeof r.err);
	return r.ret;
}
