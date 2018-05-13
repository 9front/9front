#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

u8int pla;
u8int ram[65536], krom[8192], brom[8192], crom[4096], cart[16384], cram[1024];
u8int reg[47];
u16int vicbank;
u8int cia[32];
u16int timer[4], timrel[4];

enum {
	TIMEREN = 1,
	TIMERUND = 2,
	TIMERSTOP = 8,
	TIMERASRC = 0x20,
	TIMERBSRC = 0x60,
	TIMERBSYS = 0,
	TIMERBA = 0x40,
};

u8int 
ciaread(int n, u8int a)
{
	u8int r;
	int i;

	switch(a){
	case 0:
		return (cia[0] | ~cia[2]) & (~joys >> 5 | 0xe0);
	case 1:
		if(!n){
			r = 0;
			for(i = 0; i < 8; i++)
				if((cia[0] & 1<<i) == 0)
					r |= keys >> 8 * i;
			return (cia[1] | ~cia[3]) & ~r & (~joys | 0xe0);
		}
		break;
	case 4: return timer[n*2];
	case 5: return timer[n*2] >> 8;
	case 6: return timer[n*2+1];
	case 7: return timer[n*2+1] >> 8;
	case 13:
		if(n){
			r = nmi >> 4 & 0x1f | ((nmi & nmien & 0x1f0) != 0) << 7;
			nmi &= ~0x1f0;
			return r;
		}else{
			r = irq >> 4 & 0x1f | ((irq & irqen & 0x1f0) != 0) << 7;
			irq &= ~0x1f0;
			return r;
		}
	}
	return cia[n * 16 + a];
}

void
ciawrite(int n, u8int a, u8int v)
{
	switch(a){
	case 0:
		if(n)
			vicbank = (~v & 3) << 14;
		break;
	case 4: timrel[n*2] = v | timrel[n*2] & 0xff00; break;
	case 5: timrel[n*2] = v << 8 | timrel[n*2] & 0xff; break;
	case 6: timrel[n*2+1] = v | timrel[n*2+1] & 0xff00; break;
	case 7: timrel[n*2+1] = v << 8 | timrel[n*2+1] & 0xff; break;
	case 13:
		if(n)
			if((v & 0x80) != 0)
				nmien |= v << 4 & 0x1f0;
			else
				nmien &= ~(v << 4 & 0x1f0);
		else
			if((v & 0x80) != 0)
				irqen |= v << 4 & 0x1f0;
			else
				irqen &= ~(v << 4 & 0x1f0);
		break;
	case 14: case 15:
		if((v & 0x10) != 0){
			timer[n * 2 + (a & 1)] = timrel[n * 2 + (a & 1)];
			v &= ~0x10;
		}
		break;
	}
	cia[n * 16 + a] = v;
}

u8int
mioread(u16int a)
{
	u8int b, v;

	switch(a & 0xc00){
	case 0:
		b = a & 63;
		switch(b){
		case CTRL1:
			return reg[b] & 0x7f | ppuy >> 1 & 0x80;
		case RASTER:
			return ppuy;
		case IRQLATCH:
			return irq & 0xf | (irq & irqen & 0xf) + 0x7f & 0x80;
		case IRQEN:
			return irqen & 0xf;
		case SPRSPR:
		case SPRBG:
			v = reg[b];
			reg[b] = 0;
			return v;
		}
		if(b >= 0x20)
			return reg[b] | 0xf0;
		if(b >= 47)
			return 0xff;
		return reg[b];
	case 0x800:
		return cram[a & 0x3ff];
	case 0xc00:
		if((a & 0x200) == 0)
			return ciaread(a >> 8 & 1, a & 0xf);
	default:
		return 0xff;
	}
}

void
miowrite(u16int a, u8int v)
{
	u8int b;

	switch(a & 0xc00){
	case 0:
		b = a & 63;
		if(b >= 0x20)
			v &= 0xf;
		switch(b){
		case CTRL2: v |= 0xc0; break;
		case IRQLATCH:
			v |= 0xf0;
			irq &= ~(v & 0xf);
			break;
		case IRQEN:
			irqen = irqen & ~0xf | v & 0xf;
			v |= 0xf0;
			break;
		}
		if(b < 47)
			reg[b] = v;
		if(b == CTRL1 || b == CTRL2)
			bordset();
		return;
	case 0x800:
		cram[a & 0x3ff] = v & 0xf;
		return;
	case 0xc00:
		if((a & 0x200) == 0)
			ciawrite(a >> 8 & 1, a & 0xf, v);
		return;
	}
}

