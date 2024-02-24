#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

uchar bios[16*KB], wram0[32*KB], wram1[256*KB];
uchar vram[96*KB];
u16int pram[512], oam[512];
uchar *rom, *back;
int nrom, nback;
u16int reg[512];
int dmaact;
enum {
	DMASRC,
	DMADST,
	DMACNT
};
u32int dmar[16];
u8int waitst[16] = {5, 5, 5, 5, 3, 5, 5, 9, 8, 10, 10, 14};
u32int eepstart;

Var memvars[] = {
	ARR(wram0), ARR(wram1), ARR(vram), ARR(pram), ARR(oam), ARR(reg),
	VAR(dmaact), ARR(dmar), ARR(waitst),
	{nil, 0, 0},
};

extern int cyc;

static int eepromread(void);
static void eepromwrite(int);
static u8int flashread(u16int);
static void flashwrite(u16int, u8int);

static u32int
arread(uchar *c, int n)
{
	switch(n){
	default:
		return c[0];
	case 2:
		return c[0] | c[1] << 8;
	case 4:
		return c[0] | c[1] << 8 | c[2] << 16 | c[3] << 24;
	}
}

static void
arwrite(uchar *c, u32int v, int n)
{
	switch(n){
	case 4:
		c[3] = v >> 24;
		c[2] = v >> 16;
	case 2:
		c[1] = v >> 8;
	default:
		c[0] = v;
	}
}

static u32int
ar16read(u16int *c, int h, int n)
{
	switch(n){
	case 1:
		return c[0] >> (h << 3);
	default:
		return c[0];
	case 4:
		return c[0] | c[1] << 16;
	}
}

static void
ar16write(u16int *c, int h, u32int v, int n)
{
	switch(n){
	case 1:
		if(h)
			c[0] = c[0] & 0xff | ((u8int)v) << 8;
		else
			c[0] = c[0] & 0xff00 | (u8int)v;
		break;
	case 2:
		c[0] = v;
		break;
	case 4:
		c[0] = v;
		c[1] = v >> 16;
		break;
	}
}

static u32int
regread(u32int a)
{
	u32int v;

	switch(a){
	case DISPSTAT*2:
		v = reg[a/2] & ~7;
		
		if(ppuy >= 160 && ppuy != 227)
			v |= 1;
		if(hblank)
			v |= 2;
		if(ppuy == v >> 8)
			v |= 4;
		return v;
	case 0x006:
		return ppuy;
	case 0x100: case 0x104: case 0x108: case 0x10c:
		return timerget((a - 0x100) / 4);
	case 0x130:
		return keys ^ 0x3ff;
	default:
		return reg[a/2];
	}
}

static void
regwrite16(u32int a, u16int v)
{
	u16int *p;
	int i;
	static u8int ws0[4] = {5,4,3,9};

	if(a < 0x56)
		ppuwrite(a, v);
	else if(a < 0xa0)
		sndwrite(a, v);
	p = &reg[a/2];
	switch(a){
	case IF*2:
		*p &= ~v;
		setif(0);
		return;
	case IME*2: case IE*2:
		*p = v;
		setif(0);
		return;
	case DMA0CNTH*2: case DMA1CNTH*2: case DMA2CNTH*2: case DMA3CNTH*2:
		i = (a - DMA0CNTH*2) / 12;
		if((v & DMAEN) != 0){
			if((v >> DMAWHEN & 3) == 0)
				dmaact |= 1<<i;
			if(i == 3 && (v >> DMAWHEN & 3) == 3)
				print("DMA video capture mode\n");
			dmar[4*i + DMASRC] = p[-5] | p[-4] << 16;
			dmar[4*i + DMADST] = p[-3] | p[-2] << 16;
			dmar[4*i + DMACNT] = p[-1];
		}else
			dmaact &= ~1<<i;
		break;
	case SOUNDCNTH*2:
		soundcnth(v);
		break;
	case FIFOAH*2: case FIFOBH*2:
		fifoput(a >> 2 & 1, p[-1] | v << 16);
		break;
	case 0x102: case 0x106: case 0x10a: case 0x10e:
		timerset((a - 0x102) / 4, v);
		break;
	case WAITCNT*2:
		waitst[3] = waitst[7] = ws0[v & 3];
		waitst[0] = ws0[v >> 2 & 3];
		waitst[4] = ((v & 1<<4) == 0) + 2;
		waitst[1] = ws0[v >> 5 & 3];
		waitst[5] = (v & 1<<7) == 0 ? 5 : 2;
		waitst[2] = ws0[v >> 8 & 3];
		waitst[6] = (v & 1<<10) == 0 ? 9 : 2;
		for(i = 0; i < 8; i++)
			waitst[8 + i] = waitst[i] + waitst[i | 4];
		break;
	case 0x301:
		cpuhalt = 1;
		break;
	}
	*p = v;
}

