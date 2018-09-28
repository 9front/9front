#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include <mach.h>
#include "dat.h"
#include "fns.h"

enum {
	fI = 1<<25,
	fP = 1<<24,
	fLi = 1<<24,
	fU = 1<<23,
	fB = 1<<22,
	fW = 1<<21,
	fL = 1<<20,
	fS = 1<<20,
	fSg = 1<<6,
	fH = 1<<5,
};

void
invalid(u32int instr)
{
	suicide("undefined instruction %8ux @ %8ux", instr, P->R[15] - 4);
}

u32int
evenaddr(u32int addr, u32int mask)
{
	if((addr & mask) == 0)
		return addr;
	suicide("unaligned access %8ux @ %8ux", addr, P->R[15] - 4);
	return addr & ~mask;
}

static u32int
doshift(u32int instr, u8int *carry)
{
	ulong amount, val;

	val = P->R[instr & 15];
	if(instr & (1<<4)) {
		if(instr & (1<<7))
			invalid(instr);
		amount = P->R[(instr >> 8) & 15] & 0xFF;
		if(amount == 0)
			return val;
	} else {
		amount = (instr >> 7) & 31;
		if(amount == 0 && (instr & (3<<5)) != 0)
			amount = 32;
	}
	switch((instr >> 5) & 3) {
	default:
		if(amount == 0)
			return val;
		if(amount < 32) {
			*carry = (val >> (32 - amount)) & 1;
			return val << amount;
		}
		*carry = val & 1;
		return 0;
	case 1:
		if(amount < 32){
			*carry = (val >> (amount - 1)) & 1;
			return val >> amount;
		}
		*carry = val >> 31;
		return 0;
	case 2:
		if(amount < 32){
			*carry = (val >> (amount - 1)) & 1;
			return ((long) val) >> amount;
		}
		if((long)val < 0){
			*carry = 1;
			return -1;
		}
		*carry = 0;
		return 0;
	case 3:
		amount &= 31;
		if(amount){
			*carry = (val >> (amount - 1)) & 1;
			return (val >> amount) | (val << (32 - amount));
		}
		amount = *carry & 1;
		*carry = val & 1;
		return (val>>1) | (amount<<31);
	}
}

static void
single(u32int instr)
{
	long offset;
	u32int addr;
	u32int *Rn, *Rd;
	void *targ;
	Segment *seg;
	
	if(instr & fI) {
		u8int carry = 0;
		if(instr & (1<<4))
			invalid(instr);
		offset = doshift(instr, &carry);
	} else
		offset = instr & ((1<<12) - 1);
	if(!(instr & fU))
		offset = - offset;
	Rn = P->R + ((instr >> 16) & 15);
	Rd = P->R + ((instr >> 12) & 15);
	if((instr & (fW | fP)) == fW)
		invalid(instr);
	if(Rn == P->R + 15) {
		if(instr & fW)
			invalid(instr);
		addr = P->R[15] + 4;
	}
	else
		addr = *Rn;
	if(instr & fP)
		addr += offset;
	if((instr & fB) == 0)
		addr = evenaddr(addr, 3);
	targ = vaddr(addr, (instr & fB) == 0 ? 4 : 1, &seg);
	switch(instr & (fB | fL)) {
	case 0:
		*(u32int*) targ = *Rd;
		break;
	case fB:
		*(u8int*) targ = *Rd;
		break;
	case fL:
		*Rd = *(u32int*) targ;
		break;
	case fB | fL:
		*Rd = *(u8int*) targ;
		break;
	}
	if(Rd == P->R + 15 && !(instr & fL)) {
		if(instr & fB)
			*(u8int*) targ += 8;
		else
			*(u32int*) targ += 8;
	}
	segunlock(seg);
	if(!(instr & fP))
		addr += offset;
	if((instr & fW) || !(instr & fP))
		*Rn = addr;
}

/* atomic compare and swap from libc */
extern int cas(u32int *p, u32int old, u32int new);

static void
swap(u32int instr)
{
	u32int *Rm, *Rn, *Rd, *targ, addr, old, new;
	Segment *seg;
	
	Rm = P->R + (instr & 15);
	Rd = P->R + ((instr >> 12) & 15);
	Rn = P->R + ((instr >> 16) & 15);
	if(Rm == P->R + 15 || Rd == P->R + 15 || Rn == P->R + 15)
		invalid(instr);
	addr = *Rn;
	if((instr & fB) == 0)
		addr = evenaddr(addr, 3);
	targ = (u32int *) vaddr(addr & ~3, 4, &seg);
	do {
		old = *targ;
		new = *Rm;
		if(instr & fB){
			new &= 0xFF;
			new <<= 8*(addr&3);
			new |= old & ~(0xFF << 8*(addr&3));
		}
	} while(!cas(targ, old, new));
	if(instr & fB) {
		old >>= 8*(addr&3);
		old &= 0xFF;
	}
	*Rd = old;
	segunlock(seg);
}