void
tapestep(void)
{
	static int tapectr;
	static int idx;
	
	if((ram[1] & 1<<5) != 0)
		return;
	if(tapectr == 0){
		if(idx >= tapelen){
			progress(0, 0);
			tapeplay = 0;
			idx = 0;
			return;
		}
		tapectr = tape[idx++] << 3;
		if(tapever == 1 && tapectr == 0){
			tapectr = tape[idx++];
			tapectr |= tape[idx++] << 8;
			tapectr |= tape[idx++] << 16;
		}
		progress(idx, tapelen);
	}else{
		tapectr--;
		if(tapectr == 0)
			irq |= IRQFLAG;
	}
}

void
timerstep(void)
{
	int i, at;
	u8int a, b;
	u16int *t;
	
	for(i = 0; i < 2; i++){
		a = cia[i * 16 + 14];
		b = cia[i * 16 + 15];
		at = 0;
		t = &timer[2 * i];
		if((a & (TIMEREN|TIMERASRC)) == TIMEREN){
			t[0]--;
			if(t[0] == 0){
				at = 1;
				if(i)
					nmi |= IRQTIMERA;
				else
					irq |= IRQTIMERA;
				if((a & TIMERSTOP) != 0)
					cia[i * 16 + 14] &= ~TIMEREN;
				t[0] = timrel[2 * i];
			}
		}
		if((b & TIMEREN) != 0 && ((b & TIMERBSRC) == TIMERBSYS || (b & TIMERBSRC) == TIMERBA && at)){
			t[1]--;
			if(t[1] == 0){
				if(i)
					nmi |= IRQTIMERB;
				else
					irq |= IRQTIMERB;
				if((b & TIMERSTOP) == 0)
					cia[i * 16 + 15] &= ~TIMEREN;
				t[1] = timrel[2 * i + 1];
			}
		}
	}
	if(tapeplay)
		tapestep();
}

void
io(void)
{
	vicstep();
	timerstep();
}

u8int
memread(u16int a)
{
	io();
	if(a == 1)
		return ram[1] & ~(1<<4) | (tapeplay ^ 1) << 4;
	switch(a >> 12){
	case 8: case 9:
		if((pla & (EXROM|GAME)) == EXROM || (pla & (EXROM|HIRAM|LORAM)) == (HIRAM|LORAM))
			return cart[a & 0x1fff];
		goto def;
	case 10: case 11:
		if((pla & (GAME|HIRAM|LORAM)) == (GAME|HIRAM|LORAM))
			return brom[a & 0x1fff];
		if((pla & (EXROM|GAME|HIRAM)) == HIRAM)
			return cart[8192 + (a & 0x1fff)];
		goto def;
	case 13:
		if((pla & (HIRAM|LORAM)) == 0 || pla == 1)
			goto def;
		if((pla & CHAREN) == 0 && (pla & (EXROM|GAME)) != EXROM)
			return crom[a & 0xfff];
		return mioread(a & 0xfff);
	case 14: case 15:
		if((pla & (EXROM|GAME)) == EXROM)
			return cart[8192 + (a & 0x1fff)];
		if((pla & HIRAM) == HIRAM)
			return krom[a & 0x1fff];
	def:
	default:
		return ram[a];
	}
}

void
memwrite(u16int a, u8int v)
{
	if(a >> 12 == 13 && !((pla & (HIRAM|LORAM)) == 0 || pla == 1 || (pla & CHAREN) == 0 && (pla & (EXROM|GAME)) != EXROM)){
		miowrite(a & 0xfff, v);
		io();
		return;
	}
	ram[a] = v;
	if(a == 1)
		pla = pla & ~7 | v & 7;
	io();
}

u8int
vmemread(u16int a)
{
	a |= vicbank;
	if((a & 0x7000) == 0x1000)
		return crom[a & 0xfff];
	return ram[a];
}

void
memreset(void)
{
	pla = 0x1f;
}
