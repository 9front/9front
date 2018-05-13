#include <u.h>
#include <libc.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

u8int ram[128], reg[64];
static u8int timer, timerun, timerspeed;
static u16int timerpre;
static u8int grp0d, grp1d, enabld;

static u8int
tiaread(u8int a)
{
	if(a < 8)
		return coll >> (a << 1 & 14) << 6;
	if(a == 0xc)
		return ~keys << 3 & 0x80;
	return 0x80;
}

static void
tiawrite(u8int a, u8int v)
{
	switch(a){
	case VSYNC:
		if((v & 2) != 0)
			flush();
		return;
	case VBLANK:
		if((v & 2) == 0)
			ppuy = 0;
		break;
	case WSYNC: nrdy = 1; break;
	case RESP0: p0x = ppux >= 160 ? 3 : ppux+5; break;
	case RESP1: p1x = ppux >= 160 ? 3 : ppux+5; break;
	case RESM0: m0x = ppux >= 160 ? 2 : ppux+4; break;
	case RESM1: m1x = ppux >= 160 ? 2 : ppux+4; break;
	case RESBL: blx = ppux >= 160 ? 2 : ppux+4; break;
	case HMOVE:
		p0x = (p0x - ((s8int) reg[HMP0] >> 4)) % 160;
		p1x = (p1x - ((s8int) reg[HMP1] >> 4)) % 160;
		m0x = (m0x - ((s8int) reg[HMM0] >> 4)) % 160;
		m1x = (m1x - ((s8int) reg[HMM1] >> 4)) % 160;
		blx = (blx - ((s8int) reg[HMBL] >> 4)) % 160;
		break;
	case HMCLR: reg[HMP0] = reg[HMP1] = reg[HMM0] = reg[HMM1] = reg[HMBL] = 0; break;
	case VDELP0:
		if((v & 1) == 0)
			reg[GRP0] = grp0d;
		break;
	case VDELP1:
		if((v & 1) == 0)
			reg[GRP1] = grp1d;
		break;
	case VDELBL:
		if((v & 1) == 0)
			reg[ENABL] = enabld;
		break;
	case GRP0:
		if((reg[VDELP1] & 1) != 0)
			reg[GRP1] = grp1d;
		if((reg[VDELP0] & 1) != 0){
			grp0d = v;
			return;
		}
		break;
	case GRP1:
		if((reg[VDELP0] & 1) != 0)
			reg[GRP0] = grp0d;
		if((reg[VDELBL] & 1) != 0)
			reg[ENABL] = enabld;
		if((reg[VDELP1] & 1) != 0){
			grp1d = v;
			return;
		}
		break;
	case ENABL:
		if((reg[VDELBL] & 1) != 0){
			enabld = v;
			return;
		}
		break;
	case CXCLR:
		coll = 0;
		break;
	}
	reg[a] = v;
}

static u8int
ioread(u8int a)
{
	u8int v;

	switch(a){
	case 0:
		return ~(keys << 4);
	case 2:
		return keys >> 5 ^ 3 | bwmod | p0difc;
	case 4:
		timerspeed = 0;
		return timer;
	case 5:
		v = timerun;
		timerun &= ~(1<<6);
		return v;
	}
	return 0;
}

static void
iowrite(u8int a, u8int v)
{
	switch(a){
	case 4:
		timerpre = 1;
		goto timer;
	case 5:
		timerpre = 8;
		goto timer;
	case 6:
		timerpre = 64;
		goto timer;
	case 7:
		timerpre = 1024;
	timer:
		timerun &= ~(1<<7);
		timerspeed = v == 0;
		timer = v - 1;
		break;
	}
}

u8int
memread(u16int a)
{
	u8int v;

	if((a & 0x1000) != 0)
		v = rop[a & mask];
	else if((a & 1<<7) == 0)
		v = tiaread(a & 0xf);
	else if((a & 1<<9) == 0)
		v = ram[a & 0x7f];
	else
		v = ioread(a & 7);
	if(a > 0xfff3 && a < 0xfffc)
		rop = rom + bnk[a - 0xfff4];
	io();
	return v;
}

void
memwrite(u16int a, u8int v)
{
	if((a & 0x1000) != 0){
		;}
	else if((a & 1<<7) == 0)
		tiawrite(a & 0x3f, v);
	else if((a & 1<<9) == 0)
		ram[a & 0x7f] = v;
	else
		iowrite(a & 7, v);
	if(a > 0xfff3 && a < 0xfffc)
		rop = rom + bnk[a - 0xfff4];
	io();
}

static void
timerstep(void)
{
	static int cl;
	
	cl++;
	if((timerspeed || (cl & timerpre - 1) == 0) && timer-- == 0){
		timerspeed = 1;
		timerun |= 3<<6;
	}
}

void
io(void)
{
	static int snddiv;

	timerstep();
	tiastep();
	tiastep();
	tiastep();
	if(++snddiv == SAMPDIV){
		snddiv = 0;
		sample();
	}
}
