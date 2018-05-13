#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

enum {
	FLAGN = 1<<31,
	FLAGZ = 1<<30,
	FLAGC = 1<<29,
	FLAGV = 1<<28,
	FLAGT = 1<<5,
	FLAGI = 1<<7,
	
	MUSR = 0x10,
	MFIQ = 0x11,
	MIRQ = 0x12,
	MSVC = 0x13,
	MABT = 0x17,
	MUND = 0x1b,
	MSYS = 0x1f,
	MODE = 0x1f,
	
	R13USR = 0, R14USR, R13FIQ, R14FIQ,
	R13SVC, R14SVC, R13ABT, R14ABT,
	R13IRQ, R14IRQ, R13UND, R14UND,
	SPSRFIQ, SPSRSVC, SPSRABT, SPSRIRQ, SPSRUND,
	R8USR, R9USR, R10USR, R11USR, R12USR,
	R8FIQ, R9FIQ, R10FIQ, R11FIQ, R12FIQ
};
u32int r[16], cpsr, spsr;
u32int saver[R12FIQ+1];
u32int curpc;
int irq;

u32int instr0, instr1, pipel = -1;
int cyc;

Var cpuvars[] = {
	ARR(r), VAR(cpsr), VAR(spsr), ARR(saver), VAR(irq),
	VAR(instr0), VAR(instr1), VAR(pipel),
	{nil, 0, 0},
};

#define pipeflush() {io(); pipel = -1;}
#define io() cyc++

static int steparm(void);
static int stepthumb(void);
int (*step)(void);

void
undefined(u32int instr)
{
	if((cpsr & FLAGT) != 0)
		sysfatal("undefined opcode %#.4ux (pc=%#.8ux)", (u16int)instr, curpc);
	else
		sysfatal("undefined opcode %#.8ux (pc=%#.8ux)", instr, curpc);
}

int
cond(int n, u32int instr)
{
	switch(n){
	case 0: return (cpsr & FLAGZ) != 0;
	case 1: return (cpsr & FLAGZ) == 0;
	case 2: return (cpsr & FLAGC) != 0;
	case 3: return (cpsr & FLAGC) == 0;
	case 4: return (cpsr & FLAGN) != 0;
	case 5: return (cpsr & FLAGN) == 0;
	case 6: return (cpsr & FLAGV) != 0;
	case 7: return (cpsr & FLAGV) == 0;
	case 8: return (cpsr & (FLAGC|FLAGZ)) == FLAGC;
	case 9: return (cpsr & (FLAGC|FLAGZ)) != FLAGC;
	case 10: return ((cpsr ^ cpsr << 3) & FLAGN) == 0;
	case 11: return ((cpsr ^ cpsr << 3) & FLAGN) != 0;
	case 12: return ((cpsr ^ cpsr << 3) & (FLAGN|FLAGZ)) == 0;
	case 13: return ((cpsr ^ cpsr << 3) & (FLAGN|FLAGZ)) != 0;
	case 14: return 1;
	}
	undefined(instr);
	return 0;
}

static void
setcpsr(int n)
{
	if((n & FLAGT) != 0)
		step = stepthumb;
	else
		step = steparm;
	if((cpsr & MODE) == (n & MODE)){
		cpsr = n;
		return;
	}
	switch(cpsr & MODE){
	case MUSR:
	case MSYS:
		saver[R13USR] = r[13];
		saver[R14USR] = r[14];
		break;
	case MFIQ:
		saver[R13FIQ] = r[13];
		saver[R14FIQ] = r[14];
		saver[SPSRFIQ] = spsr;
		break;
	case MSVC:
		saver[R13SVC] = r[13];
		saver[R14SVC] = r[14];
		saver[SPSRSVC] = spsr;
		break;
	case MABT:
		saver[R13ABT] = r[13];
		saver[R14ABT] = r[14];
		saver[SPSRABT] = spsr;
		break;
	case MIRQ:
		saver[R13IRQ] = r[13];
		saver[R14IRQ] = r[14];
		saver[SPSRIRQ] = spsr;
		break;
	case MUND:
		saver[R13UND] = r[13];
		saver[R14UND] = r[14];
		saver[SPSRUND] = spsr;
		break;
	}
	switch(n & MODE){
	case MUSR:
	case MSYS:
		r[13] = saver[R13USR];
		r[14] = saver[R14USR];
		break;
	case MFIQ:
		r[13] = saver[R13FIQ];
		r[14] = saver[R14FIQ];
		spsr = saver[SPSRFIQ];
		break;
	case MSVC:
		r[13] = saver[R13SVC];
		r[14] = saver[R14SVC];
		spsr = saver[SPSRSVC];
		break;
	case MABT:
		r[13] = saver[R13ABT];
		r[14] = saver[R14ABT];
		spsr = saver[SPSRABT];
		break;
	case MIRQ:
		r[13] = saver[R13IRQ];
		r[14] = saver[R14IRQ];
		spsr = saver[SPSRIRQ];
		break;
	case MUND:
		r[13] = saver[R13UND];
		r[14] = saver[R14UND];
		spsr = saver[SPSRUND];
		break;
	default:
		sysfatal("invalid mode switch to %#x (pc=%#.8x)", n, curpc);
	}
	if((cpsr & MODE) == MFIQ){
		memcpy(&saver[R8FIQ], &r[8], 5*4);
		memcpy(&r[8], &saver[R8USR], 5*4);
	}
	if((n & MODE) == MFIQ){
		memcpy(&saver[R8USR], &r[8], 5*4);
		memcpy(&r[8], &saver[R8FIQ], 5*4);
	}
	cpsr = n;
}

