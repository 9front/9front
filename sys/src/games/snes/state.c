#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <bio.h>
#include "dat.h"
#include "fns.h"

static Biobuf *bp;

void
put8(u8int i)
{
	Bputc(bp, i);
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

void
put16s(u16int *p, int n)
{
	while(n--)
		put16(*p++);
}

int
get8(void)
{
	return Bgetc(bp);
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
get16s(u16int *p, int n)
{
	while(n--)
		*p++ = get16();
}

void
loadstate(char *file)
{
	bp = Bopen(file, OREAD);
	if(bp == nil){
		message("open: %r");
		return;
	}
	Bread(bp, reg, sizeof(reg));
	Bread(bp, mem, sizeof(mem));
	Bread(bp, vram, sizeof(vram));
	Bread(bp, oam, sizeof(oam));
	Bread(bp, spcmem, sizeof(spcmem));
	Bread(bp, dsp, sizeof(dsp));
	get16s(cgram, nelem(cgram));
	ppuclock = get32();
	spcclock = get32();
	dspclock = get32();
	stimerclock = get32();
	rA = get16();
	rX = get16();
	rY = get16();
	rS = get16();
	rP = get8();
	rD = get16();
	rDB = get8()<<16;
	pc = get16();
	rPB = get8()<<16;
	emu = get8();
	irq = get8();
	nmi = get8();
	dma = get8();
	hdma = get32();
	wai = get8();
	mdr = get8();
	mdr1 = get8();
	mdr2 = get8();
	oamaddr = get16();
	vramlatch = get16();
	keylatch = get32();
	ppux = get16();
	ppuy = get16();
	htime = reg[0x4207] | reg[0x4208] << 8 & 0x100;
	vtime = reg[0x4209] | reg[0x420a] << 8 & 0x100;
	subcolor = get16();
	get16s(hofs, nelem(hofs));
	get16s(vofs, nelem(vofs));
	get16s((u16int*) m7, nelem(m7));
	sA = get8();
	sX = get8();
	sY = get8();
	sS = get8();
	sP = get8();
	dspstate = get8();
	dspcounter = get16();
	noise = get16();
	Bread(bp, spctimer, sizeof(spctimer));
	dspload();
	Bterm(bp);
}

void
savestate(char *file)
{
	flushram();
	bp = Bopen(file, OWRITE);
	if(bp == nil){
		message("open: %r");
		return;
	}
	Bwrite(bp, reg, sizeof(reg));
	Bwrite(bp, mem, sizeof(mem));
	Bwrite(bp, vram, sizeof(vram));
	Bwrite(bp, oam, sizeof(oam));
	Bwrite(bp, spcmem, sizeof(spcmem));
	Bwrite(bp, dsp, sizeof(dsp));
	put16s(cgram, nelem(cgram));
	put32(ppuclock);
	put32(spcclock);
	put32(dspclock);
	put32(stimerclock);
	put16(rA);
	put16(rX);
	put16(rY);
	put16(rS);
	put8(rP);
	put16(rD);
	put8(rDB>>16);
	put16(pc);
	put8(rPB>>16);
	put8(emu);
	put8(irq);
	put8(nmi);
	put8(dma);
	put32(hdma);
	put8(wai);
	put8(mdr);
	put8(mdr1);
	put8(mdr2);
	put16(oamaddr);
	put16(vramlatch);
	put32(keylatch);
	put16(ppux);
	put16(ppuy);
	put16(subcolor);
	put16s(hofs, nelem(hofs));
	put16s(vofs, nelem(vofs));
	put16s((u16int*) m7, nelem(m7));
	put8(sA);
	put8(sX);
	put8(sY);
	put8(sS);
	put8(sP);
	put8(dspstate);
	put16(dspcounter);
	put16(noise);
	Bwrite(bp, spctimer, sizeof(spctimer));
	dspsave();
	Bterm(bp);
}