static u32int
add(u32int a, u32int b, u8int type, u8int *carry, u8int *overflow)
{
	u32int res1;
	u64int res2;

	if(type) {
		res2 = (u64int)a - b + *carry - 1;
		res1 = res2;
		if(((a ^ b) & (1<<31)) && !((b ^ res1) & (1<<31))) *overflow = 1;
		else *overflow = 0;
		if(res2 & 0x100000000LL) *carry = 0;
		else *carry = 1;	
	} else {
		res2 = (u64int)a + b + *carry;
		res1 = res2;
		if(!((a ^ b) & (1<<31)) && ((b ^ res1) & (1<<31))) *overflow = 1;
		else *overflow = 0;
		if(res2 & 0x100000000LL) *carry = 1;
		else *carry = 0;
	}
	return res1;
}

static void
alu(u32int instr)
{
	u32int Rn, *Rd, operand, shift, result, op;
	u8int carry, overflow;
	
	Rn = P->R[(instr >> 16) & 15];
	Rd = P->R + ((instr >> 12) & 15);
	if(((instr >> 16) & 15) == 15) {
		Rn += 4;
		if(!(instr & fI) && (instr & (1<<4)))
			Rn += 4;
	}
	if(Rd == P->R + 15 && (instr & fS))
		invalid(instr);

	carry = (P->CPSR & flC) != 0;
	overflow = (P->CPSR & flV) != 0;

	if(instr & fI) {
		operand = instr & 0xFF;
		shift = ((instr >> 8) & 15) << 1;
		if(shift){
			operand = (operand >> shift) | (operand << (32 - shift));
			carry = operand >> 31;
		}
	} else
		operand = doshift(instr, &carry);

	op = (instr >> 21) & 15;
	if(op >= 8 && op <= 11 && !(instr & fS))
		sysfatal("no PSR transfers plz");
	if(op >= 5 && op < 8)
		carry = (P->CPSR & flC) != 0;
	switch(op) {
	case 0: case 8: result = Rn & operand; break;
	case 1: case 9: result = Rn ^ operand; break;
	case 2: case 10: carry = 1; case 6: result = add(Rn, operand, 1, &carry, &overflow); break;
	case 3:          carry = 1; case 7: result = add(operand, Rn, 1, &carry, &overflow); break;
	case 4: case 11: carry = 0; case 5: result = add(operand, Rn, 0, &carry, &overflow); break;
	case 12: result = Rn | operand; break;
	case 13: result = operand; break;
	case 14: result = Rn & ~operand; break;
	case 15: result = ~operand; break;
	default: result = 0; /* never happens */
	}
	if(instr & fS) {
		P->CPSR &= ~FLAGS;
		if(result == 0)
			P->CPSR |= flZ;
		if(result & (1<<31))
			P->CPSR |= flN;
		if(carry)
			P->CPSR |= flC;
		if(overflow)
			P->CPSR |= flV;
	}
	if(op < 8 || op >= 12)
		*Rd = result;
}

static void
branch(u32int instr)
{
	long offset;
	
	offset = instr & ((1<<24) - 1);
	if(offset & (1<<23))
		offset |= ~((1 << 24) - 1);
	offset *= 4;
	if(instr & fLi)
		P->R[14] = P->R[15];
	P->R[15] += offset + 4;
}

static void
halfword(u32int instr)
{
	u32int offset, target, *Rn, *Rd;
	Segment *seg;
	
	if(instr & (1<<22)) {
		offset = (instr & 15) | ((instr >> 4) & 0xF0);
	} else {
		if((instr & 15) == 15)
			invalid(instr);
		offset = P->R[instr & 15];
	}
	if(!(instr & fU))
		offset = - offset;
	if(!(instr & fP) && (instr & fW))
		invalid(instr);
	Rn = P->R + ((instr >> 16) & 15);
	Rd = P->R + ((instr >> 12) & 15);
	if(Rn == P->R + 15 || Rd == P->R + 15)
		sysfatal("R15 in halfword");
	target = *Rn;
	if(instr & fP)
		target += offset;
	if(instr & fH)
		target = evenaddr(target, 1);
	switch(instr & (fSg | fH | fL)) {
	case fSg: *(u8int*) vaddr(target, 1, &seg) = *Rd; break;
	case fSg | fL: *Rd = (long) *(char*) vaddr(target, 1, &seg); break;
	case fH: case fSg | fH: *(u16int*) vaddr(target, 2, &seg) = *Rd; break;
	case fH | fL: *Rd = *(u16int*) vaddr(target, 2, &seg); break;
	case fH | fL | fSg: *Rd = (long) *(short*) vaddr(target, 2, &seg); break;
	}
	segunlock(seg);
	if(!(instr & fP))
		target += offset;
	if(!(instr & fP) || (instr & fW))
		*Rn = target;
}

