#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include <mach.h>
#include "dat.h"
#include "fns.h"

void
resetvfp(void)
{
	int i;

	P->FPSR = 0x00000000;
	for(i = 0; i < Nfpregs; i++)
		P->F[i] = 0;
}

void
vfpregtransfer(u32int instr)
{
	u32int *Rt;
	long double *Fn;
	Rt = P->R + ((instr>>12)&0xF);
	Fn = P->F + ((instr>>16)&0xF);

	switch((instr>>20)&0xF){
	case 0:
		*((int*)Fn) = *Rt; break;
	case 1:
		*Rt = *((int*)Fn); break;
	case 14:
		P->FPSR = *Rt; break;
	case 15:
		if(Rt == (P->R + 15))
			P->CPSR = P->FPSR;
		else
			*Rt = P->FPSR;
		break;
	default:
		sysfatal("unimplemented VFP instruction %8ux @ %8ux", instr, P->R[15] - 4);
	}
}

void
vfprmtransfer(u32int instr)
{
	int n, d, off, sz;
	void* ea;
	Segment *seg;

	n = (instr>>16) & 0xF;
	d = (instr>>12) & 0xF;
	off = (instr & 0xFF) << 2;
	sz = instr & (1<<8);
	if((instr & (1<<23)) == 0)
		off = -off;
	ea = vaddr(evenaddr(P->R[n] + off, 3), sz ? 8 : 4, &seg);
	switch((instr>>20)&0x3){
	case 0:
		if(sz)
			*(double*)ea = P->F[d];
		else
			*(float*)ea = P->F[d];
		break;
	case 1:
		if(sz)
			P->F[d] = *(double*)ea;
		else
			P->F[d] = *(float*)ea;
		break;
	default:
		sysfatal("unimplemented VFP instruction %8ux @ %8ux", instr, P->R[15] - 4);
	}
	segunlock(seg);
}

void
vfparithop(int opc, u32int instr)
{
	int o;
	long double *Fd, *Fn, *Fm;
	Fd = P->F + ((instr>>12)&0xF);
	Fn = P->F + ((instr>>16)&0xF);
	Fm = P->F + (instr&0xF);
	o = ((opc&0x3)<<1) | (opc&0x8) | ((instr>>6)&0x1);

	switch(o){
	case 4:
		*Fd = *Fn * *Fm; break;
	case 6:
		*Fd = *Fn + *Fm; break;
	case 7:
		*Fd = *Fn - *Fm; break;
	case 8:
		*Fd = *Fn / *Fm; break;
	default:
		sysfatal("unimplemented VFP instruction %8ux @ %8ux", instr, P->R[15] - 4);
	}
}

void
vfpotherop(u32int instr)
{
	int o2, o3;
	long double *Fd, *Fm, F0;
	Fd = P->F + ((instr>>12)&0xF);
	Fm = P->F + (instr&0xF);
	F0 = 0.0;
	o2 = (instr>>16) & 0xF;
	o3 = (instr>>6) & 0x3;

	if((o3&1) == 0)
		sysfatal("unimplemented VFP instruction %8ux @ %8ux", instr, P->R[15] - 4);
	switch(o2){
	case 0x5:
		Fm = &F0;
	case 0x4:
		if(*Fd < *Fm)
			P->FPSR = (P->FPSR & ~FLAGS) | flN;
		else if(*Fd >= *Fm) {
			P->FPSR = (P->FPSR & ~FLAGS) | flC;
			if(*Fd == *Fm)
				P->FPSR |= flZ;
		} else
			P->FPSR = (P->FPSR & ~FLAGS) | flV | flC;
		break;
	case 0x8:
		*Fd = *((int*)Fm); break;
	case 0xD:
		*((int*)Fd) = (int)*Fm; break;
	default:
		switch((o2<<1)|(o3>>1)){
		case 0:
		case 15:
			*Fd = *Fm; break;
		case 1:
			*Fd = fabs(*Fm); break;
		case 2:
			*Fd = -*Fm; break;
		case 3:
			*Fd = sqrt(*Fm); break;
		default:
			sysfatal("unimplemented VFP instruction %8ux @ %8ux", instr, P->R[15] - 4);
		}
	}
}

void
vfpoperation(u32int instr)
{
	int o1;
	o1 = (instr>>20) & 0xF;
	if(o1 == 0xB)
		vfpotherop(instr);
	else
		vfparithop(o1, instr);
}

