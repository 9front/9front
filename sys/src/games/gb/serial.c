#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

static int infd = -1;
static int outfd = -1;
static int leader = 0;

static Channel *inc = nil;
static Channel *outc = nil;

typedef struct Msg Msg;
struct Msg {
	u8int c;
	u8int b;
	u32int t;
};

enum{
	Cex = 1,
	Csync = 2,
	Cgone = 0xFF,
};

static Msg mine = {Cgone};
static Msg theirs = {Cgone};
Event evse;

static void
writer(void *)
{
	Msg m;
	uchar buf[1+1+4];

	while(recv(outc, &m) == 1){
		buf[0] = m.c;
		buf[1] = m.b;
		buf[2] = m.t>>24;
		buf[3] = m.t>>16;
		buf[4] = m.t>>8;
		buf[5] = m.t;
		if(write(outfd, buf, sizeof buf) != sizeof buf)
			break;
	}

	chanclose(outc);
	threadexits(nil);
}

static void
reader(void *)
{
	Msg m;
	uchar buf[1+1+4];

	while(readn(infd, buf, sizeof buf) == sizeof buf){
		m.c = buf[0];
		m.b = buf[1];
		m.t = ((u32int)buf[2]<<24) | ((u32int)buf[3]<<16) | ((u32int)buf[4]<<8) | (u32int)buf[5];
		send(inc, &m);
	}

	chanclose(inc);
	threadexits(nil);
}

void
serialwrite(void)
{
	if(outc == nil)
		return;
	mine = (Msg){Cex, reg[SB], clock};
}

void
serialtick(void *)
{
	Msg m, t;

	if(leader){
		m = (Msg){mine.c == Cgone ? Csync : mine.c, mine.b, clock};
		if(send(outc, &m) != 1)
			return;
		if(recv(inc, &t) != 1)
			return;
		theirs = t;
	} else {
		if(recv(inc, &t) != 1)
			return;
		theirs = t;
		m = (Msg){theirs.c, reg[SB], clock};
		if(send(outc, &m) != 1)
			return;
	}

	addevent(&evse, FREQ / 64);
	assert(theirs.c != Cgone);
	if(theirs.c == Csync)
		return;
	if(leader)
		mine.c = Cgone;

	reg[SB] = theirs.b;
	reg[SC] &= ~0x80;
	if(leader)
		reg[SC] |= 0x1;
	else
		reg[SC] &= ~0x1;
	reg[IF] |= 0x8;
}

void
serialinit(int dolis, char *addr)
{
	int acfd, lcfd;
	char adir[40], ldir[40];

	evse.f = serialtick;
	addevent(&evse, FREQ / 64);

	if(dolis){
		acfd = announce(addr, adir);
		if(acfd < 0)
			sysfatal("failed to announce: %r");
		lcfd = listen(adir, ldir);
		if(lcfd < 0)
			sysfatal("failed to listen: %r");
		infd = outfd = accept(lcfd, ldir);
		leader = 1;
	} else {
		infd = outfd = dial(addr, nil, nil, nil);
	}

	if(infd < 0 || outfd < 0)
		sysfatal("failed to establish link: %r");

	inc = chancreate(sizeof(Msg), 0);
	outc = chancreate(sizeof(Msg), 0);
	proccreate(reader, nil, mainstacksize);
	proccreate(writer, nil, mainstacksize);
}
