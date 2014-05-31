#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

static int fd;

void
put8(u8int i)
{
	write(fd, &i, 1);
}

void
put16(u16int i)
{
	put8(i);
	put8(i >> 8);
}

void
put32(u32int i)
{
	put8(i);
	put8(i >> 8);
	put8(i >> 16);
	put8(i >> 24);
}

int
get8(void)
{
	u8int c;
	
	read(fd, &c, 1);
	return c;
}

int
get16(void)
{
	int i;
	
	i = get8();
	i |= get8() << 8;
	return i;
}

int
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
	fd = open(file, OREAD);
	if(fd < 0){
		message("open: %r");
		return;
	}
	read(fd, mem, sizeof(mem));
	read(fd, ppuram, sizeof(ppuram));
	read(fd, oam, sizeof(oam));
	if(chrram)
		read(fd, chr, nchr * CHRSZ);
	rA = get8();
	rX = get8();
	rY = get8();
	rS = get8();
	rP = get8();
	nmi = get8();
	pc = get16();
	pput = get16();
	ppuv = get16();
	ppusx = get8();
	ppux = get16();
	ppuy = get16();
	mirr = get8();
	odd = get8();
	vramlatch = get8();
	keylatch = get8();
	keylatch2 = get8();
	vrambuf = get8();
	clock = get32();
	ppuclock = get32();
	apuclock = get32();
	apuseq = get8();
	dmcaddr = get16();
	dmccnt = get16();
	read(fd, apuctr, sizeof(apuctr));
	mapper[map](RSTR, 0);
	close(fd);
}

void
savestate(char *file)
{
	fd = create(file, ORDWR, 0666);
	if(fd < 0){
		message("create: %r");
		return;
	}
	write(fd, mem, sizeof(mem));
	write(fd, ppuram, sizeof(ppuram));
	write(fd, oam, sizeof(oam));
	if(chrram)
		write(fd, chr, nchr * CHRSZ);
	put8(rA);
	put8(rX);
	put8(rY);
	put8(rS);
	put8(rP);
	put8(nmi);
	put16(pc);
	put16(pput);
	put16(ppuv);
	put8(ppusx);
	put16(ppux);
	put16(ppuy);
	put8(mirr);
	put8(odd);
	put8(vramlatch);
	put8(keylatch);
	put8(keylatch2);
	put8(vrambuf);
	put32(clock);
	put32(ppuclock);
	put32(apuclock);
	put8(apuseq);
	put16(dmcaddr);
	put16(dmccnt);
	write(fd, apuctr, sizeof(apuctr));
	mapper[map](SAVE, 0);
	close(fd);
}
