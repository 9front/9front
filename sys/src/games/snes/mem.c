#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

u8int reg[32768];
u8int mem[131072];
u8int oam[544], vram[65536];
u16int cgram[256];
u16int oamaddr, vramlatch;
u32int keylatch, lastkeys;
int cyc;
enum {
	OAMLATCH,
	CGLATCH,
	CGLH,
	OFSPREV,
	M7PREV,
	OPCTLATCH,
	OPHCTH,
	OPVCTH,
};

u8int mdr, mdr1, mdr2;

extern void calc7(void);

static u16int
vrammap(u16int a)
{
	switch(reg[0x2115] & 12){
	default: return a << 1;
	case  4: return a << 1 & 0xfe00 | a << 4 & 0x01f0 | a >> 4 & 0x000e;
	case  8: return a << 1 & 0xfc00 | a << 4 & 0x03f0 | a >> 5 & 0x000e;
	case 12: return a << 1 & 0xf800 | a << 4 & 0x07f0 | a >> 6 & 0x000e;
	}
}

static void
vramread(void)
{
	u16int b;

	b = vrammap(reg[0x2116] | reg[0x2117] << 8);
	vramlatch = vram[b++];
	vramlatch |= vram[b] << 8;
}

static void
incvram(int i, int r)
{
	u16int a;
	int c;
	
	c = reg[0x2115];
	if((c >> 7) != i)
		return;
	a = reg[0x2116] | reg[0x2117] << 8;
	if(r)
		vramread();
	switch(c & 3){
	case 0: a++; break;
	case 1: a += 32; break;
	default: a += 128; break;
	}
	reg[0x2116] = a;
	reg[0x2117] = (a >> 8) & 0x7f;
}

static void
incwram(void)
{
	if(++reg[0x2181] == 0)
		if(++reg[0x2182] == 0)
			reg[0x2183] ^= 1;
}

static void
hvlatch(void)
{
	reg[0x213c] = ppux;
	reg[OPHCTH] = ppux >> 8;
	reg[0x213d] = ppuy;
	reg[OPVCTH] = ppuy >> 8;
	reg[OPCTLATCH] |= 0x40;
}

static u16int
swaprb(u16int a)
{
	return (a & 0x83e0) | (a & 0x7c00) >> 10 | (a & 0x001f) << 10;
}

static void
mouselatch(void)
{
	int x, y;
	u32int v;
	
	v = keys & 0xffff0000;
	x = (keys & 0xff) - (lastkeys & 0xff);
	y = (keys >> 8 & 0xff) - (lastkeys >> 8 & 0xff);
	if(x < 0){
		v |= 0x80;
		x = -x;
	}
	if(y < 0){
		v |= 0x8000;
		y = -y;
	}
	if(x > 127)
		x = 127;
	if(y > 127)
		y = 127;
	keylatch = v | x | y << 8;
	lastkeys = keys;
}

