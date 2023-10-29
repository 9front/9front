/*
 * ARMv8 system registers
 * mainly to cope with arm hard-wiring register numbers into instructions.
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
	ip[1] = 0xd65f03c0;	// RETURN
	ep[MAXMACH] = ie = ip + 2;
	cachedwbinvse(ip, 2*sizeof(*ip));
Found:
	iunlock(&lk);
	cacheiinv();
	ep[m->machno] = ie;
	return ip;
}

uvlong
sysrd(ulong spr)
{
	uvlong (*fp)(void) = mkinstr(0xd5380000UL | spr);
	return fp();
}

void
syswr(ulong spr, uvlong val)
{
	void (*fp)(uvlong) = mkinstr(0xd5180000UL | spr);
	fp(val);
}
