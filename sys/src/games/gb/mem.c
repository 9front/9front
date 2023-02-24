#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

u8int *rom;
u8int *romb, *vramb, *wramb, *eramb;
u8int wram[32768], vram[16384], oam[256], reg[256];
u8int *back;
u8int palm[128];
u32int pal[64];
int nrom, nback, nbackbank;
u32int divclock;
int prish;
MBC3Timer timer, timerl;
u8int dma;
u32int white;
u32int moncols[4];

Var memvars[] = {ARR(wram), ARR(vram), ARR(oam), ARR(reg), ARR(palm), VAR(clock), VAR(divclock), VAR(mode), VAR(dma), {nil, 0, 0}};

u8int
regread(u8int a)
{
	u8int v;

	switch(a){
	case JOYP:
		v = reg[a] & 0xff;
		if((reg[a] & 0x10) == 0)
			v &= 0xf0 | ~keys;
		if((reg[a] & 0x20) == 0)
			v &= 0xf0 | ~(keys >> 4);
		return v;
	case DIV:
		return reg[DIV] + (clock - divclock >> 7 - ((mode & TURBO) != 0));
	case TIMA:
		return timread();
	case STAT:
		return reg[a] & 0xf8 | (reg[LYC] == ppuy) << 2 | ppustate;
	case LY:
		return ppuy;
	case BCPD:
		return palm[reg[BCPS] & 0x3f];
	case OCPD:
		return palm[0x40 | reg[OCPS] & 0x3f];
	case NR13: case NR23: case NR31: case NR33: case NR41:
		return 0xff;
	case NR14: case NR24: case NR34: case NR44:
		return reg[a] | 0xbf;
	case NR52:
		return apustatus;
	case NR11: case NR21:
		return reg[a] | 0x3f;
	case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
	case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f:
		return waveread(a & 0xf);
	default:
		return reg[a];
	}
}

void
colcol(int i, u16int v)
{
	union { u8int c[4]; u32int l; } c;

	c.c[0] = v >> 7 & 0xf8;
	c.c[1] = v >> 2 & 0xf8;
	c.c[2] = v << 3;
	c.c[3] = 0;
	pal[i] = c.l;	
}