u8int
regread(u16int p)
{
	u8int v;
	u16int a;
	int r;

	if(p < 0x2000){
		cyc += 8;
		return mem[p];
	}
	cyc += 6;
	if((p & 0xfe00) == 0x4000)
		cyc += 6;
	switch(p){
	case 0x2134: case 0x2135: case 0x2136:
		r = ((signed short)m7[0] * (signed char)reg[0x211c]) & 0xffffff;
		return mdr1 = r >> 8 * (p - 0x2134);
	case 0x2137:
		if((reg[0x4201] & 0x80) != 0)
			hvlatch();
		return mdr;
	case 0x2138:
		if(oamaddr < 0x200)
			v = oam[oamaddr];
		else
			v = oam[oamaddr & 0x21f];
		oamaddr = (oamaddr + 1) & 0x3ff;
		return mdr1 = v;
	case 0x2139:
		v = vramlatch;
		incvram(0, 1);
		return mdr1 = v;
	case 0x213a:
		v = vramlatch >> 8;
		incvram(1, 1);
		return mdr1 = v;
	case 0x213b:
		a = swaprb(cgram[reg[0x2121]]);
		if(reg[CGLH] != 0){
			a >>= 8;
			a |= mdr2 & 0x80;
			reg[0x2121]++;
		}
		reg[CGLH] ^= 1;
		return mdr2 = a;
	case 0x213c:
		reg[OPCTLATCH] ^= 1;
		if((reg[OPCTLATCH] & 1) == 0)
			return mdr2 = reg[OPHCTH] | mdr2 & 0xfe;
		return mdr2 = reg[p];
	case 0x213d:
		reg[OPCTLATCH] ^= 2;
		if((reg[OPCTLATCH] & 2) == 0)
			return reg[OPVCTH] | mdr2 & 0xfe;
		return mdr2 = reg[p];
	case 0x213e:
		return mdr1 = reg[p] | mdr1 & 0x10;
	case 0x213f:
		v = reg[OPCTLATCH] & 0x40;
		if((reg[0x4201] & 0x80) != 0)
			reg[OPCTLATCH] &= ~0x43;
		else
			reg[OPCTLATCH] &= ~3;
		return mdr2 = reg[p] | v | mdr2 & 0x20;
	case 0x2180:
		v = mem[reg[0x2181] | reg[0x2182] << 8 | (reg[0x2183] & 1) << 16];
		incwram();
		return v;
	case 0x4016:
		if((reg[0x4016] & 1) != 0){
			if(mouse)
				if((keys & 0x300000) == 0x300000)
					keys &= ~0x300000;
				else
					keys += 0x100000;
			return keys >> 31;
		}
		v = keylatch >> 31;
		keylatch = (keylatch << 1) | 1;
		return v | mdr & 0xfc;
	case 0x4017:
		return 0x1f | mdr & 0xe0;
	case 0x4210:
		v = reg[p];
		reg[p] &= ~VBLANK;
		return v | mdr & 0x70;
	case 0x4211:
		v = irq;
		irq &= ~IRQPPU;
		return v | mdr & 0x7f;
	case 0x4212:
		v = 0;
		if(ppux >= 274 || ppux == 0)
			v |= 0x40;
		a = (reg[SETINI] & OVERSCAN) != 0 ? 0xf0 : 0xe1;
		if(ppuy >= a){
			v |= 0x80;
			if(ppuy <= a + 2 && (reg[NMITIMEN] & AUTOJOY) != 0)
				v |= 1;
		}
		return v | mdr & 0x3e;
	case 0x4214: case 0x4215: case 0x4216: case 0x4217: case 0x4218:
	case 0x4219: case 0x421a: case 0x421b: case 0x421c: case 0x421d:
	case 0x421e: case 0x421f:
		return reg[p];
	case 0x2104: case 0x2105: case 0x2106: case 0x2108: case 0x2109: case 0x210a:
	case 0x2114: case 0x2115: case 0x2116: case 0x2118: case 0x2119: case 0x211a:
	case 0x2124: case 0x2125: case 0x2126: case 0x2128: case 0x2129: case 0x212a:
		return mdr1;
	}
	if((p & 0xff80) == 0x4300)
		return reg[p];
	if((p & 0xffc0) == 0x2140)
		return spcmem[0xf4 | p & 3];
	return mdr;
}

