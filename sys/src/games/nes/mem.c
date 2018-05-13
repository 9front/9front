#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

uchar mem[32768];
uchar ppuram[16384];
uchar oam[256];
uchar *prgb[16], *chrb[16];
u16int pput, ppuv;
u8int ppusx, vrambuf;
int vramlatch = 1, keylatch = 0xFF, keylatch2 = 0xFF;
int prgsh, chrsh, mmc3hack;

static void
nope(int p)
{
	print("unimplemented mapper function %d (mapper %d)\n", p, map);
}

static void
nrom(int p, u8int)
{
	if(p >= 0)
		return;
	switch(p){
	case INIT:
	case RSTR:
		prgb[0] = prg;
		if(nprg == 1)
			prgb[1] = prg;
		else
			prgb[1] = prg + 0x4000;
		prgsh = 14;
		chrb[0] = chr;
		chrsh = 13;
		break;
	case SAVE:
	case SCAN:
		break;
	default:
		nope(p);
	}
}

static void
mmc1(int v, u8int p)
{
	static u8int n, s, mode, c0, c1, pr;
	static int mirrs[] = {MSINGB, MSINGA, MVERT, MHORZ};
	
	if(v < 0){
		switch(v){
		case INIT:
			if(nprg > 32)
				sysfatal("bad rom, too much prg rom for mmc1");
			mode = 0x0C;
			prgsh = 14;
			chrsh = 12;
			goto t;
		case RSTR:
			mode = get8();
			c0 = get8();
			c1 = get8();
			pr = get8();
			n = get8();
			s = get8();
			goto t;
		case SAVE:
			put8(mode);
			put8(c0);
			put8(c1);
			put8(pr);
			put8(n);
			put8(s);
			break;
		default:
			nope(v);
		case SCAN:
			break;
		}
		return;
	}
	if((p & 0x80) != 0){
		n = 0;
		s = 0;
		mode |= 0xC;
		return;
	}
	s |= (p & 1) << 4;
	if(n < 4){
		n++;
		s >>= 1;
		return;
	}
	switch(v & 0xE000){
	case 0x8000:
		mode = s;
		mirr = mirrs[mode & 3];
		break;
	case 0xA000:
		if(nprg > 16){
			pr = s & 0x10 | pr & 0x0f;
			pr %= nprg;
		}
		c0 = s & 0x1f;
		c0 %= 2*nchr;
		break;
	case 0xC000:
		c1 = s & 0x1f;
		c1 %= 2*nchr;
		break;
	case 0xE000:
		pr = pr & 0x10 | s & 0x0f;
		pr %= nprg;
		break;
	}
	s = 0;
	n = 0;
t:
	switch(mode & 0x0c){
	case 0x08:
		prgb[0] = prg;
		prgb[1] = prg + pr * 0x4000;
		break;
	case 0x0C:
		prgb[0] = prg + pr * 0x4000;
		prgb[1] = prg + ((pr & 0x10 | 0x0f) % nprg) * 0x4000;
		break;
	default:
		prgb[0] = prg + (pr & 0xfe) * 0x4000;
		prgb[1] = prg + (pr | 1) * 0x4000;
		break;
	}
	if((mode & 0x10) != 0){
		chrb[0] = chr + c0 * 0x1000;
		chrb[1] = chr + c1 * 0x1000;
	}else{
		chrb[0] = chr + (c0 & 0xfe) * 0x1000;
		chrb[1] = chr + (c0 | 1) * 0x1000;
	}
}

static void
uxrom(int p, u8int v)
{
	static u8int b;
	
	if(p < 0)
		switch(p){
		case INIT:
			prgsh = 14;
			chrsh = 13;
			prgb[1] = prg + (nprg - 1) * 0x4000;
			chrb[0] = chr;
			break;
		case SAVE:
			put8(b);
			return;
		case RSTR:
			b = get8();
			break;
		case SCAN:
			return;
		default:
			nope(p);
			return;
		}
	else
		b = v % nprg;
	prgb[0] = prg + b * 0x4000;
}

static void
cnrom(int p, u8int v)
{
	static u8int b;
	
	if(p < 0)
		switch(p){
		case INIT:
			prgsh = 14;
			chrsh = 13;
			prgb[0] = prg;
			if(nprg == 1)
				prgb[1] = prg;
			else
				prgb[1] = prg + 0x4000;
			break;
		case SAVE:
			put8(b);
			return;
		case RSTR:
			b = get8();
			break;
		case SCAN:
			return;
		default:
			nope(p);
			return;
		}
	else
		b = v % nchr;
	chrb[0] = chr + b * 0x2000;

}