void
regwrite(u8int a, u8int v)
{
	extern Event evhblank;
	int i;
	u8int u;

	switch(a){
	case JOYP: v |= 0xcf; break;
	case SC:
		if((reg[a] & 0x80) == 0 && (v & 0x80))
			serialwrite();
		reg[a] = v;
		return;
	case DIV:
		divclock = clock;
		v = 0;
		break;
	case TIMA:
		reg[a] = v;
		timerset();
		return;
	case TAC:
		v |= 0xf8;
		timertac(v, 0);
		break;
	case STAT:
		v |= 0x80;
		if((v & IRQLYC) != 0 && ppuy == reg[LYC])
			reg[IF] |= IRQLCDS;
		break;
	case LYC:
		if((reg[STAT] & IRQLYC) != 0 && ppuy == v)
			reg[IF] |= IRQLCDS;
		break;
	case LCDC:
		ppusync();
		if((~v & reg[a] & LCDEN) != 0){
			ppuy = 0;
			ppuw = 0;
			ppustate = 0;
			delevent(&evhblank);
		}
		if((v & ~reg[a] & LCDEN) != 0)
			addevent(&evhblank, 456*2);
		break;
	case SCY: case SCX: case WY: case WX:
		ppusync();
		break;
	case VBK:
		if((mode & COL) == 0)
			goto ff;
		vramb = vram + ((v & 1) << 13);
		v |= 0xfe;
		break;
	case SVBK:
		if((mode & COL) == 0)
			goto ff;
		v &= 7;
		wramb = wram + (v + (v - 1 >> 3 & 1) << 12);
		v |= 0xf8;
		break;
	case BGP:
	case OBP0:
	case OBP1:
		if((mode & COL) != 0)
			break;
		ppusync();
		i = a - BGP << 2;
		pal[i] = moncols[~v & 3];
		pal[i+1] = moncols[~v >> 2 & 3];
		pal[i+2] = moncols[~v >> 4 & 3];
		pal[i+3] = moncols[~v >> 6 & 3];
		break;
	case DMA:
		for(i = 0; i < 160; i++)
			oam[i] = memread(v << 8 | i);
		break;
	case BCPS: v |= 0x40; break;
	case OCPS: v |= 0x40; break;
	case BCPD:
		if((mode & COL) == 0)
			break;
		ppusync();
		u = reg[BCPS] & 0x3f;
		palm[u] = v;
		colcol(u / 2, palm[u & 0xfe] | palm[u | 1] << 8);
		if((reg[BCPS] & 0x80) != 0)
			reg[BCPS] = reg[BCPS] + 1 - (u + 1 & 0x40);
		break;
	case OCPD:
		if((mode & COL) == 0)
			break;
		ppusync();
		u = 0x40 | reg[OCPS] & 0x3f;
		palm[u] = v;
		colcol(u / 2, palm[u & 0xfe] | palm[u | 1] << 8);
		if((reg[OCPS] & 0x80) != 0)
			reg[OCPS] = reg[OCPS] + 1 - ((reg[OCPS] & 0x3f) + 1 & 0x40);
		break;
	case IF: v |= 0xe0; break;
	case IE: v &= 0x1f; break;
	case KEY1: v |= 0x7e; break;
	case HDMAC:
		if((mode & COL) == 0)
			goto ff;
		if(v & 0x80){
			v &= 0x7f;
			dma = DMAHBLANK;
		}else if(dma){
			v |= 0x80;
			dma = 0;
		}else
			dma = DMAREADY;
		break;
	case NR10: v |= 0x80; goto snd;
	case NR14: case NR24: v |= 0x38; goto snd;
	case NR30: v |= 0x7f; goto snd;
	case NR32: v |= 0x9f; goto snd;
	case NR41: v |= 0xc0; goto snd;
	case NR44: v |= 0x3f; goto snd;
	case NR52: v |= 0x70; goto snd;
	case NR11: case NR12: case NR13:
	case NR21: case NR22: case NR23:
	case NR31: case NR33: case NR34:
	case NR42: case NR43:
	case NR50: case NR51:
	snd:
		sndwrite(a, v);
		return;
	case SB:
	case TMA:
		break;
	case HDMASL: case HDMASH: case HDMADL: case HDMADH: case RP:
		if((mode & COL) == 0)
			goto ff;
		break;
	case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
	case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f:
		wavewrite(a & 0xf, v);
		return;
	default:
		if(a >= 0x80)
			break;
	ff:
		v = 0xff;
	}
	reg[a] = v;
}

static void
nope(int p)
{
	print("unimplemented mapper function %d (mapper %d)\n", p, mbc);
}

static int
mbc0(int a, int)
{
	if(a < 0)
		switch(a){
		case INIT:
			if(nback != 0)
				eramb = back;
			return 0;
		case SAVE:
		case RSTR:
			return 0;
		case READ:
			return -1;
		default:
			nope(a);
		}
	return 0;
}

static int
mbc1(int a, int v)
{
	static u8int ramen, b0, b1, romram;
	static Var mbc1vars[] = {VAR(ramen), VAR(b0), VAR(b1), VAR(romram), {nil, 0, 0}};
	u16int b;

	if(a < 0)
		switch(a){
		case INIT:
			return 0;
		case SAVE:
			putvars(mbc1vars);
			break;
		case RSTR:
			getvars(mbc1vars);
			break;
		case READ:
			return -1;
		default:
			nope(a);
		}
	switch(a >> 13){
	case 0: ramen = (v & 0xf) == 0xa; break;
	case 1: v &= 0x1f; b0 = v != 0 ? v : 1; break;
	case 2: b1 = v & 3; b1 %= nbackbank; break;
	case 3: romram = v & 1; break;
	}
	b = b0;
	if(!romram)
		b |= b1 << 5;
	b %= nrom >> 14;
	romb = rom + (b << 14);
	if(ramen)
		if(romram)
			eramb = back + (b1 << 13);
		else
			eramb = back;
	else
		eramb = nil;
	return 0;
}