void
regwrite(u16int p, u8int v)
{
	u16int a;

	if(p < 0x2000){
		cyc += 8;
		mem[p] = v;
		return;
	}
	cyc += 6;
	if((p & 0xfe00) == 0x4000)
		cyc += 6;
	switch(p){
	case 0x2102:
		oamaddr = (reg[0x2103] & 1) << 9 | v << 1;
		break;
	case 0x2103:
		oamaddr = (v & 1) << 9 | reg[0x2102] << 1;
		break;
	case 0x2104:
		if((oamaddr & 1) == 0)
			reg[OAMLATCH] = v;
		if(oamaddr < 0x200){
			if((oamaddr & 1) != 0){
				oam[oamaddr - 1] = reg[OAMLATCH];
				oam[oamaddr] = v;
			}
		}else
			oam[oamaddr & 0x21f] = v;
		oamaddr = (oamaddr + 1) & 0x3ff;
		return;
	case 0x2105:
		reg[p] = v;
		calc7();
		return;
	case 0x210d:
		hofs[4] = (v & 0x1f) << 8 | reg[M7PREV];
		if((v & 0x10) != 0)
			hofs[4] |= 0xe000;
		reg[M7PREV] = v;
		calc7();
	case 0x210f: case 0x2111: case 0x2113:
		a = (p - 0x210d) >> 1;
		hofs[a] = v << 8 | reg[OFSPREV] & ~7 | (hofs[a] >> 8) & 7;
		reg[OFSPREV] = v;
		break;
	case 0x210e:
		vofs[4] = (v & 0x1f) << 8 | reg[M7PREV];
		if((v & 0x10) != 0)
			vofs[4] |= 0xe000;
		reg[M7PREV] = v;
		calc7();
	case 0x2110: case 0x2112: case 0x2114:
		vofs[(p - 0x210e) >> 1] = v << 8 | reg[OFSPREV];
		reg[OFSPREV] = v;
		break;
	case 0x2117:
		v &= 0x7f;
	case 0x2116:
		reg[p] = v;
		vramread();
		return;
	case 0x2118:
		a = vrammap(reg[0x2116] | reg[0x2117] << 8);
		vram[a] = v;
		incvram(0, 0);
		return;
	case 0x2119:
		a = vrammap(reg[0x2116] | reg[0x2117] << 8);
		vram[a|1] = v;
		incvram(1, 0);
		return;
	case 0x211b: case 0x211c: case 0x211d:
	case 0x211e: case 0x211f: case 0x2120:
		m7[p - 0x211b] = v << 8 | reg[M7PREV];
		if(p >= 0x211f)
			if((v & 0x10) != 0)
				m7[p - 0x211b] |= 0xe000;
			else
				m7[p - 0x211b] &= 0x1fff;
		reg[M7PREV] = v;
		calc7();
		break;
	case 0x2121:
		reg[CGLH] = 0;
		break;
	case 0x2122:
		if(reg[CGLH] == 0)
			reg[CGLATCH] = v;
		else
			cgram[reg[0x2121]++] = swaprb(reg[CGLATCH] | v << 8);
		reg[CGLH] ^= 1;
		break;
	case 0x2132:
		if((v & 0x80) != 0) subcolor = subcolor & 0x7fe0 | v & 0x1f;
		if((v & 0x40) != 0) subcolor = subcolor & 0x7c1f | (v & 0x1f) << 5;
		if((v & 0x20) != 0) subcolor = subcolor & 0x03ff | (v & 0x1f) << 10;
		return;
	case 0x213e:
		return;
	case 0x2180:
		mem[reg[0x2181] | reg[0x2182] << 8 | (reg[0x2183] & 1) << 16] = v;
		incwram();
		return;
	case 0x4016:
		if((reg[0x4016] & 1) != 0 && (v & 1) == 0){
			if(mouse)
				mouselatch();
			else
				keylatch = keys | 0xffff;
		}
		break;
	case 0x4200:
		if((reg[0x4200] & 0x80) == 0 && (v & 0x80) != 0 && (reg[RDNMI] & 0x80) != 0)
			nmi = 2;
		if((v & (HCNTIRQ|VCNTIRQ)) == 0)
			irq &= ~IRQPPU;
		break;
	case 0x4201:
		if((reg[0x4201] & 0x80) == 0 && (v & 0x80) != 0)
			hvlatch();
		break;
	case 0x4203:
		a = reg[0x4202] * v;
		reg[0x4216] = a;
		reg[0x4217] = a >> 8;
		break;
	case 0x4206:
		if(v == 0){
			reg[0x4214] = 0xff;
			reg[0x4215] = 0xff;
			reg[0x4216] = reg[0x4204];
			reg[0x4217] = reg[0x4205];
		}else{
			a = reg[0x4204] | reg[0x4205] << 8;
			reg[0x4214] = a / v;
			reg[0x4215] = (a / v) >> 8;
			reg[0x4216] = a % v;
			reg[0x4217] = (a % v) >> 8;
		}
		break;
	case 0x4207:
		htime = htime & 0x100 | v;
		break;
	case 0x4208:
		htime = htime & 0xff | (v & 1) << 8;
		break;
	case 0x4209:
		vtime = vtime & 0x100 | v;
		break;
	case 0x420a:
		vtime = vtime & 0xff | (v & 1) << 8;
		break;
	case 0x420b:
		dma |= v & ~(reg[0x420c] & ~hdma >> 24);
		break;
	case 0x4210:
		return;
	case 0x4211:
		irq &= ~IRQPPU;
		return;
	case 0x4216: case 0x4217:
	case 0x4218: case 0x4219: case 0x421a: case 0x421b:
	case 0x421c: case 0x421d: case 0x421e: case 0x421f:
		return;
	}
	if((p & 0xff40) == 0x2140)
		p &= 0xff43;
	reg[p] = v;
}