static void
regwrite(u32int a, u32int v, int n)
{
	u16int w;

	switch(n){
	case 1:
		if((a & ~1) == IF)
			w = 0;
		else
			w = regread(a);
		if((a & 1) == 0)
			w = w & 0xff00 | (u8int)v;
		else
			w = w & 0xff | v << 8;
		regwrite16(a & ~1, w);
		break;
	default:
		regwrite16(a, v);
		break;
	case 4:
		regwrite16(a, v);
		regwrite16(a + 2, v >> 16);
		break;
	}
}

void
setif(u16int v)
{
	reg[IF] |= v;
	irq = (reg[IME] & 1) != 0 && (reg[IF] & reg[IE]) != 0;
	if(irq)
		cpuhalt = 0;
}

u32int
memread(u32int a, int n, int seq)
{
	u32int b;
	assert((a & n-1) == 0);

	switch(a >> 24){
	case 0:
		b = a & sizeof(bios) - 1;
		cyc++;
		return arread(bios + b, n);
	case 2:
		b = a & sizeof(wram1) - 1;
		cyc += n > 2 ? 6 : 3;
		return arread(wram1 + b, n);
	case 3:
		b = a & sizeof(wram0) - 1;
		cyc++;
		return arread(wram0 + b, n);
	case 4:
		b = a & 0xffffff;
		if(b >= sizeof(reg)) goto fault;
		cyc++;
		if(n == 4)
			return regread(b) | regread(b+2) << 16;
		else if(n == 1)
			if((b & 1) != 0)
				return regread(b) >> 8;
			else
				return regread(b) & 0xff;
		return regread(b);
	case 5:
		b = a & sizeof(pram) - 1;
		cyc += (n+1) >> 1;
		return ar16read(pram + b/2, b & 1, n);
	case 6:
		b = a & 128*KB - 1;
		if(b >= 64*KB)
			b &= ~(32*KB);
		cyc += (n+1) >> 1;
		return arread(vram + b, n);
	case 7:
		b = a & sizeof(oam) - 1;
		cyc++;
		return ar16read(oam + b/2, b & 1, n);
	case 8:
		if(!gpiogame)
			goto Rom;
		b = a & 0xffffff;
		switch(b){
		case 0xC4:
			return gpiordata();
		case 0xC6:
			return gpiordir();
		case 0xC8:
			return gpiorcontrol();
		}
		/* fallthrough */
	case 9: case 10: case 11: case 12: case 13:
	Rom:
		b = a & 0x1ffffff;
		cyc += waitst[(a >> 25) - 4 | seq << 2 | (n > 2) << 3];
		if(b >= nrom){
			if(b >= eepstart)
				return eepromread();
			return 0;
		}
		return arread(rom + b, n);
	case 14:
		if(backup == SRAM){
			b = a & nback - 1;
			return arread(back + b, n);
		}
		if(backup == FLASH)
			return flashread(a);
		return 0;
	default:
	fault:
		print("read from %#.8ux (pc=%#.8ux)\n", a, curpc);
		return 0;
	}
}

void
memwrite(u32int a, u32int v, int n)
{
	u32int b, t;
	assert((a & n-1) == 0);

	switch(a >> 24){
	case 0:
		return;
	case 2:
		b = a & sizeof(wram1) - 1;
		cyc += n > 2 ? 6 : 3;
		arwrite(wram1 + b, v, n);
		return;
	case 3:
		b = a & sizeof(wram0) - 1;
		cyc++;
		arwrite(wram0 + b, v, n);
		return;
	case 4:
		cyc++;
		b = a & 0xffffff;
		if(b == 0x410) return;
		if(b >= sizeof(reg)) goto fault;
		regwrite(b, v, n);
		return;
	case 5:
		b = a & sizeof(pram) - 1;
		if(n == 1){
			b &= ~1;
			n = 2;
			v |= v << 8;
		}
		cyc += (n+1) >> 1;
		ar16write(pram + b/2, b & 1, v, n);
		return;
	case 6:
		b = a & 128*KB - 1;
		if(b >= 64*KB)
			b &= ~(32*KB);
		if(n == 1 && ((reg[DISPCNT] & 7) > 2 && b < 80*KB || b < 64*KB)){
			b &= ~1;
			n = 2;
			v |= v << 8;
		}
		cyc += (n+1) >> 1;
		if(n != 1)
			arwrite(vram + b, v, n);
		return;
	case 7:
		b = a & sizeof(oam) - 1;
		cyc++;
		if(n != 1)
			ar16write(oam + b/2, b & 1, v, n);
		return;
	case 8:
		if(!gpiogame)
			goto Rom;
		b = a & 0xffffff;
		switch(b){
		case 0xC4:
			t = v&0xFFFF;
			gpiowdata(t);
			if(n <= 2)
				return;
			v>>=16;
		case 0xC6:
			t = v&0xFFFF;
			gpiowdir(t);
			return;
		case 0xC8:
			t = v&0xFFFF;
			gpiowcontrol(t);
			return;
		}
		/* fallthrough */
	case 9: case 10: case 11: case 12: case 13:
	Rom:
		if(backup == EEPROM){
			b = a & 0x01ffffff;
			if(b >= eepstart)
				eepromwrite(v & 1);
		}
		return;
	case 14:
		if(backup == SRAM){
			b = a & nback - 1;
			arwrite(back + b, v, n);
			writeback();
			return;
		}
		if(backup == FLASH){
			flashwrite(a, v);
			return;
		}
		return;
	default:
	fault:
		print("write to %#.8ux, value %#.8ux (pc=%#.8ux)\n", a, v, curpc);
	}
}