static int
mbc2(int a, int v)
{
	static int ramen, b;
	static Var mbc2vars[] = {VAR(ramen), VAR(b), {nil, 0, 0}};

	if(a < 0)
		switch(a){
		case INIT:
			return 0;
		case SAVE:
			putvars(mbc2vars);
			return 0;
		case RSTR:
			getvars(mbc2vars);
			romb = rom + (b << 14);
			return 0;
		case READ:
			if(ramen)
				return back[a & 0x1ff];
			return 0xff;
		default:
			nope(a);
		}
	if((a & 0xc100) == 0)
		ramen = (v & 0x0f) == 0x0a;
	else if((a & 0xc100) == 0x100){
		b = v & 0x0f;
		if(b == 0)
			b++;
		b %= nrom >> 14;
		romb = rom + (b << 14);
	}else if((a >> 12) == 0xa && ramen)
		back[a & 0x1ff] = v | 0xf0;
	return 0;
}

void
timerforward(MBC3Timer *t)
{
	vlong n, nd;
	uint x;
	
	n = nsec();
	nd = n - t->ns;
	if(nd < 0)
		return;
	if((t->dh & 0x40) != 0){
		t->ns = n;
		return;
	}
	t->ns = n - nd % BILLION;
	x = t->sec + t->min * 60 + t->hr * 3600 + ((t->dh & 1) << 8 | t->dl) * 86400;
	x += nd / BILLION;
	t->sec = x % 60;
	x /= 60;
	t->min = x % 60;
	x /= 60;
	t->hr = x % 24;
	x /= 24;
	t->dl = x & 0xff;
	x >>= 8;
	t->dh = t->dh & 0xfe | x & 1;
	if(x >= 2) t->dh |= 0x80;
}

static int
mbc3(int a, int v)
{
	static u8int ramen, b0, b1, latch;
	static Var mbc3vars[] = {VAR(ramen), VAR(b0), VAR(b1), VAR(latch),
		VAR(timer.ns), VAR(timer.sec), VAR(timer.min), VAR(timer.hr), VAR(timer.dl), VAR(timer.dh),
		VAR(timerl.sec), VAR(timerl.min), VAR(timerl.hr), VAR(timerl.dl), VAR(timerl.dh), {nil, 0, 0}};

	if(a < 0)
		switch(a){
		case INIT:
			return 0;
		case SAVE:
			putvars(mbc3vars);
			return 0;
		case RSTR:
			getvars(mbc3vars);
			romb = rom + (b0 << 14);
			break;
		case READ:
			if(!ramen)
				return -1;
			switch(b1){
			case 8: return timerl.sec;
			case 9: return timerl.min;
			case 10: return timerl.hr;
			case 11: return timerl.dl;
			case 12: return timerl.dh;
			}
			return -1;
		default:
			nope(a);
		}
	switch(a >> 13){
	case 0: ramen = (v & 0xf) == 0xa; break;
	case 1:
		v &= 0x7f;
		b0 = v != 0 ? v : 1;
		b0 %= nrom >> 14;
		romb = rom + (b0 << 14);
		return 0;
	case 2: b1 = v & 15; break;
	case 3:
		if(latch == 0 && v == 1){
			timerforward(&timer);
			timerl = timer;
		}
		latch = v;
		break;
	case 5:
		if(!ramen)
			return 0;
		switch(b1){
		case 8: timerforward(&timer); timer.sec = v; break;
		case 9: timerforward(&timer); timer.min = v; break;
		case 10: timerforward(&timer); timer.hr = v; break;
		case 11: timerforward(&timer); timer.dl = v; break;
		case 12: timerforward(&timer); timer.dh = v; break;
		}
		return 0;
	}
	eramb = ramen && b1 < nbackbank ? back + (b1 << 13) : nil;
	return 0;
}

static int
mbc5(int a, int v)
{
	static u8int ramen, b1;
	static u16int b0;
	static Var mbc5vars[] = {VAR(ramen), VAR(b0), VAR(b1), {nil, 0, 0}};

	if(a < 0)
		switch(a){
		case INIT:
			return 0;
		case SAVE:
			putvars(mbc5vars);
			return 0;
		case RSTR:
			getvars(mbc5vars);
			break;
		case READ:
			return -1;
		default:
			nope(a);
		}
	switch(a >> 12){
	case 0: case 1: ramen = (v & 0xf) == 0xa; break;
	case 2: b0 = b0 & 0x100 | v; break;
	case 3: b0 = b0 & 0xff | v << 8 & 0x100; break;
	case 4: b1 = v & 0xff; b1 %= nbackbank; break;
	}
	b0 %= nrom >> 14;
	romb = rom + (b0 << 14); 
	eramb = ramen ? back + (b1 << 13) : nil;
	return 0;
	
}

