#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include <mach.h>
#include "dat.h"
#include "fns.h"

void
resetfpa(void)
{
	int i;
	
	P->FPSR = 0x81000000;
	for(i = 0; i < 8; i++)
		P->F[i] = 0;
}

void
fpatransfer(u32int instr)
{
	enum {
		fP = 1<<24,
		fU = 1<<23,
		fT1 = 1<<22,
		fW = 1<<21,
		fL = 1<<20,
		fT0 = 1<<15,
	};

	long double *Fd;
	u32int *Rn, addr;
	int off;
	void *targ;
	Segment *seg;

	Rn = P->R + ((instr >> 16) & 15);
	Fd = P->F + ((instr >> 12) & 7);
	if(Rn == P->R + 15)
		invalid(instr);
	off = (instr & 255) * 4;
	if(!(instr  & fU))
		off = -off;
	addr = *Rn;
	if(instr & fP)
		addr += off;
	targ = vaddr(addr, 8, &seg);
	switch(instr & (fT0 | fT1 | fL)) {
	case 0: *(float *) targ = *Fd; break;
	case fL: *Fd = *(float *) targ; break;
	case fT0: *(double *) targ = *Fd; break;
	case fT0 | fL: *Fd = *(double *) targ; break;
	default: invalid(instr);
	}
	segunlock(seg);
	if(!(instr & fP))
		addr += off;
	if(instr & fW)
		*Rn = addr;
}

static long double
fpasecop(u32int instr)
{
	switch(instr & 15) {
	case 8: return 0.0; break;
	case 9: return 1.0; break;
	case 10: return 2.0; break;
	case 11: return 3.0; break;
	case 12: return 4.0; break;
	case 13: return 5.0; break;
	case 14: return 0.5; break;
	case 15: return 10.0; break;
	}
	return P->F[instr & 7];
}

void
fpaoperation(u32int instr)
{
	long double *Fn, *Fd, op, op2, res;
	int prec, opc;
	
	Fn = P->F + ((instr >> 16) & 7);
	Fd = P->F + ((instr >> 12) & 7);
	op2 = fpasecop(instr);
	op = *Fn;
	prec = ((instr >> 7) & 1) | ((instr >> 18) & 2);
	opc = ((instr >> 20) & 15) | ((instr >> 11) & 16);
	switch(opc) {
	case 0: res = op + op2; break;
	case 1: res = op * op2; break;
	case 2: res = op - op2; break;
	case 3: res = op2 - op; break;
	case 4: res = op / op2; break;
	case 5: res = op2 / op; break;
	case 16: res = op2; break;
	case 17: res = - op2; break;
	case 18: res = fabs(op2); break;
	case 19: res = (vlong) op2; break;
	case 20: res = sqrt(op2); break;
	default: sysfatal("unimplemented FPA operation %#x @ %8ux", opc, P->R[15] - 4);
	return;
	}
	switch(prec) {
	case 0: *Fd = (float) res; break;
	case 1: *Fd = (double) res; break;
	case 2: *Fd = res; break;
	default: invalid(instr);
	}
}

void
fparegtransfer(u32int instr)
{
	u32int *Rd;
	long tmp;
	long double *Fn, op, op2;
	
	Rd = P->R + ((instr >> 12) & 15);
	Fn = P->F + ((instr >> 16) & 7);
	op = fpasecop(instr);
	if(Rd == P->R + 15) {
		op2 = *Fn;
		switch((instr >> 21) & 7) {
		case 4: break;
		case 5: op = - op; break;
		default: invalid(instr);
		}
		if(op2 < op)
			P->CPSR = (P->CPSR & ~FLAGS) | flN;
		else if(op2 >= op) {
			P->CPSR = (P->CPSR & ~FLAGS) | flC;
			if(op2 == op)
				P->CPSR |= flZ;
		} else
			P->CPSR = (P->CPSR & ~FLAGS) | flV;
		return;
	}
	if(instr & (1<<3))
		invalid(instr);
	switch((instr >> 20) & 15) {
	case 0: *Fn = *(long *) Rd; break;
	case 1: tmp = op; *Rd = tmp; break;
	case 2: P->FPSR = *Rd; break;
	case 3: *Rd = P->FPSR; break;
	default: invalid(instr);
	}
}