static void
interrupt(int src)
{
	u32int v;
	
	v = cpsr;
	setcpsr(cpsr & ~(MODE|FLAGI|FLAGT) | FLAGI | src);
	spsr = v;
	switch(src){
	case MIRQ:
		if((v & FLAGT) != 0)
			r[14] = r[15];
		else
			r[14] = r[15] - 4;
		r[15] = 0x18;
		break;
	case MSVC:
		if((v & FLAGT) != 0)
			r[14] = r[15] - 2;
		else
			r[14] = r[15] - 4;
		r[15] = 0x08;
		break;
	default:
		sysfatal("unknown exception %x\n", src);
	}
	pipeflush();
}

static void
mulspeed(u32int val)
{
	if((int)val < 0) val = ~val;
	if((val >> 8) == 0)
		cyc += 1;
	else if((val >> 16) == 0)
		cyc += 2;
	else if((val >> 24) == 0)
		cyc += 3;
	else
		cyc += 4;
}

static void
armextra(u32int instr)
{
	int Rn, Rd, Rm, Rs, sh;
	u32int addr, off, val;
	u64int vall;
	enum {
		SIGN = 1<<6,
		HALF = 1<<5,
		LOAD = 1<<20,
		WRBACK = 1<<21,
		IMM = 1<<22,
		ADD = 1<<23,
		PRE = 1<<24,
		
		BYTE = 1<<22,
		
		LONG = 1<<23,
		MSIGN = 1<<22,
		ACC = 1<<21,
		FLAGS = 1<<20,
	};
	
	Rm = instr & 0xf;
	Rn = instr >> 16 & 0xf;
	Rd = instr >> 12 & 0xf;
	if((instr & 0x60) == 0){
		if((instr & 1<<24) != 0){
			addr = r[Rn];
			if((instr & 0x0ffffff0) == 0x012fff10){
				r[14] = r[15] - 4;
				r[15] = r[Rm] & ~1;
				setcpsr(cpsr | FLAGT);
				pipeflush();
			}else if((instr & BYTE) != 0){
				io();
				val = (u8int) memread(addr, 1, 0);
				memwrite(addr, (u8int) r[Rm], 1);
				r[Rd] = val;
			}else{
				io();
				val = memread(addr & ~3, 4, 0);
				if((addr & 3) != 0){
					sh = (addr & 3) << 2;
					val = val >> sh | val << 32 - sh;
				}
				memwrite(addr & ~3, r[Rm], 4);
				r[Rd] = val;	
			}
		}else{
			Rs = instr >> 8 & 0xf;
			mulspeed(r[Rs]);
			if((instr & LONG) != 0){
				if((instr & ACC) != 0){
					vall = (u64int)r[Rn] << 32 | r[Rd];
					io();
				}else
					vall = 0;
				io();
				if((instr & MSIGN) == 0)
					vall += ((u64int) r[Rs]) * r[Rm];
				else
					vall += (s64int) ((s32int) r[Rs]) * (s32int) r[Rm];
				r[Rn] = vall >> 32;
				r[Rd] = vall;
				if((instr & FLAGS) != 0){
					cpsr &= ~(FLAGN|FLAGZ|FLAGC);
					if(vall == 0)
						cpsr |= FLAGZ;
					if((s64int)vall < 0)
						cpsr |= FLAGN;
				}
			}else{
				val = r[Rs] * r[Rm];
				if((instr & ACC) != 0){
					val += r[Rd];
					io();
				}
				if((instr & FLAGS) != 0){
					cpsr &= ~(FLAGN|FLAGZ|FLAGC);
					if(val == 0)
						cpsr |= FLAGZ;
					if((int)val < 0)
						cpsr |= FLAGN;
				}
				r[Rn] = val;
			}
		}
		return;
	}
	if((instr & IMM) == 0)
		off = r[Rm];
	else
		off = instr & 0xf | instr >> 4 & 0xf0;
	if((instr & ADD) == 0)
		off = -off;
	addr = r[Rn];
	if((instr & PRE) != 0)
		addr += off;
	switch(instr & (HALF|LOAD)){
	case 0:
		memwrite(addr, (u8int) r[Rd], 1);
		break;
	case HALF:
		memwrite(addr & ~1, (u16int) r[Rd], 2);
		break;
	case LOAD:
		io();
		r[Rd] = (u8int) memread(addr, 1, 0);
		if((instr & SIGN) != 0)
			r[Rd] = (s8int) r[Rd];
		break;
	case LOAD|HALF:
		io();
		val = (u16int) memread(addr & ~1, 2, 0);
		if((instr & SIGN) != 0)
			val = (s16int) val;
		if((addr & 1) != 0)
			val = val << 8 | val >> 24;
		r[Rd] = val;
		break;
	}
	if((instr & PRE) == 0)
		addr += off;
	if((instr & (WRBACK|PRE)) != PRE && Rn != Rd)
		r[Rn] = addr;
}