void
memreset(void)
{
	reg[0x88/2] = 0x200;
	reg[BG2PA] = reg[BG2PD] = 0x100;
	if(backup == EEPROM)
		if(nrom <= 16*KB*KB)
			eepstart = 0x1000000;
		else
			eepstart = 0x1ffff00;
	else
		eepstart = -1;
}

int
dmastep(void)
{
	int i;
	u16int *cntp, cnt;
	u32int *dr;
	u32int v;
	int sz, snd;
	
	cyc = 0;
	for(i = 0; i < 4; i++)
		if((dmaact & 1<<i) != 0)
			break;
	if(i == 4)
		return cyc;
	curpc = -1;
	cntp = reg + DMA0CNTH + i * 6;
	cnt = *cntp;
	dr = dmar + 4 * i;
	snd = (cnt >> DMAWHEN & 3) == 3 && (i == 1 || i == 2);
	if(snd)
		cnt = cnt & ~(3 << DMADCNT) | DMAFIX << DMADCNT | DMAWIDE;

	sz = (cnt & DMAWIDE) != 0 ? 4 : 2;
	if(i == 0)
		dr[DMASRC] &= 0x07FFFFFF;
	else
		dr[DMASRC] &= 0x0FFFFFFF;
	if(i != 3)
		dr[DMADST] &= 0x7FFFFFFF;
	else
		dr[DMADST] &= 0x0FFFFFFF;
	v = memread(dr[DMASRC] & -sz, sz, 1);
	memwrite(dr[DMADST] & -sz, v, sz);
	switch(cnt >> DMADCNT & 3){
	case DMAINC: case DMAINCREL: dr[DMADST] += sz; break;
	case DMADEC: dr[DMADST] -= sz; break;
	}
	switch(cnt >> DMASCNT & 3){
	case DMAINC: dr[DMASRC] += sz; break;
	case DMADEC: dr[DMASRC] -= sz; break;
	}
	if(dr[DMACNT] == 0)
		dr[DMACNT] = i != 3 ? 0x4000 : 0x10000;
	if(--dr[DMACNT] == 0){
		dmaact &= ~(1<<i);
		if((cnt & DMAREP) != 0){
			dr[DMACNT] = cntp[-1];
			if((cnt >> DMADCNT & 3) == DMAINCREL)
				dr[DMADST] = cntp[-3] | cntp[-2] << 16;
		}else
			*cntp &= ~DMAEN;
		if((cnt & DMAIRQ) != 0)
			setif(IRQDMA0 << i);
	}
	return cyc;
}

void
dmastart(int cond)
{
	int i;
	u16int *cntp, cnt, c;
	
	cntp = reg + DMA0CNTH;
	for(i = 0; i < 4; i++, cntp += 6){
		cnt = *cntp;
		if((cnt & DMAEN) == 0)
			continue;
		c = cnt >> DMAWHEN & 3;
		if(c == 3)
			c += (i + 1) / 2;
		if(c == cond){
			dmaact |= 1<<i;
			if(c == DMASOUND)
				dmar[i * 4 + DMACNT] = 4;
		}
	}
}

int eepromstate, eeprompos, eepromaddr;
u64int eepromdata;

enum {
	EEPROMCMD,
	EEPROMRDCMD,
	EEPROMRDRESP,
	EEPROMWRCMD,
	EEPROMWRDATA,
	EEPROMWRRESP,
};

