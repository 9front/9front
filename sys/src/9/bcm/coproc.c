/*
 * arm co-processors
 * mainly to cope with arm hard-wiring register numbers into instructions.
 *
 * CP15 (system control) is the one that gets used the most in practice.
 *
 * these routines must be callable from KZERO.
 *
 * on a multiprocessor, process switching to another cpu is assumed
 * to be inhibited by the caller as these registers are local to the cpu.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "arm.h"

enum {
	/* alternates:	0xe12fff1e	BX (R14); last e is R14 */
	/*		0xe28ef000	B 0(R14); second e is R14 (ken) */
	Retinst	= 0xe1a0f00e,		/* MOV R14, R15 */

	Opmask	= MASK(3),
	Regmask	= MASK(4),
};

static void*
mkinstr(ulong wd)
{
	static ulong ib[256], *ep[MAXMACH+1];
	static Lock lk;
	ulong *ip, *ie;

	ie = ep[m->machno];
	for(ip = ib; ip < ie; ip += 2)
		if(*ip == wd)
			return ip;

	ilock(&lk);
	ie = ep[MAXMACH];
	for(; ip < ie; ip += 2)
		if(*ip == wd)
			goto Found;
	if(ip >= &ib[nelem(ib)])
		panic("mkinstr: out of instrucuction buffer");
	ip[0] = wd;
	ip[1] = Retinst;
	ep[MAXMACH] = ie = ip + 2;
	cachedwbse(ip, 2*sizeof(*ip));
Found:
	iunlock(&lk);
	cacheiinv();
	ep[m->machno] = ie;
	return ip;
}


static void*
setupcpop(ulong opcode, int cp, int op1, int crn, int crm,
	int op2)
{
	op1 &= Opmask;
	op2 &= Opmask;
	crn &= Regmask;
	crm &= Regmask;
	cp  &= Regmask;
	return mkinstr(opcode | op1 << 21 | crn << 16 | cp << 8 | op2 << 5 | crm);
}

ulong
cprd(int cp, int op1, int crn, int crm, int op2)
{
	/*
	 * MRC.  return value will be in R0, which is convenient.
	 * Rt will be R0.
	 */
	ulong (*fp)(void) = setupcpop(0xee100010, cp, op1, crn, crm, op2);
	return fp();
}

void
cpwr(int cp, int op1, int crn, int crm, int op2, ulong val)
{
	/* MCR, Rt is R0 */
	void (*fp)(ulong) = setupcpop(0xee000010, cp, op1, crn, crm, op2);
	fp(val);
}

ulong
cprdsc(int op1, int crn, int crm, int op2)
{
	return cprd(CpSC, op1, crn, crm, op2);
}

void
cpwrsc(int op1, int crn, int crm, int op2, ulong val)
{
	cpwr(CpSC, op1, crn, crm, op2, val);
}

/* floating point */

/* fp coproc control */
static void*
setupfpctlop(int opcode, int fpctlreg)
{
	fpctlreg &= Nfpctlregs - 1;
	return mkinstr(opcode | fpctlreg << 16 | 0 << 12 | CpFP << 8);
}

ulong
fprd(int fpreg)
{
	/*
	 * VMRS.  return value will be in R0, which is convenient.
	 * Rt will be R0.
	 */
	ulong (*fp)(void) = setupfpctlop(0xeef00010, fpreg);
	return fp();
}

void
fpwr(int fpreg, ulong val)
{
	/*
	 * fpu might be off and this VMSR might enable it
	 * VMSR, Rt is R0
	 */
	void (*fp)(ulong) = setupfpctlop(0xeee00010, fpreg);
	fp(val);
}

/* fp register access; don't bother with single precision */
static void*
setupfpop(int opcode, int fpreg)
{
	ulong wd = opcode | 0 << 16 | (fpreg & (16 - 1)) << 12;
	if (fpreg >= 16)
		wd |= 1 << 22;		/* high bit of dfp reg # */
	return mkinstr(wd);
}

ulong
fpsavereg(int fpreg, uvlong *fpp)
{
	/*
	 * VSTR.  pointer will be in R0, which is convenient.
	 * Rt will be R0.
	 */
	ulong (*fp)(uvlong *) = setupfpop(0xed000000 | CpDFP << 8, fpreg);
	return fp(fpp);
}

void
fprestreg(int fpreg, uvlong val)
{
	/* VLDR, Rt is R0 */
	void (*fp)(uvlong *) = setupfpop(0xed100000 | CpDFP << 8, fpreg);
	fp(&val);
}
