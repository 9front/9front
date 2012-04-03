#include <u.h>
#include <libc.h>
#include <thread.h>
#include "threadimpl.h"

enum
{
	STACK = 8192,
};

void
iointerrupt(Ioproc *io)
{
	if(io->ctl < 0)
		return;
	qlock(io);
	if(++io->intr == 1)
		write(io->ctl, "interrupt", 9);
	qunlock(io);
}

static void
xioproc(void *a)
{
	Channel *c;
	Ioproc *io;
	Iocall *r;

	c = a;
	if(io = mallocz(sizeof(*io), 1)){
		char buf[128];

		/*
		 * open might fail, ignore it for programs like factotum
		 * that don't use iointerrupt() anyway.
		 */
		snprint(buf, sizeof(buf), "/proc/%d/ctl", getpid());
		io->ctl = open(buf, OWRITE);

		if((io->creply = chancreate(sizeof(void*), 0)) == nil){
			if(io->ctl >= 0)
				close(io->ctl);
			free(io);
			io = nil;
		} else
			io->c = c;
	}
	while(send(c, &io) < 0)
		;
	if(io == nil)
		return;

	for(;;){
		while(recv(io->c, &r) < 0)
			;
		if(r == 0)
			break;
		if(io->intr){
			r->ret = -1;
			strcpy(r->err, "interrupted");
		} else if((r->ret = r->op(&r->arg)) < 0)
			rerrstr(r->err, sizeof r->err);
		qlock(io);
		if(io->intr){
			io->intr = 0;
			if(io->ctl >= 0)
				write(io->ctl, "nointerrupt", 11);
		}
		while(send(io->creply, &r) < 0)
			;
		qunlock(io);
	}

	if(io->ctl >= 0)
		close(io->ctl);
	chanfree(io->c);
	chanfree(io->creply);
	free(io);
}

Ioproc*
ioproc(void)
{
	Channel *c;
	Ioproc *io;

	if((c = chancreate(sizeof(void*), 0)) == nil)
		sysfatal("ioproc chancreate");
	proccreate(xioproc, c, STACK);
	while(recv(c, &io) < 0)
		;
	if(io == nil)
		sysfatal("ioproc alloc");
	return io;
}

void
closeioproc(Ioproc *io)
{
	if(io == nil)
		return;
	iointerrupt(io);
	while(sendp(io->c, nil) < 0)
		;
}
