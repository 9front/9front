#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

int region, picidx;
u16int ppux, ppuy, lastx, wrapx, maxy, lvis, rvis, uvis, dvis, picw, pich, lbord, rbord, ubord, dbord, spr0;
u16int vc, vcbase, vmli;
u8int badln, rc, displ, fract, visreg, hbord, vbord, rbord0, lbord0;
u16int chrp[40];
u64int pxs, npxs, npxs0, opxs;
u8int fg;

typedef struct spr spr;
enum {
	SPRFDMA = 1,
	SPRFYEX = 2,
	SPRFDISP = 4,
};
struct spr {
	u8int flags;
	u32int data;
	u8int mc, mcbase, dp;
	u16int x;
} sp[8];

void
bordset(void)
{
	int r, c;

	r = (reg[CTRL1] & RSEL) != 0;
	c = (reg[CTRL2] & CSEL) != 0;
	lbord = c ? 0x14 : 0x1c;
	lbord0 = c ? 0xf0 : 0xe0;
	rbord = c ? 0x154 : 0x14c;
	rbord0 = c ? 0x0f : 0x3f;
	ubord = r ? 0x33 : 0x37;
	dbord = r ? 0xfb : 0xf7;
	if((reg[CTRL1] & DEN) == 0)
		ubord = -1;
}

void
vicreset(void)
{
	switch(region){
	case NTSC0:
		lastx = 0x1fc;
		wrapx = 0x19c;
		maxy = 262;
		picw = 412;
		pich = 234;
		spr0 = 0x16c;
		lvis = 0x1e4;
		rvis = 0x184;
		uvis = 41;
		dvis = 13;
		break;
	case NTSC:
		lastx = 0x1fc;
		wrapx = 0x19c;
		maxy = 263;
		picw = 419;
		pich = 235;
		spr0 = 0x174;
		lvis = 0x1e4;
		rvis = 0x18c;
		uvis = 41;
		dvis = 13;
		break;
	case PAL:
		lastx = 0x1f4;
		wrapx = 0x194;
		maxy = 312;
		picw = 404;
		pich = 284;
		spr0 = 0x164;
		lvis = 0x1dc;
		rvis = 0x17c;
		uvis = 16;
		dvis = 300;
		break;
	}
	ppux = 4;
	ppuy = 0;
	bordset();
}

void
pixeldraw(u64int p, int n)
{
	int i;
	union { u8int c[4]; u32int l; } u;
	static u8int cr[] = {0, 255, 136, 170, 204, 0, 0, 238, 221, 102, 255, 51, 119, 170, 0, 187};
	static u8int cg[] = {0, 255, 0, 255, 68, 204, 0, 238, 136, 68, 119, 51, 119, 255, 136, 187};
	static u8int cb[] = {0, 255, 0, 238, 204, 85, 170, 119, 85, 0, 119, 51, 119, 102, 255, 187};
	u8int c;
	u32int *q;

	q = (u32int *)pic + picidx * scale;
	for(i = 0; i < n; i++){
		c = p >> 56;
		p <<= 8;
		u.c[0] = cb[c];
		u.c[1] = cg[c];
		u.c[2] = cr[c];
		u.c[3] = 0;
		switch(scale){
		case 16: *q++ = u.l;
		case 15: *q++ = u.l;
		case 14: *q++ = u.l;
		case 13: *q++ = u.l;
		case 12: *q++ = u.l;
		case 11: *q++ = u.l;
		case 10: *q++ = u.l;
		case 9: *q++ = u.l;
		case 8: *q++ = u.l;
		case 7: *q++ = u.l;
		case 6: *q++ = u.l;
		case 5: *q++ = u.l;
		case 4: *q++ = u.l;
		case 3: *q++ = u.l;
		case 2: *q++ = u.l;
		default: *q++ = u.l;
		}
	}
	picidx += n;
}