static void
mmc3(int p, u8int v)
{
	static u8int m, b[8], l, n, en;
	int i, j, c;

	if(p < 0){
		switch(p){
		case INIT:
			prgsh = 13;
			chrsh = 10;
			mmc3hack = 1;
			prgb[2] = prg + (2 * nprg - 2) * 0x2000;
			prgb[3] = prgb[2] + 0x2000;
			goto t;
		case SCAN:
			if(n == 0)
				n = l;
			else
				n--;
			if(n == 0 && en)
				irq |= IRQMMC;
			return;
		case SAVE:
			put8(m);
			for(i = 0; i < 8; i++)
				put8(b[i]);
			put8(l);
			put8(n);
			put8(en);
			return;
		case RSTR:
			m = get8();
			for(i = 0; i < 8; i++)
				b[i] = get8();
			l = get8();
			n = get8();
			en = get8();
			goto t;
		}
	}
	switch(p & 0xE001){
	case 0x8000:
		if(((m ^ v) & 0xc0) != 0){
			m = v;
			goto t;
		}
		m = v;
		break;
	case 0x8001:
		i = m & 7;
		if(i < 6)
			v %= 8 * nchr;
		else
			v %= 2 * nprg;
		b[i] = v;
		goto t;
	case 0xA000:
		if(mirr == MFOUR)
			break;
		if(v & 1)
			mirr = MHORZ;
		else
			mirr = MVERT;
		break;
	case 0xC000: l = v; break;
	case 0xC001: n = 0; break;
	case 0xE000: en = 0; irq &= ~IRQMMC; break;
	case 0xE001: en = 1; break;
	}
	return;
t:
	if((m & 0x40) != 0){
		prgb[0] = prg + (2 * nprg - 2) * 0x2000;
		prgb[2] = prg + b[6] * 0x2000;
	}else{
		prgb[0] = prg + b[6] * 0x2000;
		prgb[2] = prg + (2 * nprg - 2) * 0x2000;
	}
	prgb[1] = prg + b[7] * 0x2000;
	c = (m & 0x80) >> 5;
	for(i = 0; i < 2; i++){
		chrb[j = (i << 1) ^ c] = chr + (b[i] >> 1) * 0x800;
		chrb[j+1] = chrb[j] + 0x400;
	}
	for(i = 2; i < 6; i++)
		chrb[(i + 2) ^ c] = chr + b[i] * 0x400;
}

static void
axrom(int p, u8int v)
{
	static int b;

	if(p >= 0)
		b = v;
	else
		switch(p){
		case INIT:
			nrom(INIT, 0);
			b = 0;
			break;
		case SAVE:
			put8(b);
			return;
		case RSTR:
			b = get8();
			break;
		case SCAN:
			return;
		default:
			nope(p);
			return;
		}
	prgb[0] = prg + (b & 3) * 0x8000;
	prgb[1] = prgb[0] + 0x4000;
}

void (*mapper[256])(int, u8int) = {
	[0] nrom,
	[1] mmc1,
	[2] uxrom,
	[3] cnrom,
	[4] mmc3,
	[7] axrom,
};

static void
incvram(void)
{
	int old;
	
	old = ppuv;
	if((mem[PPUCTRL] & VRAMINC) != 0)
		ppuv += 32;
	else
		ppuv += 1;
	ppuv &= 0x3FFF;
	if(mmc3hack && (old & (1<<12)) == 0 && (ppuv & (1<<12)) != 0)
		mapper[map](SCAN, 0);
}

u8int
memread(u16int p)
{
	u8int v;
	int i;

	if(p < 0x2000){
		p &= 0x7FF;
	}else if(p < 0x6000){
		if(p < 0x4000)
			p &= 0x2007;
		switch(p){
		case 0x2002:
			v = mem[p];
			mem[p] &= ~PPUVBLANK;
			vramlatch = 1;
			return v;
		case 0x2004:
			return oam[mem[0x2003]];
		case 0x2007:
			if(ppuv < 0x4000){
				v = vrambuf;
				vrambuf = ppuread(ppuv);
				incvram();
				return v;
			}
			vrambuf = ppuread(ppuv);
			incvram();
			return vrambuf;
		case APUSTATUS:
			v = (irq & 3) << 6;
			for(i = 0; i < 4; i++){
				if(apuctr[i] != 0)
					v |= (1<<i);
			}
			if(mem[0x4013] != 0)
				v |= (1<<4);
			irq &= ~IRQFRAME;
			return v;
		case 0x4016:
			if((mem[p] & 1) != 0)
				return keys & 1;
			v = keylatch & 1;
			keylatch = (keylatch >> 1) | 0x80;
			return v | 0x40;
		case 0x4017:
			if((mem[p] & 1) != 0)
				return keys2 & 1;
			v = keylatch2 & 1;
			keylatch2 = (keylatch2 >> 1) | 0x80;
			return v | 0x40;
		}
	}
	if(p >= 0x8000){
		p -= 0x8000;
		return prgb[p >> prgsh][p & ((1 << prgsh) - 1)];
	}
	return mem[p];
}