static void
block(u32int instr)
{
	int i;
	u32int targ, *Rn;
	Segment *seg;

	if(instr & (1<<22))
		invalid(instr);
	Rn = P->R + ((instr >> 16) & 15);
	if(Rn == P->R + 15 || instr & (1<<15))
		sysfatal("R15 block");
	targ = evenaddr(*Rn, 3);
	if(instr & fU) {
		for(i = 0; i < 16; i++) {
			if(!(instr & (1<<i)))
				continue;
			if(instr & fP)
				targ += 4;
			if(instr & fL)
				P->R[i] = *(u32int*) vaddr(targ, 4, &seg);
			else
				*(u32int*) vaddr(targ, 4, &seg) = P->R[i];
			segunlock(seg);
			if(!(instr & fP))
				targ += 4;
		}
	} else {
		for(i = 15; i >= 0; i--) {
			if(!(instr & (1<<i)))
				continue;
			if(instr & fP)
				targ -= 4;
			if(instr & fL)
				P->R[i] = *(u32int*) vaddr(targ, 4, &seg);
			else
				*(u32int*) vaddr(targ, 4, &seg) = P->R[i];
			segunlock(seg);
			if(!(instr & fP))
				targ -= 4;
		}
	}
	if(instr & fW)
		*Rn = targ;
}

static void
multiply(u32int instr)
{
	u32int *Rd, *Rn, *Rs, *Rm, res;
	
	Rm = P->R + (instr & 15);
	Rs = P->R + ((instr >> 8) & 15);
	Rn = P->R + ((instr >> 12) & 15);
	Rd = P->R + ((instr >> 16) & 15);
	if(Rd == Rm || Rm == P->R + 15 || Rs == P->R + 15 || Rn == P->R + 15 || Rd == P->R + 15)
		invalid(instr);
	res = *Rm * *Rs;
	if(instr & (1<<21))
		res += *Rn;
	*Rd = res;
	if(instr & (1<<20)) {
		P->CPSR &= ~(flN | flZ);
		if(res & (1<<31))
			P->CPSR |= flN;
		if(res == 0)
			P->CPSR |= flZ;
	}
}

static void
multiplylong(u32int instr)
{
	u32int *RdH, *RdL, *Rs, *Rm;
	u64int res;
	
	Rm = P->R + (instr & 15);
	Rs = P->R + ((instr >> 8) & 15);
	RdL = P->R + ((instr >> 12) & 15);
	RdH = P->R + ((instr >> 16) & 15);
	if(RdL == RdH || RdH == Rm || RdL == Rm || Rm == P->R + 15 || Rs == P->R + 15 || RdL == P->R + 15 || RdH == P->R + 15)
		invalid(instr);
	if(instr & (1<<22))
		res = ((vlong)*(int*)Rs) * *(int*)Rm;
	else {
		res = *Rs;
		res *= *Rm;
	}
	if(instr & (1<<21)) {
		res += *RdL;
		res += ((uvlong)*RdH) << 32;
	}
	*RdL = res;
	*RdH = res >> 32;
	if(instr & (1<<20)) {
		P->CPSR &= ~FLAGS;
		if(res == 0)
			P->CPSR |= flN;
		if(res & (1LL<<63))
			P->CPSR |= flV;
	}
}

static void
singleex(u32int instr)
{
	u32int *Rn, *Rd, *Rm, *targ, addr;
	Segment *seg;
	
	Rd = P->R + ((instr >> 12) & 15);
	Rn = P->R + ((instr >> 16) & 15);
	if(Rd == P->R + 15 || Rn == P->R + 15)
		invalid(instr);
	addr = evenaddr(*Rn, 3);
	if(instr & fS) {
		targ = vaddr(addr, 4, &seg);
		*Rd = *targ;
		P->lladdr = addr;
		P->llval = *Rd;
		segunlock(seg);
	} else {
		Rm = P->R + (instr & 15);
		if(Rm == P->R + 15)
			invalid(instr);
		targ = vaddr(addr, 4, &seg);

		/*
		 * this is not quite correct as we will succeed even
		 * if the value was modified and then restored to its
		 * original value but good enougth approximation for
		 * libc's _tas(), _cas() and _xinc()/_xdec().
		 */
		*Rd = addr != P->lladdr || !cas(targ, P->llval, *Rm);
		segunlock(seg);
		clrex();
	}
}