u8int
memread(u32int a)
{
	u16int al;
	u8int b;
	int speed;

	al = a;
	b = (a>>16) & 0x7f;
	speed = a < 0x800000 || (reg[MEMSEL] & 1) == 0 ? 8 : 6;
	if(al < 0x8000){
		if(b < 0x40){
			if(al >= 0x6000){
				cyc += 8;
				if(hirom && nsram != 0)
					return mdr = sram[(b << 13 | al & 0x1ffff) & (nsram - 1)];
				return mdr;
			}
			return mdr = regread(al);
		}
		if(!hirom && (b & 0xf8) == 0x70 && nsram != 0){
			cyc += speed;
			return mdr = sram[a & 0x07ffff & (nsram - 1)];
		}
	}
	if(b >= 0x7e && (a & (1<<23)) == 0){
		cyc += 8;
		return mdr = mem[a - 0x7e0000];
	}
	cyc += speed;
	if(hirom)
		return mdr = prg[((b & 0x3f) % nprg) << 16 | al];
	return mdr = prg[(b%nprg) << 15 | al & 0x7fff];
}

void
memwrite(u32int a, u8int v)
{
	u16int al;
	u8int b;
	int speed;

	al = a;
	b = (a>>16) & 0x7f;
	speed = a < 0x800000 || (reg[MEMSEL] & 1) == 0 ? 8 : 6;
	if(b >= 0x7e && a < 0x800000){
		cyc += 8;
		mem[a - 0x7e0000] = v;
	}
	if(al < 0x8000){
		if(b < 0x40){
			if(al >= 0x6000){
				cyc += 8;
				if(hirom && nsram != 0){
					sram[(b << 13 | al & 0x1fff) & (nsram - 1)] = v;
					goto save;
				}
			}else
				regwrite(a, v);
			return;
		}
		if(!hirom && (b & 0xf8) == 0x70 && nsram != 0){
			sram[a & 0x07ffff & (nsram - 1)] = v;
			cyc += speed;
		save:	
			if(saveclock == 0)
				saveclock = SAVEFREQ;
			return;
		}
	}
	cyc += speed;
}

static u8int nbytes[] = {1, 2, 2, 4, 4, 4, 2, 4};
static u8int modes[] = {0x00, 0x04, 0x00, 0x50, 0xe4, 0x44, 0x00, 0x50};

static int
dmavalid(int a, int b)
{
	if(b)
		return 1;
	if((a & 0x400000) != 0)
		return 1;
	switch(a >> 8){
	case 0x21: return 0;
	case 0x42: return a != 0x420b && a != 0x420c;
	case 0x43: return 0;
	}
	return 1;
}