void
memwrite(u16int p, u8int v)
{
	extern u8int apulen[32];
	extern u16int dmclen[16];
	int i;

	if(p < 0x2000){
		p &= 0x7FF;
	}else if(p < 0x6000){
		if(p < 0x4000)
			p &= 0x2007;
		switch(p){
		case PPUCTRL:
			if((mem[PPUCTRL] & PPUNMI) == 0 && (v & PPUNMI) != 0 &&
			   (mem[PPUSTATUS] & PPUVBLANK) != 0)
				nmi = 1;
			pput = (pput & 0xF3FF) | ((v & 3) << 10);
			break;
		case PPUSTATUS:
			return;
		case 0x2004:
			oam[mem[0x2003]++] = v;
			return;
		case 0x2005:
			if(vramlatch){
				ppusx = v & 7;
				pput = (pput & 0xFFE0) | (v >> 3);
			}else
				pput = (pput & 0x0C1F) | ((v & 0xF8) << 2) | ((v & 7) << 12);
			vramlatch ^= 1;
			return;
		case 0x2006:
			if(vramlatch)
				pput = (pput & 0xFF) | (v << 8) & 0x3F00;
			else{
				pput = (pput & 0xFF00) | v;
				if(mmc3hack && (ppuv & (1<<12)) == 0 && (pput & (1<<12)) != 0)
					mapper[map](SCAN, 0);
				ppuv = pput;
			}
			vramlatch ^= 1;
			return;
		case 0x2007:
			ppuwrite(ppuv, v);
			incvram();
			return;
		case 0x4001:
		case 0x4005:
			i = (p & 0xC) >> 2;
			apuctr[i+8] |= 0x80;
			break;
		case 0x4003:
		case 0x4007:
		case 0x400B:
		case 0x400F:
			i = (p & 0xC) >> 2;
			if((mem[APUSTATUS] & (1<<i)) != 0){
				apuctr[i] = apulen[v >> 3];
				apuctr[10] |= (1<<i);
			}
			break;
		case DMCCTRL:
			if((v & 0x80) == 0)
				irq &= ~IRQDMC;
			dmcfreq = 12 * dmclen[v & 0xf];
			break;
		case DMCBUF:
			v &= ~0x80;
			break;
		case 0x4014:
			memcpy(oam, mem + (v<<8), sizeof(oam));
			return;
		case APUSTATUS:
			for(i = 0; i < 4; i++)
				if((v & (1<<i)) == 0)
					apuctr[i] = 0;
			if((v & 0x10) != 0 && dmccnt == 0){
				dmcaddr = mem[DMCADDR] * 0x40 + 0xC000;
				dmccnt = mem[DMCLEN] * 0x10 + 1;
			}
			irq &= ~IRQDMC;
			break;
		case 0x4016:
			if((mem[p] & 1) != 0 && (v & 1) == 0){
				keylatch = keys;
				keylatch2 = keys2;
			}
			break;
		case APUFRAME:
			apuseq = 0;
			if((v & 0x80) != 0)
				apuclock = APUDIV;
			else
				apuclock = 0;
			if((v & 0x40) != 0)
				irq &= ~IRQFRAME;
			break;
		}
	}else if(p < 0x8000){
		if(saveclock == 0)
			saveclock = SAVEFREQ;
	}else{
		if(mapper[map] != nil)
			mapper[map](p, v);
		return;
	}
	mem[p] = v;
}

static uchar *
ppumap(u16int p)
{
	if(p >= 0x3F00){
		if((p & 3) == 0)
			p &= 0x3F0F;
		return ppuram + (p & 0x3F1F);
	}
	p &= 0x3FFF;
	if(p >= 0x3000)
		p &= 0x2FFF;
	if(p >= 0x2000)
		switch(mirr){
		case MHORZ: if((p & 0x800) != 0) p |= 0x400; else p &= ~0x400; break;
		case MVERT: if((p & 0x400) != 0) p |= 0x800; else p &= ~0x800; break;
		case MSINGA: p &= ~0xC00; break;
		case MSINGB: p |= 0xC00; break;
		}
	if(p < 0x2000)
		return chrb[p >> chrsh] + (p & ((1 << chrsh) - 1));
	else
		return ppuram + p;
}

u8int
ppuread(u16int p)
{
	return *ppumap(p);
}

void
ppuwrite(u16int p, u8int v)
{
	*ppumap(p) = v;
}