void
pixels(u8int d, u16int c)
{
	u8int c0, c1, c2, n;
	int i;

	npxs0 = npxs;
	npxs = 0;
	switch((reg[CTRL1] & (ECM|BMM) | reg[CTRL2] & MCM) >> 4){
	case 0:
		c0 = c >> 8;
	normal:
		fg = d;
		for(i = 0; i < 8; i++){
			npxs = npxs << 8 | ((d & 0x80) != 0 ? c0 : reg[BG0]);
			d <<= 1;
		}
		break;
	case 1:
		c0 = c >> 8 & 7;
		if((c & 0x800) == 0)
			goto normal;
		fg = d & 0xaa | d >> 1 & 0x55;
		for(i = 0; i < 8; i += 2){
			n = d >> 6;
			npxs = npxs << 16 | (n == 3 ? c0 : reg[BG0 + n]) * 0x101;
			d <<= 2;
		}
		break;
	case 2:
		c0 = c & 15;
		c1 = c >> 4 & 15;
		fg = d;
		for(i = 0; i < 8; i++){
			npxs = npxs << 8 | ((d & 0x80) != 0 ? c1 : c0);
			d <<= 1;
		}
		break;
	case 3:
		c0 = c & 15;
		c1 = c >> 4 & 15;
		c2 = c >> 8;
		fg = d & 0xaa | d >> 1 & 0x55;
		for(i = 0; i < 8; i += 2){
			n = d >> 6;
			switch(n){
			default: n = reg[BG0]; break;
			case 1: n = c1; break;
			case 2: n = c0; break;
			case 3: n = c2;
			}
			npxs = npxs << 16 | n * 0x101;
			d <<= 2;
		}
		break;
	case 4:
		c0 = c >> 8;
		fg = d;
		for(i = 0; i < 8; i++){
			npxs = npxs << 8 | ((d & 0x80) != 0 ? c0 : reg[BG0 + (d >> 6 & 3)]);
			d <<= 1;
		}
		break;
	default:
		fg = 0;
		break;
	}
}

void
bgpixels(void)
{
	int i, j, x;
	u8int h;
	u8int spract, s, o;
	
	h = hbord;
	opxs = 0;
	for(i = 0; i < 8; i++){
		if((reg[CTRL2] + 4 & 7) == i)
			pxs = i >= 4 ? npxs : npxs0;
		o = pxs >> 56;
		pxs <<= 8;

		x = ppux + i;
		spract = 0;
		for(j = 0; j < 8; j++)
			if((sp[j].flags & SPRFDISP) != 0 && (u16int)(x-sp[j].x) < 48 && (sp[j].data & ((reg[SPRMC]&1<<j)?3:2)<<22) != 0)
				spract |= 1<<j;
		if((spract & spract - 1) != 0 && vbord == 0){
			reg[SPRSPR] |= spract;
			irq |= IRQSPRCOLL;
		}
		if(fg != 0 && spract != 0 && vbord == 0){
			reg[SPRBG] |= spract;
			irq |= IRQBGCOLL;
		}
		s = spract & ~(reg[SPRDP] & ((s8int)fg) >> 7);
		if(s != 0)
			for(j = 0; j < 8; j++)
				if((s & 1<<j) != 0){
					if((reg[SPRMC] & 1<<j) != 0)
						switch(sp[j].data >> 22 & 3){
						case 1: o = reg[SPRMC0]; break;
						case 2: o = reg[SPRCOL+j]; break;
						case 3: o = reg[SPRMC1]; break;
						}
					else
						o = reg[SPRCOL+j];
					break;
				}
		if((h & 0x80) != 0)
			o = reg[EC];
		opxs = opxs << 8 | o;
		h <<= 1;
		fg <<= 1;
		for(j = 0; j < 8; j++)
			if((u16int)(x-sp[j].x) < 48 && ((reg[SPRXE] & 1<<j) == 0 || (x-sp[j].x & 1) != 0))
				if((reg[SPRMC] & 1<<j) != 0){
					if((x-sp[j].x & 1) != 0)
						sp[j].data <<= 2;
				}else
					sp[j].data <<= 1;
	}
}

