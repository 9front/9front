#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

static int fd;

static void
put8(u8int i)
{
	write(fd, &i, 1);
}

static void
put16(u16int i)
{
	put8(i);
	put8(i >> 8);
}

static void
put32(u32int i)
{
	put8(i);
	put8(i >> 8);
	put8(i >> 16);
	put8(i >> 24);
}

static int
get8(void)
{
	u8int c;
	
	read(fd, &c, 1);
	return c;
}

static int
get16(void)
{
	int i;
	
	i = get8();
	i |= get8() << 8;
	return i;
}

static int
get32(void)
{
	int i;
	
	i = get8();
	i |= get8() << 8;
	i |= get8() << 16;
	i |= get8() << 24;
	return i;
}

void
loadstate(char *file)
{
	flushram();
	fd = open(file, OREAD);
	if(fd < 0){
		message("open: %r");
		return;
	}
	read(fd, mem, 65536);
	if(ram != nil)
		read(fd, ram, rambanks * 8192);
	read(fd, R, sizeof R);
	sp = get16();
	pc = get16();
	Fl = get8();
	halt = get32();
	IME = get32();
	clock = get32();
	ppuclock = get32();
	divclock = get32();
	syncclock = get32();
	timerfreq = get32();
	timer = get32();
	rombank = get32();
	rambank = get32();
	ramen = get32();
	battery = get32();
	ramrom = get32();
	close(fd);
}

void
savestate(char *file)
{
	flushram();
	fd = create(file, ORDWR, 0666);
	if(fd < 0){
		message("create: %r");
		return;
	}
	write(fd, mem, 65536);
	if(ram != nil)
		write(fd, ram, rambanks * 8192);
	write(fd, R, sizeof R);
	put16(sp);
	put16(pc);
	put8(Fl);
	put32(halt);
	put32(IME);
	put32(clock);
	put32(ppuclock);
	put32(divclock);
	put32(timerclock);
	put32(syncclock);
	put32(timerfreq);
	put32(timer);
	put32(rombank);
	put32(rambank);
	put32(ramen);
	put32(battery);
	put32(ramrom);
	close(fd);
}
