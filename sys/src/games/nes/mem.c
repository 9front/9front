#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

uchar mem[32768];
uchar ppuram[16384];
uchar oam[256];
uchar *prgb[2], *chrb[2];
u16int pput, ppuv;
u8int ppusx;
static int vramlatch = 1, keylatch = 0xFF;

static void
nrom(int p, u8int)
{
	if(p < 0){
		prgb[0] = prg;
		if(nprg == 1)
			prgb[1] = prg;
		else
			prgb[1] = prg + 0x4000;
		chrb[0] = chr;
		chrb[1] = chr + 0x1000;
	}
	return;
}

static void
mmc1(int v, u8int p)
{
	static u8int n, s, mode, c0, c1, pr;
	int wchr, wprg;
	static int mirrs[] = {MSINGB, MSINGA, MVERT, MHORZ};
	
	if(v < 0){
		wchr = 1;
		wprg = 1;
		mode = 0x0C;
		goto t;
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
	wchr = wprg = 1;
	switch(v & 0xE000){
	case 0x8000:
		mode = s;
		mirr = mirrs[mode & 3];
		wchr = wprg = 1;
		break;
	case 0xA000:
		c0 = s & 0x1f;
		c0 %= 2*nchr;
		wchr = 1;
		break;
	case 0xC000:
		c1 = s & 0x1f;
		c1 %= 2*nchr;
		if((mode & 0x10) != 0)
			wchr = 1;
		break;
	case 0xE000:
		pr = s & 0x0f;
		pr %= nprg;
		wprg = 1;
		break;
	}
t:
	if(wprg)
		switch(mode & 0x0c){
		case 0x08:
			prgb[0] = prg;
			prgb[1] = prg + pr * 0x4000;
			break;
		case 0x0C:
			prgb[0] = prg + pr * 0x4000;
			prgb[1] = prg + (0x0f % nprg) * 0x4000;
			break;
		default:
			prgb[0] = prg + (pr & 0xfe) * 0x4000;
			prgb[1] = prg + (pr | 1) * 0x4000;
			break;
		}
	if(wchr)
		if((mode & 0x10) != 0){
			chrb[0] = chr + c0 * 0x1000;
			chrb[1] = chr + c1 * 0x1000;
		}else{
			chrb[0] = chr + (c0 & 0xfe) * 0x1000;
			chrb[1] = chr + (c0 | 1) * 0x1000;
		}
	s = 0;
	n = 0;
}

static void
mmc7(int v, u8int p)
{
	if(v < 0){
		nrom(-1, 0);
		p = 0;
	}
	prgb[0] = prg + (p & 3) * 0x8000;
	prgb[1] = prgb[0] + 0x4000;
}

void (*mapper[256])(int, u8int) = {
	[0] nrom,
	[1] mmc1,
	[7] mmc7,
};

static void
incvram(void)
{
	if((mem[PPUCTRL] & VRAMINC) != 0)
		ppuv += 32;
	else
		ppuv += 1;
	ppuv &= 0x3FFF;
}

u8int
memread(u16int p)
{
	static u8int vrambuf;
	u8int v;

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
		case 0x4016:
			if((mem[p] & 1) != 0)
				return keys & 1;
			v = keylatch & 1;
			keylatch = (keylatch >> 1) | 0x80;
			return v | 0x40;
		case 0x4017:
			return 0x40;
		}
	}
	if(p >= 0x8000){
		if((p & 0x4000) != 0)
			return prgb[1][p - 0xC000];
		else
			return prgb[0][p - 0x8000];
	}
	return mem[p];
}

void
memwrite(u16int p, u8int v)
{
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
				ppuv = pput;
			}
			vramlatch ^= 1;
			return;
		case 0x2007:
			ppuwrite(ppuv, v);
			incvram();
			return;
		case 0x4014:
			memcpy(oam, mem + (v<<8), sizeof(oam));
			return;
		case 0x4016:
			if((mem[p] & 1) != 0 && (v & 1) == 0)
				keylatch = keys;
			break;
		}
	}else if(p >= 0x8000){
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
	if(p < 0x1000)
		return chrb[0] + p;
	else if(p < 0x2000)
		return chrb[1] + p - 0x1000;
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