static void
armsr(u32int instr)
{
	int Rd, Rs;
	u32int op, op0;
	
	if((instr & 0x0fbf0fff) == 0x010f0000){
		Rd = instr >> 12 & 0xf;
		r[Rd] = (instr & 1<<22) != 0 ? spsr : cpsr;
		return;
	}
	if((instr & 0x0fb0fff0) == 0x0120f000){
		Rs = instr & 0xf;
		op = r[Rs];
	msr:
		op0 = 0;
		if((instr & 1<<16) != 0) op0 |= 0xff;
		if((instr & 1<<17) != 0) op0 |= 0xff00;
		if((instr & 1<<18) != 0) op0 |= 0xff0000;
		if((instr & 1<<19) != 0) op0 |= 0xff000000;
		if((instr & 1<<22) != 0)
			spsr = spsr & ~op0 | op & op0;
		else
			setcpsr(cpsr & ~op0 | op & op0);
		if((cpsr & FLAGT) != 0)
			sysfatal("illegal MSR to CPSR (T bit set, val=%#.8ux, pc=%#.8ux)", cpsr, curpc);
		return;
	}
	if((instr & 0x0fb0f000) == 0x0320f000){
		op = (u8int) instr;
		Rs = instr >> 7 & 0x1e;
		op = op >> Rs | op << 32 - Rs;
		goto msr;
	}
	if((instr & 0x0ffffff0) == 0x012fff10){
		Rs = instr & 0xf;
		op = r[Rs];
		if((op & 1) != 0)
			setcpsr(cpsr | FLAGT);
		r[15] = op & ~1;
		pipeflush();
		return;
	}
	undefined(instr);
}