int (*mappers[7])(int, int) = {mbc0, mbc1, mbc2, mbc3, mbc0, mbc5, mbc0};
int (*mapper)(int, int);

u8int
memread(u16int a)
{
	switch(a >> 12){
	case 0: case 1: case 2: case 3:
		return rom[a];
	case 4: case 5: case 6: case 7:
		return romb[a - 0x4000];
	case 8: case 9:
		return vramb[a - 0x8000];
	case 10: case 11:
		if(eramb != nil)
			return eramb[a - 0xa000];
		return mapper(READ, a);
	case 12: case 14:
		return wram[a & 0xfff];
	case 13:
		return wramb[a & 0xfff];
	case 15:
		if(a >= 0xff00)
			return regread(a);
		else if(a >= 0xfe00)
			return oam[a - 0xfe00];
	}
	return 0xff;
}

void
memwrite(u16int a, u8int v)
{
	switch(a >> 12){
	case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
		mapper(a, v);
		return;
	case 8: case 9:
		vramb[a - 0x8000] = v;
		return;
	case 10: case 11:
		if(eramb != nil)
			eramb[a - 0xa000] = v;
		else
			mapper(a, v);
		writeback();
		return;
	case 12: case 14:
		wram[a & 0xfff] = v;
		return;
	case 13:
		wramb[a & 0xfff] = v;
		return;
	case 15:
		if(a >= 0xff00)
			regwrite(a, v);
		else if(a >= 0xfe00)
			oam[a - 0xfe00] = v;
		return;
	}
}

void
meminit(void)
{
	union { u8int c[4]; u32int l; } c;

	c.c[0] = c.c[1] = c.c[2] = 0;
	c.c[3] = 1;
	for(; c.l != 1; prish++)
		c.l >>= 1;
	
	c.c[0] = c.c[1] = c.c[2] = 0xff;
	c.c[3] = 0;
	white = c.l;

	romb = rom + 0x4000;
	wramb = wram + 0x1000;
	vramb = vram;
	mapper = mappers[mbc];
	mapper(INIT, 0);
	
	reg[LCDC] = 0x91;
	reg[VBK] = 0xfe;
	reg[SVBK] = 0xf8;
	reg[IF] = 0xe0;
	reg[HDMAC] = 0xff;
}

void
memload(void)
{
	int i;
	u8int v;

	if((mode & COL) != 0){
		for(i = 0; i < 64; i++)
			colcol(i, palm[2*i] | palm[2*i+1] << 8);
		vramb = vram + ((reg[VBK] & 1) << 13);
		wramb = wram + (reg[SVBK] + (reg[SVBK] - 1 >> 3 & 1) << 12);
	}else{
		v = reg[BGP];
		pal[0] = moncols[~v & 3];
		pal[1] = moncols[~v >> 2 & 3];
		pal[2] = moncols[~v >> 4 & 3];
		pal[3] = moncols[~v >> 6 & 3];
		v = reg[OBP0];
		pal[4] = moncols[~v & 3];
		pal[5] = moncols[~v >> 2 & 3];
		pal[6] = moncols[~v >> 4 & 3];
		pal[7] = moncols[~v >> 6 & 3];
		v = reg[OBP1];
		pal[8] = moncols[~v & 3];
		pal[9] = moncols[~v >> 2 & 3];
		pal[10] = moncols[~v >> 4 & 3];
		pal[11] = moncols[~v >> 6 & 3];
	}

}

int
dmastep(void)
{
	int i;
	u16int sa, da;

	sa = (reg[HDMASL] | reg[HDMASH] << 8) & 0xfff0;
	da = (reg[HDMADL] | reg[HDMADH] << 8) & 0x1ff0 | 0x8000;
	for(i = 0; i < 16; i++)
		memwrite(da++, memread(sa++));
	reg[HDMASL] += 16;
	if((reg[HDMASL] & 0xf0) == 0)
		reg[HDMASH]++;
	reg[HDMADL] += 16;
	if((reg[HDMADL] & 0xf0) == 0)
		reg[HDMADH]++;
	if(--reg[HDMAC] == 0xff)
		dma = 0;
	else if(dma & DMAHBLANK)
		dma &= ~DMAREADY;
	return 64;
}