void
vicstep(void)
{
	u16int gaddr;
	int i;

	if(ppuy == 0x30 && (reg[CTRL1] & DEN) != 0)
		fract = 1;
	badln = ((ppuy ^ reg[CTRL1]) & 7) == 0 && fract;
	hbord = ((s8int)(hbord << 7)) >> 7;
	if(ppux == rbord && hbord == 0)
		hbord = rbord0;
	else if(ppux == lbord){
		if(ppuy == dbord)
			vbord = 1;
		if(ppuy == ubord)
			vbord = 0;
		if(!vbord)
			hbord = lbord0;
	}
	if(badln)
		displ = 1;
	if(ppux == 4){
		vc = vcbase;
		vmli = 0;
		if(badln)
			rc = 0;
	}
	if(ppux == 12)
		for(i = 0; i < 8; i++){
			if((sp[i].flags & SPRFDISP) == 0 || (reg[SPRYE] & 1<<i) != 0 && (sp[i].flags & SPRFYEX) == 0)
				continue;
			sp[i].mcbase += 3;
			if(sp[i].mcbase == 63)
				sp[i].flags &= ~(SPRFDMA|SPRFDISP);
		}
	if(ppux >= 0x14 && ppux <= 0x14c){
		if((reg[CTRL1] & BMM) != 0)
			gaddr = (reg[MEMP] & 0x08) << 10 | vc << 3 | rc;
		else
			gaddr = (reg[MEMP] & 0x0e) << 10 | (chrp[vmli] & 0xff) << 3 | rc;
		if(!displ)
			gaddr = 0x3fff;
		if((reg[CTRL1] & ECM) != 0)
			gaddr &= ~0x600;
		pixels(vmemread(gaddr) & -displ, chrp[vmli] & -displ);
		vmli++;
		vc = vc + 1 & 0x3ff;
	}
	if(visreg && (ppux >= lvis || ppux < rvis)){
		bgpixels();
		pixeldraw(opxs, ppux == lvis ? region == NTSC ? 3 : 4 : 8);
	}
	if(ppux == 0x14c){
		for(i = 0; i < 8; i++){
			if((reg[SPRYE] & 1<<i) != 0)
				sp[i].flags ^= SPRFYEX;
			if((reg[SPREN] & 1<<i) == 0 || reg[2*i+1] != (u8int)ppuy)
				continue;
			sp[i].flags |= SPRFDMA;
			sp[i].mcbase = 0;
			if((reg[SPRYE] & 1<<i) != 0)
				sp[i].flags &= ~SPRFYEX;		
		}
	}else if(ppux == 0x154)
		npxs = reg[BG0] * 0x0101010101010101ULL;
	if(badln){
		if(ppux == lastx - 8)
			nrdy = 1;
		else if(ppux >= 0xc && ppux <= 0x144)
			chrp[vmli] = vmemread(vc | (reg[MEMP] & 0xf0) << 6) | cram[vc] << 8;
		else if(ppux == 0x154)
			nrdy = 0;
	}
	if(ppux == 0x164){
		if(displ && rc == 7){
			displ = badln;
			vcbase = vc;
		}
		if(displ)
			rc = rc + 1 & 7;
		for(i = 0; i < 8; i++){
			sp[i].mc = sp[i].mcbase;
			if((sp[i].flags & SPRFDMA) != 0 && reg[2*i+1] == (u8int)ppuy)
				sp[i].flags |= SPRFDISP;
		}
	}
	if((u16int)(ppux - spr0) < 128){
		i = ppux - spr0 >> 4;
		nrdy = (sp[i].flags & SPRFDMA) != 0;
		if((ppux & 8) == 0){
			sp[i].dp = vmemread((reg[MEMP] & 0xf0) << 6 | 0x3f8 | i);
			sp[i].x = nrdy ? reg[2 * i] | reg[MSBX] << 8 - i & 0x100 : -1;
			if(nrdy)
				sp[i].data = vmemread(sp[i].dp << 6 | sp[i].mc++) << 16;
		}else if(nrdy){
			sp[i].data = sp[i].data & 0xff00ff | vmemread(sp[i].dp << 6 | sp[i].mc++) << 8;
			sp[i].data = sp[i].data & 0xffff00 | vmemread(sp[i].dp << 6 | sp[i].mc++);
		}
	}else if(ppux - spr0 == 128)
		nrdy = 0;
	if(ppux == wrapx){
		ppuy++;
		if(ppuy == maxy){
			flush();
			ppuy = 0;
			vcbase = 0;
		}
		if((ppuy & 0xff) == reg[RASTER] && ((ppuy ^ reg[CTRL1] << 1) & 0x100) == 0)
			irq |= IRQRASTER;
		if(ppuy == dbord)
			vbord = 1;
		else if(ppuy == ubord)
			vbord = 0;
		else if(ppuy == dvis)
			visreg = 0;
		if(ppuy == uvis){
			picidx = 0;
			visreg = 1;
		}
		if(ppuy == 0xf7)
			fract = 0;
	}
	if(ppux == lastx)
		ppux = 4;
	else
		ppux += 8;
}