static void
armalu(u32int instr)
{
	int Rn, Rd, Rs, Rm, oper, sbit;
	u32int op, op0, res;
	u64int res64;
	int sh;
	int cout;

	if((instr & (1<<25|0x90)) == 0x90){
		armextra(instr);
		return;
	}
	Rn = instr >> 16 & 0xf;
	Rd = instr >> 12 & 0xf;
	Rs = instr >> 8 & 0xf;
	if((instr & 1<<25) == 0){
		Rm = instr & 0xf;
		op = r[Rm];
		if((instr & 1<<4) == 0)
			sh = instr >> 7 & 0x1f;
		else{
			sh = (u8int) r[Rs];
			if(Rm == 15)
				op += 4; /* undocumented behaviour */
		}
		switch(instr >> 5 & 3){
		default:
			if(sh == 0)
				cout = cpsr >> 29;
			else if(sh < 32){
				cout = op >> 32 - sh;
				op = op << sh;
			}else if(sh == 32){
				cout = op;
				op = 0;
			}else
				cout = op = 0;
			break;
		case 1:
			if(sh == 0)
				if((instr & 1<<4) != 0)
					cout = cpsr >> 29;
				else{
					cout = op >> 31;
					op = 0;
				}
			else if(sh < 32 && sh != 0){
				cout = op >> sh - 1;
				op = op >> sh;
			}else if(sh == 32){
				cout = op >> 31;
				op = 0;
			}else
				cout = op = 0;
			break;
		case 2:
			if(sh == 0){
				if((instr & 1<<4) != 0)
					cout = cpsr >> 29;
				else
					cout = op = -((int)op < 0);
			}else if(sh < 32){
				cout = op >> sh - 1;
				op = ((int) op) >> sh;
			}else
				cout = op = -((int)op < 0);
			break;
		case 3:
			if(sh == 0){
				if((instr & 1<<4) != 0)
					cout = cpsr >> 29;
				else{
					cout = op;
					op = op >> 1 | (cpsr & FLAGC) << 2;
				}
			}else{
				sh &= 31;
				if(sh == 0)
					cout = op >> 31;
				else{
					cout = op >> sh - 1;
					op = op << 32 - sh | op >> sh;
				}
			}
			break;
		}
		cyc++;
	}else{
		op = (u8int) instr;
		Rs <<= 1;
		if(Rs != 0){
			op = op << 32 - Rs | op >> Rs;
			cout = op >> 31;
		}else
			cout = cpsr >> 29;
	}
	sbit = instr & 1<<20;
	op0 = r[Rn];
	oper = instr >> 21 & 0xf;
	SET(res64);
	switch(oper){
	default: case 0: case 8: res = op0 & op; break;
	case 1: case 9: res = op0 ^ op; break;
	case 2: case 10: res64 = (uvlong) op0 + ~op + 1; res = res64; break;
	case 3: res64 = (uvlong) op + ~op0 + 1; res = res64; break;
	case 4: case 11: res64 = (uvlong) op0 + op; res = res64; break;
	case 5: res64 = (uvlong) op0 + op + (cpsr >> 29 & 1); res = res64; break;
	case 6: res64 = (uvlong) op0 + ~op + (cpsr >> 29 & 1); res = res64; break;
	case 7: res64 = (uvlong) op + ~op0 + (cpsr >> 29 & 1); res = res64; break;
	case 12: res = op0 | op; break;
	case 13: res = op; break;
	case 14: res = op0 & ~op; break;
	case 15: res = ~op; break;
	}
	if(sbit){
		switch(oper){
		case 2: case 6: case 10:
			cpsr &= ~(FLAGC|FLAGN|FLAGZ|FLAGV);
			if(res64 >> 32 != 0)
				cpsr |= FLAGC;
			if(((op0 ^ op) & (op0 ^ res) & 1<<31) != 0)
				cpsr |= FLAGV;
			break;
		case 3: case 7:
			cpsr &= ~(FLAGC|FLAGN|FLAGZ|FLAGV);
			if(res64 >> 32 != 0)
				cpsr |= FLAGC;
			if(((op ^ op0) & (op ^ res) & 1<<31) != 0)
				cpsr |= FLAGV;
			break;
		case 4: case 5: case 11:
			cpsr &= ~(FLAGC|FLAGN|FLAGZ|FLAGV);
			if(res64 >> 32 != 0)
				cpsr |= FLAGC;
			if((~(op ^ op0) & (op ^ res) & 1<<31) != 0)
				cpsr |= FLAGV;
			break;
		default:
			cpsr &= ~(FLAGC|FLAGN|FLAGZ);
			if(cout & 1)
				cpsr |= FLAGC;
			break;
		}
		if(res == 0)
			cpsr |= FLAGZ;
		if((res & 1<<31) != 0)
			cpsr |= FLAGN;
	}
	if(oper < 8 || oper > 11){
		r[Rd] = res;
		if(Rd == 15){
			if(sbit)
				setcpsr(spsr);
			pipeflush();
		}
	}else if(!sbit){
		if((instr & 1<<25) != 0)
			cyc--;
		armsr(instr);
	}
}