static int
eepromread(void)
{
	int v;

	switch(eepromstate){
	case EEPROMRDRESP:
		eeprompos++;
		if(eeprompos <= 4)
			return 0;
		if(eeprompos == 68){
			eepromstate = EEPROMCMD;
			eeprompos = 0;
		}
		v = eepromdata >> 63;
		eepromdata <<= 1;
		return v;
	case EEPROMWRRESP:
		if(++eeprompos == 1000){
			eepromstate = EEPROMCMD;
			eeprompos = 0;
			return 1;
		}
		return 0;
	default:
		return 0;
	}
}

static void
eepromwrite(int n)
{
	uchar *p;

	switch(eepromstate){
	case EEPROMCMD:
		eepromaddr = eepromaddr << 1 | n;
		if(++eeprompos >= 2){
			switch(eepromaddr & 3){
			case 2:
				eepromstate = EEPROMWRCMD;
				break;
			case 3:
				eepromstate = EEPROMRDCMD;
				break;
			}
			eeprompos = 0;
		}
		break;
	case EEPROMRDCMD:
	case EEPROMWRCMD:
		eepromaddr = eepromaddr << 1 | n;
		eeprompos++;
		if(nback == 512){
			if(eeprompos >= 7)
				eepromaddr = eepromaddr >> 1 & 0x3f;
			else
				break;
		}else{
			if(eeprompos >= 15)
				eepromaddr = eepromaddr >> 1 & 0x3fff;
			else
				break;
		}
		if(eepromstate == EEPROMRDCMD){
			p = back + eepromaddr * 8;
			eepromdata = p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24 | (u64int)p[4] << 32 | 
				(u64int)p[5] << 40 | (u64int)p[6] << 48 | (u64int)p[7] << 56;
			eeprompos = 0;
			eepromstate = EEPROMRDRESP;
			break;
		}else{
			eepromdata = n;
			eeprompos = 1;
			eepromstate = EEPROMWRDATA;
			break;
		}
	case EEPROMWRDATA:
		if(eeprompos == 64){
			p = back + eepromaddr * 8;
			p[0] = eepromdata;
			p[1] = eepromdata >> 8;
			p[2] = eepromdata >> 16;
			p[3] = eepromdata >> 24;
			p[4] = eepromdata >> 32;
			p[5] = eepromdata >> 40;
			p[6] = eepromdata >> 48;
			p[7] = eepromdata >> 56;
			eepromstate = EEPROMWRRESP;
			eeprompos = 0;
			writeback();
			break;
		}
		eepromdata = eepromdata << 1 | n;
		eeprompos++;
		break;
	}
}

int flashstate, flashmode, flashbank, flashid;

enum {
	FLASHCMD0,
	FLASHCMD1,
	FLASHCMD2,
	FLASHBANK,
	FLASHWRITE,
	
	FLASHID = 1,
	FLASHERASE = 2,
};

static u8int
flashread(u16int a)
{
	if((flashmode & FLASHID) != 0)
		return (a & 1) != 0 ? flashid >> 8 : flashid;
	return back[(flashbank << 16) + a];
}

static void
flashwrite(u16int a, u8int v)
{
	int erase;

	switch(flashstate){
	case FLASHCMD0:
		if(a == 0x5555 && v == 0xaa)
			flashstate = FLASHCMD1;
		break;
	case FLASHCMD1:
		if(a == 0x2aaa && v == 0x55)
			flashstate = FLASHCMD2;
		else
			flashstate = FLASHCMD0;
		break;
	case FLASHCMD2:
		flashstate = FLASHCMD0;
		erase = flashmode & FLASHERASE;
		flashmode &= ~FLASHERASE;
		switch(v){
		case 0x90: flashmode |= FLASHID; break;
		case 0xF0: flashmode &= ~FLASHID; break;
		case 0x80: flashmode |= FLASHERASE; break;
		case 0x10:
			if(erase){
				memset(back, 0xff, nback);
				writeback();
			}
			break;
		case 0x30:
			if(erase){
				memset(back + (a & 0xf000) + (flashbank << 16), 0xff, 4096);
				writeback();
			}
			break;
		case 0xA0:
			writeback();
			flashstate = FLASHWRITE;
			break;
		case 0xB0: flashstate = FLASHBANK; break;
		default:
			print("unknown flash cmd %x\n", v);
		}
		break;
	case FLASHBANK:
		flashbank = v % (nback >> 16);
		flashstate = FLASHCMD0;
		break;
	case FLASHWRITE:
		back[(flashbank << 16) + a] &= v;
		writeback();
		flashstate = FLASHCMD0;
		break;
	}
}

