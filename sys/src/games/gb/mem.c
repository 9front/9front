#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

uchar mem[65536];
int rombank, rambank, ramen, battery, ramrom;
extern int savefd;

u8int
memread(u16int p)
{
	extern int keys;

	if((p & 0xFF80) == 0xFF00)
		switch(p){
		case 0xFF00:
			if((mem[0xFF00] & (1<<5)) == 0)
				return (mem[0xFF00] & 0xF0) | ~(keys >> 4);
			if((mem[0xFF00] & (1<<6)) == 0)
				return (mem[0xFF00] & 0xF0) | ~(keys & 0x0F);
			return (mem[0xFF00] & 0xF0) | 0x0F;
		}
	if(!ramen && ((p & 0xE000) == 0xA000))
		return 0xFF;
	return mem[p];
}

static void
ramswitch(int state, int bank)
{
	if(ramen){
		memcpy(ram + 8192 * rambank, mem + 0xA000, 8192);
		if(battery && savefd > 0){
			seek(savefd, rambank * 8192, 0);
			write(savefd, ram + 8192 * rambank, 8192);
		}
		ramen = 0;
	}
	rambank = bank;
	if(state){
		if(bank >= rambanks)
			sysfatal("invalid RAM bank %d selected (pc = %.4x)", bank, curpc);
		memcpy(mem + 0xA000, ram + 8192 * rambank, 8192);
		ramen = 1;
	}
}

void
flushram(void)
{
	if(ramen)
		ramswitch(ramen, rambank);
}

static void
romswitch(int bank)
{
	if(bank >= rombanks)
		sysfatal("invalid ROM bank %d selected (pc = %.4x)", bank, curpc);
	rombank = bank;
	memcpy(mem + 0x4000, cart + 0x4000 * bank, 0x4000);
}

void
memwrite(u16int p, u8int v)
{
	if(p < 0x8000){
		switch(mbc){
		case 0:
			return;
		case 1:
		case 2:
			switch(p >> 13){
			case 0:
				if((v & 0x0F) == 0x0A)
					ramswitch(1, rambank);
				else
					ramswitch(0, rambank);
				return;
			case 1:
				v &= 0x1F;
				if(v == 0)
					v++;
				romswitch((rombank & 0xE0) | v);
				return;
			case 2:
				if(ramrom)
					ramswitch(ramen, v & 3);
				else
					romswitch(((v & 3) << 5) | (rombank & 0x1F));
				return;
			case 3:
				ramrom = v;
				return;
			}
			return;
		case 3:
			switch(p >> 13){
			case 0:
				if((v & 0x0F) == 0x0A)
					ramswitch(1, rambank);
				else
					ramswitch(0, rambank);
				return;
			case 1:
				v &= 0x7F;
				if(v == 0)
					v++;
				romswitch(v);
				return;
			case 2:
				if(v < 4)
					ramswitch(ramen, v);
				return;
			}
			return;
		case 5:
			switch(p >> 13){
			case 0:
				if((v & 0x0F) == 0x0A)
					ramswitch(1, rambank);
				else
					ramswitch(0, rambank);
				return;
			case 1:
				romswitch((rombank & 0x100) | v);
				return;
			case 2:
				romswitch((((int)v & 1) << 8) | (rombank & 0xFF));
				return;
			case 3:
				ramswitch(ramen, v & 15);
				return;
			
			}
			return;
		default:
			sysfatal("mbc %d unimplemented", mbc);
		}
	}
	if((p & 0xFF80) == 0xFF00)
		switch(p){
		case 0xFF04:
			v = 0;
			break;
		case 0xFF07:
			timer = (v & 4) != 0;
			switch(v & 3){
			case 0:
				timerfreq = 1024;
				break;
			case 1:
				timerfreq = 16;
				break;
			case 2:
				timerfreq = 64;
				break;
			default:
				timerfreq = 256;
			}
			break;
		case 0xFF41:
			v &= ~7;
			v |= mem[p] & 7;
			break;
		case 0xFF46:
			memcpy(mem + 0xFE00, mem + (((int)v) << 8), 0xA0);
			break;
		}
	mem[p] = v;
}