static void
armsingle(u32int instr)
{
	int op, Rn, Rd, Rm;
	u32int off, addr, val, sh;
	enum {
		LOAD = 1<<0,
		WRBACK = 1<<1,
		BYTE = 1<<2,
		ADD = 1<<3,
		PRE = 1<<4,
		REGOFF = 1<<5
	};
	
	op = instr >> 20;
	Rn = instr >> 16 & 0xf;
	Rd = instr >> 12 & 0xf;
	if((op & REGOFF) != 0){
		Rm = instr & 0xf;
		off = r[Rm];
		if((instr & 0xfff0) != 0){
			sh = instr >> 7 & 0x1f;
			switch(instr >> 5 & 3){
			case 0: off = off << sh; break;
			case 1:
				if(sh == 0)
					off = 0;
				else
					off = off >> sh;
				break;
			case 2:
				if(sh == 0)
					off = -((int)off < 0);
				else
					off = ((int)off) >> sh;
				break;
			case 3:
				if(sh == 0)
					off = off >> 1 | (cpsr & FLAGC) << 2;
				else	
					off = off >> sh | off << 32 - sh;
				break;
			}
		}
	}else
		off = instr & 0xfff;
	if((op & ADD) == 0)
		off = -off;
	addr = r[Rn];
	if((op & PRE) != 0)
		addr += off;
	io();
	switch(op & (LOAD|BYTE)){
	case 0:
		memwrite(addr & ~3, r[Rd], 4);
		break;
	case BYTE:
		memwrite(addr, r[Rd], 1);
		break;
	case LOAD:
		val = memread(addr & ~3, 4, 0);
		if((addr & 3) != 0){
			sh = (addr & 3) << 3;
			val = val >> sh | val << 32 - sh;
		}
		r[Rd] = val;
		io();
		if(Rd == 15)
			pipeflush();
		break;
	case LOAD|BYTE:
		r[Rd] = (u8int) memread(addr, 1, 0);
		io();
		if(Rd == 15)
			pipeflush();
		break;
	}
	if((op & PRE) == 0)
		addr += off;
	if((op & (WRBACK|PRE)) != PRE && Rn != Rd)
		r[Rn] = addr;
}

static void
armmulti(u32int instr)
{
	int i, Rn, pop, user;
	u32int addr, val, *rp;
	u16int bits;
	int seq;
	enum {
		LOAD = 1<<20,
		WRBACK = 1<<21,
		USER = 1<<22,
		UP = 1<<23,
		PRE = 1<<24,
	};
	
	Rn = instr >> 16 & 0xf;
	addr = r[Rn] & ~3;
	if((instr & LOAD) != 0)
		io();
	for(bits = instr, pop = 0; bits != 0; pop++)
		bits &= bits - 1;
	pop <<= 2;
	user = (instr & (USER|1<<15)) == USER;
	switch(instr & (PRE|UP)){
	default:
		val = addr - pop;
		addr = val + 4;
		break;
	case PRE:
		addr = val = addr - pop;
		break;
	case UP:
		val = addr + pop;
		break;
	case UP|PRE:
		val = addr + pop;
		addr += 4;
		break;
	}
	seq = 0;
	for(i = 0; i < 16; i++){
		if((instr & 1<<i) == 0)
			continue;
		if(user)
			switch(i){
			case 8: case 9: case 10: case 11: case 12:
				if((cpsr & MODE) == MFIQ){
					rp = &saver[R8USR + i - 8];
					break;
				}
			default: rp = &r[i]; break;
			case 13: rp = &saver[R13USR]; break;
			case 14: rp = &saver[R14USR]; break;
			}
		else
			rp = &r[i];
		if((instr & LOAD) != 0)
			*rp = memread(addr, 4, seq);
		else
			memwrite(addr, *rp, 4);
		addr += 4;
		seq = 1;
	}
	/* undocumented: if Rn is the first register set in a load, it's overwritten if writeback is specified */
	if((instr & WRBACK) != 0 && ((instr & LOAD) == 0 || (instr & instr-1 & 1<<Rn) == 0))
		r[Rn] = val;
	if((instr & (LOAD|1<<15)) == (LOAD|1<<15)){
		if((instr & USER) != 0)
			setcpsr(spsr);
		pipeflush();
	}
}

static void
armbranch(u32int instr)
{
	int a;
	
	a = instr & 0xffffff;
	a = (a << 8) >> 6;
	if((instr & 1<<24) != 0)
		r[14] = r[15] - 4;
	r[15] += a;
	pipeflush();
}

