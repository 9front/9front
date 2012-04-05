#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

uchar mem[65536];
int bank;

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
	return mem[p];
}

void
memwrite(u16int p, u8int v)
{
	if(p < 0x8000){
		switch(mbc){
		case 0:
			return;
		case 1:
			switch(p >> 13){
			case 1:
				if(v == 0)
					v++;
				bank = v;
				if(bank >= rombanks)
					sysfatal("invalid ROM bank %d selected (pc = %.4x)", bank, curpc);
				memcpy(mem + 0x4000, cart + 0x4000 * bank, 0x4000);
				return;
			
			}
			return;
		case 3:
			switch(p >> 13){
			case 1:
				bank = v;
				if(bank >= rombanks)
					sysfatal("invalid ROM bank %d selected (pc = %.4x)", bank, curpc);
				memcpy(mem + 0x4000, cart + 0x4000 * bank, 0x4000);
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