void
clrex(void)
{
	P->lladdr = 0;
	P->llval = 0;
}

static void
barrier(void)
{
	static Lock l;

	lock(&l);
	unlock(&l);
}

void
step(void)
{
	u32int instr;
	Segment *seg;

	instr = *(u32int*) vaddr(P->R[15], 4, &seg);
	segunlock(seg);
	if(fulltrace) {
		print("%d ", P->pid);
		if(havesymbols) {
			Symbol s;
			char buf[512];
			
			if(findsym(P->R[15], CTEXT, &s) >= 0)
				print("%s ", s.name);
			if(fileline(buf, 512, P->R[15]) >= 0)
				print("%s ", buf);
		}
		print("%.8ux %.8ux %c%c%c%c\n", P->R[15], instr,
			(P->CPSR & flZ) ? 'Z' : ' ',
			(P->CPSR & flC) ? 'C' : ' ',
			(P->CPSR & flN) ? 'N' : ' ',
			(P->CPSR & flV) ? 'V' : ' '
			);
	}
	P->R[15] += 4;
	switch(instr >> 28) {
	case 0x0: if(!(P->CPSR & flZ)) return; break;
	case 0x1: if(P->CPSR & flZ) return; break;
	case 0x2: if(!(P->CPSR & flC)) return; break;
	case 0x3: if(P->CPSR & flC) return; break;
	case 0x4: if(!(P->CPSR & flN)) return; break;
	case 0x5: if(P->CPSR & flN) return; break;
	case 0x6: if(!(P->CPSR & flV)) return; break;
	case 0x7: if(P->CPSR & flV) return; break;
	case 0x8: if(!(P->CPSR & flC) || (P->CPSR & flZ)) return; break;
	case 0x9: if((P->CPSR & flC) && !(P->CPSR & flZ)) return; break;
	case 0xA: if(!(P->CPSR & flN) != !(P->CPSR & flV)) return; break;
	case 0xB: if(!(P->CPSR & flN) == !(P->CPSR & flV)) return; break;
	case 0xC: if((P->CPSR & flZ) || !(P->CPSR & flN) != !(P->CPSR & flV)) return; break;
	case 0xD: if(!(P->CPSR & flZ) && !(P->CPSR & flN) == !(P->CPSR & flV)) return; break;
	case 0xE: break;
	case 0xF:
		switch(instr & 0xFFF000F0){
		case 0xF5700010:	/* CLREX */
			clrex();
			return;
		case 0xF5700040:	/* DSB */
		case 0xF5700050:	/* DMB */
		case 0xF5700060:	/* ISB */
			barrier();
			return;
		}
	default: sysfatal("condition code %x not implemented (instr %ux, ps %ux)", instr >> 28, instr, P->R[15]);
	}
	if((instr & 0x0FB00FF0) == 0x01000090)
		swap(instr);
	else if((instr & 0x0FE000F0) == 0x01800090)
		singleex(instr);
	else if((instr & 0x0FC000F0) == 0x90)
		multiply(instr);
	else if((instr & 0x0F8000F0) == 0x800090)
		multiplylong(instr);
	else if((instr & ((1<<26) | (1<<27))) == (1 << 26))
		single(instr);
	else if((instr & 0x0E000090) == 0x90 && (instr & 0x60))
		halfword(instr);
	else if((instr & ((1<<26) | (1<<27))) == 0)
		alu(instr);
	else if((instr & (7<<25)) == (5 << 25))
		branch(instr);
	else if((instr & (15<<24)) == (15 << 24))
		syscall();
	else if((instr & (7<<25)) == (4 << 25))
		block(instr);
	else if((instr & 0x0E000F00) == 0x0C000100)
		fpatransfer(instr);
	else if((instr & 0x0E000F10) == 0x0E000100)
		fpaoperation(instr);
	else if((instr & 0x0E000F10) == 0x0E000110)
		fparegtransfer(instr);
	else if(vfp && ((instr & 0x0F000A10) == 0x0E000A00))
		vfpoperation(instr);
	else if(vfp && ((instr & 0x0F000F10) == 0x0E000A10))
		vfpregtransfer(instr);
	else if(vfp && ((instr & 0x0F000A00) == 0x0D000A00))
		vfprmtransfer(instr);
	else
		invalid(instr);
}