static int
steparm(void)
{
	int s;
	u32int instr;

	cyc = 0;
	if((pipel & 2) != 0)
		goto fetch;
	if(irq && (cpsr & FLAGI) == 0){
		interrupt(MIRQ);
		return 1;
	}
	curpc = r[15] - 8;
	instr = instr1;
	if(trace)
		print("A %.8ux %.8ux %.8ux %.8ux %.8ux | %.8ux %.8ux %.8ux %.8ux | %.8ux %.8ux %.8ux %.8ux\n", curpc, instr, cpsr, r[13], r[14], r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
	if(instr >> 28 != 0xe && !cond(instr >> 28, instr))
		goto fetch;
	switch(instr >> 24 & 0xf){
	case 0: case 1: case 2: case 3:
		armalu(instr);
		break;
	case 4: case 5: case 6: case 7:
		armsingle(instr);
		break;
	case 8: case 9:
		armmulti(instr);
		break;
	case 10: case 11:
		armbranch(instr);
		break;
	case 15:
		interrupt(MSVC);
		break;
	default:
		undefined(instr);
	}
fetch:
	instr1 = instr0;
	s = step == steparm ? 4 : 2;
	instr0 = memread(r[15], s, pipel != -1);
	r[15] += s;
	pipel <<= 1;
	return cyc;
}

static void
addflags(u32int a, u32int b, u32int c)
{
	u64int v;
	
	v = (u64int) a + b + c;
	cpsr &= ~(FLAGN|FLAGZ|FLAGC|FLAGV);
	if((u32int)v == 0)
		cpsr |= FLAGZ;
	cpsr |= v & FLAGN;
	if(v >> 32 != 0)
		cpsr |= FLAGC;
	if((~(a ^ b) & (a ^ v) & 1<<31) != 0)
		cpsr |= FLAGV;
}

static void
nz(u32int v)
{
	cpsr &= ~(FLAGN|FLAGZ);
	if(v == 0)
		cpsr |= FLAGZ;
	cpsr |= v & FLAGN;
}

static void
thshadd(u16int instr)
{
	int Rd, Rs, off, op;
	u32int val, a, b, cout;
	
	Rd = instr & 7;
	Rs = instr >> 3 & 7;
	off = instr >> 6 & 0x1f;
	op = instr >> 11 & 3;
	a = r[Rs];
	switch(op){
	case 0:
		if(off == 0){
			r[Rd] = val = a;
			cout = cpsr >> 29;
		}else{
			r[Rd] = val = a << off;
			cout = a >> 32 - off;
		}
		goto logflags;
	case 1:
		if(off == 0){
			r[Rd] = val = 0;
			cout = a >> 31;
		}else{
			r[Rd] = val = a >> off;
			cout = a >> off - 1;
		}
		goto logflags;
	case 2:
		if(off == 0)
			cout = r[Rd] = val = -((int)a < 0);
		else{
			r[Rd] = val = (int)a >> off;
			cout = a >> off - 1;
		}
		goto logflags;
	case 3:
		break;
	}
	if((instr & 1<<10) == 0)
		b = r[off & 7];
	else
		b = off & 7;
	if((instr & 1<<9) != 0)
		b = -b;
	r[Rd] = a + b;
	addflags(a, b, 0);
	return;
logflags:
	cpsr &= ~(FLAGN|FLAGZ|FLAGC);
	if(val == 0)
		cpsr |= FLAGZ;
	if((int)val < 0)
		cpsr |= FLAGN;
	if((cout & 1) != 0)
		cpsr |= FLAGC;
	
}

static void
thaddimm(u16int instr)
{
	int Rd, b, op, a;
	
	b = instr & 0xff;
	Rd = instr >> 8 & 7;
	a = r[Rd];
	op = instr >> 11 & 3;
	switch(op){
	case 0:
		r[Rd] = b;
		nz(b);
		break;
	case 1:
		addflags(a, ~b, 1);
		break;
	case 2:
		r[Rd] = a + b;
		addflags(a, b, 0);
		break;
	case 3:
		r[Rd] = a - b;
		addflags(a, ~b, 1);
		break;
	}
}

static void
thalu(u16int instr)
{
	int Rs, Rd;
	u32int a, b, v, c;

	switch(instr >> 10 & 3){
	case 0:
		Rd = instr & 7;
		Rs = instr >> 3 & 7;
		a = r[Rd];
		b = r[Rs];
		switch(instr >> 6 & 0xf){
		case 0:
			r[Rd] = v = a & b;
			nz(v);
			break;
		case 1:
			r[Rd] = v = a ^ b;
			nz(v);
			break;
		case 2:
			io();
			v = a;
			if(b != 0){
				if(b < 32){
					c = v >> 32 - b;
					v <<= b;
				}else if(b == 32){
					c = v;
					v = 0;
				}else
					c = v = 0;
				cpsr = cpsr & ~FLAGC | c << 29 & FLAGC;
			}
			r[Rd] = v;
			nz(v);
			break;
		case 3:
			io();
			v = a;
			if(b != 0){
				if(b < 32){
					c = v >> b - 1;
					v >>= b;
				}else if(b == 32){
					c = v >> 31;
					v = 0;
				}else
					c = v = 0;
				cpsr = cpsr & ~FLAGC | c << 29 & FLAGC;
			}
			r[Rd] = v;
			nz(v);
			break;
		case 4:
			io();
			v = a;
			if(b != 0){
				if(b < 32){
					c = v >> b - 1;
					v = (int)v >> b;
				}else
					c = v = -((int)v < 0);
				cpsr = cpsr & ~FLAGC | c << 29 & FLAGC;
			}
			r[Rd] = v;
			nz(v);
			break;
		case 5:
			c = cpsr >> 29 & 1;
			r[Rd] = a + b + c;
			addflags(a, b, c);
			break;
		case 6:
			c = cpsr >> 29 & 1;
			r[Rd] = a + ~b + c;
			addflags(a, ~b, c);
			break;
		case 7:
			io();
			b &= 31;
			r[Rd] = v = a >> b | a << 32 - b;
			if(r[Rs] != 0){
				c = a >> (b - 1 & 31);
				cpsr = cpsr & ~FLAGC | c << 29 & FLAGC;
			}
			nz(v);
			break;
		case 8:
			nz(a & b);
			break;
		case 9:
			r[Rd] = -b;
			addflags(0, ~b, 1);
			break;
		case 10:
			addflags(a, ~b, 1);
			break;
		case 11:
			addflags(a, b, 0);
			break;
		case 12:
			r[Rd] = v = a | b;
			nz(v);
			break;
		case 13:
			r[Rd] = v = a * b;
			mulspeed(a);
			nz(v);
			cpsr &= ~FLAGC;
			break;
		case 14:
			r[Rd] = v = a & ~b;
			nz(v);
			break;
		case 15:
			r[Rd] = ~b;
			nz(~b);
			break;
		}
		break;
	case 1:
		Rd = instr & 7 | instr >> 4 & 8;
		Rs = instr >> 3 & 15;
		switch(instr >> 8 & 3){
		case 0:
			r[Rd] += r[Rs];
			if(Rd == 15){
				r[15] &= ~1;
				pipeflush();
			}
			break;
		case 1:
			addflags(r[Rd], ~r[Rs], 1);
			break;
		case 2:
			r[Rd] = r[Rs];
			if(Rd == 15){
				r[15] &= ~1;
				pipeflush();
			}
			break;
		case 3:
			if((r[Rs] & 1) == 0)
				setcpsr(cpsr & ~FLAGT);
			r[15] = r[Rs] & ~1;
			pipeflush();
			break;
		}
		break;
	case 2: case 3:
		Rd = instr >> 8 & 7;
		a = (r[15] & ~3) + (((u8int) instr) << 2);
		io();
		r[Rd] = memread(a & ~3, 4, 0);
		break;
	}
}

static void
thldst(u16int instr)
{
	int Rd, Rb, Ro, size, sx, load, sh;
	u32int v, off;
	
	Rd = instr & 7;
	Rb = instr >> 3 & 7;
	sx = 0;
	switch(instr >> 13){
	case 2:
		Ro = instr >> 6 & 7;
		off = r[Ro];
		if((instr & 1<<9) != 0){
			load = instr & 3<<10;
			sx = instr & 1<<10;
			size = load == 1<<10 ? 1 : 2;
		}else{
			load = instr & 1<<11;
			size = (instr & 1<<10) != 0 ? 1 : 4;
		}
		break;
	default:
	case 3:
		if((instr & 1<<12) != 0){
			off = instr >> 6 & 0x1f;
			size = 1;
		}else{
			off = instr >> 4 & 0x7c;
			size = 4;
		}
		load = instr & 1<<11;
		break;
	case 4:
		if((instr & 1<<12) == 0){
			off = instr >> 5 & 0x3e;
			size = 2;
			load = instr & 1<<11;
		}else{
			Rb = 13;
			Rd = instr >> 8 & 7;
			off = instr << 2 & 0x3fc;
			load = instr & 1<<11;
			size = 4;
		}
		break;
	}
	off += r[Rb];
	if(load){
		io();
		v = memread(off & -size, size, 0);
		if(sx)
			if(size == 2)
				v = ((int)(v << 16)) >> 16;
			else
				v = ((int)(v << 24)) >> 24;
		if((off & size - 1) != 0){
			sh = (off & size - 1) << 3;
			v = v >> sh | v << 32 - sh;
		}
		r[Rd] = v;
	}else
		memwrite(off & -size, r[Rd], size);
}

static void
thldaddr(u16int instr)
{
	int Rd, imm, v;

	imm = instr << 2 & 0x3fc;
	if((instr & 1<<11) != 0)
		v = r[13];
	else
		v = r[15] & ~3;
	Rd = instr >> 8 & 7;
	r[Rd] = v + imm;
}

static void
thmulti(u16int instr)
{
	int off, lr, Rb;
	int i, seq;
	u32int addr;
	
	if((instr >> 8) == 0xb0){
		off = instr << 2 & 0x1fc;
		if((instr & 1<<7) != 0)
			off = -off;
		r[13] += off;
		return;
	}
	if(instr >> 14 != 3){
		Rb = 13;
		lr = instr & 1<<8;
	}else{
		Rb = instr >> 8 & 7;
		lr = 0;
	}
	addr = r[Rb];
	seq = 0;
	if((instr & 1<<11) != 0){
		io();
		for(i = 0; i < 8; i++){
			if((instr & 1<<i) == 0)
				continue;
			r[i] = memread(addr & ~3, 4, seq);
			addr += 4;
			seq = 1;
		}
		if(lr){
			r[15] = memread(addr & ~3, 4, seq) & ~1;
			pipeflush();
			addr += 4;
		}
	}else if(Rb == 13){
		if(lr){
			addr -= 4;
			memwrite(addr & ~3, r[14], 4);
		}
		for(i = 7; i >= 0; i--){
			if((instr & 1<<i) == 0)
				continue;
			addr -= 4;
			memwrite(addr & ~3, r[i], 4);
		}
	}else
		for(i = 0; i < 8; i++){
			if((instr & 1<<i) == 0)
				continue;
			memwrite(addr & ~3, r[i], 4);
			addr += 4;
		}
	if(Rb == 13 || (instr & 1<<Rb) == 0)
		r[Rb] = addr;
}

static void
thcondbranch(u16int instr)
{
	if((instr >> 8 & 15) == 0xf){
		interrupt(MSVC);
		return;
	}
	if(!cond(instr >> 8 & 15, instr))
		return;
	r[15] += ((int)(instr << 24)) >> 23;
	pipeflush();
}

static void
thbranch(u16int instr)
{
	r[15] += ((int)(instr << 21)) >> 20;
	pipeflush();
}

static void
thlbranch(u16int instr)
{
	if((instr & 1<<11) != 0){
		r[15] = r[14] + (instr << 1 & 0xffe);
		r[14] = curpc + 3;
		pipeflush();
	}else
		r[14] = r[15] + ((int)(instr << 21)>>9);
}

static int
stepthumb(void)
{
	u16int instr;
	int s;

	cyc = 0;
	if((pipel & 2) != 0)
		goto fetch;
	if(irq && (cpsr & FLAGI) == 0){
		interrupt(MIRQ);
		return 1;
	}
	curpc = r[15] - 4;
	instr = instr1;
	if(trace)
		print("T %.8ux %.4ux %.8ux %.8ux %.8ux | %.8ux %.8ux %.8ux %.8ux | %.8ux %.8ux %.8ux %.8ux\n", curpc, instr, cpsr, r[13], r[14], r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
	switch(instr >> 12 & 0xf){
	case 0: case 1:
		thshadd(instr);
		break;
	case 2: case 3:
		thaddimm(instr);
		break;
	case 4:
		thalu(instr);
		break;
	case 5: case 6: case 7: case 8: case 9:
		thldst(instr);
		break;
	case 10:
		thldaddr(instr);
		break;
	case 11: case 12:
		thmulti(instr);
		break;
	case 13:
		thcondbranch(instr);
		break;
	case 14:
		thbranch(instr);
		break;
	case 15:
		thlbranch(instr);
		break;
	default:
		undefined(instr);
	}
fetch:
	instr1 = instr0;
	s = step == steparm ? 4 : 2;
	instr0 = memread(r[15], s, pipel != -1);
	r[15] += s;
	pipel <<= 1;
	return cyc;
}

void
reset(void)
{
	setcpsr(0xd3);
	r[15] = 0;
	pipel = -1;
}

void
cpuload(void)
{
	if((cpsr & FLAGT) != 0)
		step = stepthumb;
	else
		step = steparm;
}