int
dmastep(void)
{
	int i, j, n, m, cycl;
	u32int a;
	u8int b, c, *p;
	u32int v;
	
	cycl = 0;
	for(i = 0; i < 8; i++)
		if((dma & (1<<i)) != 0)
			break;
	if(i == 8)
		return 0;
	p = reg + 0x4300 + (i << 4);
	c = *p;
	n = nbytes[c & 7];
	m = modes[c & 7];
	for(j = 0; j < n; j++){
		a = p[2] | p[3] << 8 | p[4] << 16;
		b = p[1] + (m & 3);
		if((c & 0x80) != 0){
			v = dmavalid(b, 1) ? memread(0x2100 | b) : 0;
			if(dmavalid(a, 0))
				memwrite(a, v);
		}else{
			v = dmavalid(a, 0) ? memread(a) : 0;
			if(dmavalid(b, 1))
				memwrite(0x2100 | b, v);
		}
		cycl += 8;
		m >>= 2;
		if((c & 0x08) == 0){
			if((c & 0x10) != 0){
				if(--p[2] == 0xff)
					p[3]--;
			}else{
				if(++p[2] == 0x00)
					p[3]++;
			}
		}
		if(p[5] == 0){
			p[5] = 0xff;
			p[6]--;
		}else if(--p[5] == 0 && p[6] == 0){
			dma &= ~(1<<i);
			break;
		}
	}
	return cycl;
}

static int
hdmaload(u8int *p)
{
	u32int a;

	a = p[8] | p[9] << 8 | p[4] << 16;
	p[10] = dmavalid(a, 0) ? memread(a) : 0;
	a++;
	if((p[0] & 0x40) != 0){
		p[5] = dmavalid(a, 0) ? memread(a) : 0;
		a++;
		p[6] = dmavalid(a, 0) ? memread(a) : 0;
		a++;
	}
	p[8] = a;
	p[9] = a >> 8;
	return (p[0] & 0x40) != 0 ? 24 : 8;
}

int
hdmastep(void)
{
	int i, j, cycl;
	u8int *p, *q, n, m, b, v, c;
	u32int a, br;
	
	cycl = 0;
	if(dma != 0)
		dma &= ~((hdma & 0xff00) >> 8 | (hdma & 0xff));
	if((hdma & 0xff) == 0)
		goto init;
	cycl += 18;
	for(i = 0; i < 8; i++){
		if(((hdma >> i) & (1<<24|1)) != 1)
			continue;
		p = reg + 0x4300 + (i << 4);
		c = p[0];
		if((hdma & (1<<(16+i))) != 0){
			n = nbytes[c & 7];
			m = modes[c & 7];
			if((c & 0x40) != 0){
				q = p + 5;
				br = p[7] << 16;
			}else{
				q = p + 8;
				br = p[4] << 16;
			}
			for(j = 0; j < n; j++){
				a = q[0] | q[1] << 8 | br;
				b = p[1] + (m & 3);
				if((c & 0x80) != 0){
					v = dmavalid(b, 1) ? memread(0x2100 | b) : 0;
					if(dmavalid(a, 0))
						memwrite(a, v);
				}else{
					v = dmavalid(a, 0) ? memread(a) : 0;
					if(dmavalid(b, 1))
						memwrite(0x2100 | b, v);
				}
				if(++q[0] == 0)
					q[1]++;
				cycl += 8;
				m >>= 2;
			}
		}
		p[10]--;
		hdma = (hdma & ~(1<<(16+i))) | ((p[10] & 0x80) << (9+i));
		cycl += 8;
		if((p[10] & 0x7f) == 0){
			cycl += hdmaload(p) - 8;
			if(p[10] == 0)
				hdma |= 1<<(24+i);
			hdma |= 1<<(16+i);
		}
	}
	hdma &= ~0xff;
	if((hdma & 0xff00) == 0)
		return cycl;
init:
	for(i = 0; i < 8; i++){
		if((hdma & (1<<(8+i))) == 0)
			continue;
		p = reg + 0x4300 + (i << 4);
		p[8] = p[2];
		p[9] = p[3];
		cycl += hdmaload(p);
		if(p[10] == 0)
			hdma |= 1<<(24+i);
		hdma |= 1<<(16+i);
	}
	cycl += 18;
	hdma &= ~0xff00;
	return cycl;
}

void
memreset(void)
{
	reg[0x213e] = 1;
	reg[0x213f] = 2;
	reg[0x4201] = 0xff;
	reg[0x4210] = 2;
}
