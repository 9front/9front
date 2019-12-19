#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

static int
intelcputempok(void)
{
	ulong regs[4];

	if(m->cpuiddx & Acpif)
	if(strcmp(m->cpuidid, "GenuineIntel") == 0){
		cpuid(6, regs);
		return regs[0] & 1;
	}
	return 0;
}

static long
cputemprd0(Chan*, void *a, long n, vlong offset)
{
	char buf[32], *s;
	ulong msr, t, res, d;
	vlong emsr;
	ulong regs[4];
	static ulong tj;

	cpuid(6, regs);
	if((regs[0] & 1) == 0)
		goto unsup;
	if(tj == 0){
		/*
		 * magic undocumented msr.  tj(max) is 100 or 85.
		 */
		tj = 100;
		d = m->cpuidmodel;
		d |= (m->cpuidax>>12) & 0xf0;
		if((d == 0xf && (m->cpuidax & 0xf)>1) || d == 0xe){
			if(rdmsr(0xee, &emsr) == 0){
				msr = emsr;
				if(msr & 1<<30)
					tj = 85;
			}
		}
	}
	if(rdmsr(0x19c, &emsr) < 0)
		goto unsup;
	msr = emsr;
	t = -1;
	if(msr & 1<<31){
		t = (msr>>16) & 127;
		t = tj - t;
	}
	res = (msr>>27) & 15;
	s = "";
	if((msr & 0x30) == 0x30)
		s = " alarm";
	snprint(buf, sizeof buf, "%ld±%uld%s\n", t, res, s);
	return readstr(offset, a, n, buf);
unsup:
	return readstr(offset, a, n, "-1±-1 unsupported\n");
}

static long
intelcputemprd(Chan *c, void *va, long n, vlong offset)
{
	char *a;
	long i, r, t;
	Mach *w;

	w = up->wired;
	a = va;
	t = 0;
	for(i = 0; i < conf.nmach; i++){
		procwired(up, i);
		sched();
		r = cputemprd0(c, a, n, offset);
		if(r == 0)
			break;
		offset -= r;
		if(offset < 0)
			offset = 0;
		n -= r;
		a = a + r;
		t += r;
	}
	up->wired = w;
	sched();
	return t;
}

static long
amd0ftemprd(Chan*, void *a, long n, vlong offset)
{
	char *s, *e, buf[64];
	long i, t, j, max;
	Pcidev *p;

	p = pcimatch(0, 0x1022, 0x1103);
	if(p == nil)
		return readstr(offset, a, n, "-1±-1 unsupported\n");
	max = 2;
	if(max > conf.nmach)
		max = conf.nmach;
	s = buf;
	e = buf + sizeof buf;
	for(j = 0; j < max; j++){
		pcicfgw32(p, 0xe4, pcicfgr32(p, 0xe4) & ~4 | j<<2);
		i = pcicfgr32(p, 0xe4);
		if(m->cpuidstepping == 2)
			t = i>>16 & 0xff;
		else{
			t = i>>14 & 0x3ff;
			t *= 3;
			t /= 4;
		}
		t += -49;
		s = seprint(s, e, "%ld±%uld%s\n", t, 1l, "");
	}
	return readstr(offset, a, n, buf);
}

static long
amd10temprd(Chan*, void *a, long n, vlong offset)
{
	char *s, *e, *r, *buf;
	long i, t, c, nb, cores[MAXMACH];
	Pcidev *p;

	nb = 0;
	for(p = 0; p = pcimatch(p, 0x1022, 0x1203); ){
		cores[nb++] = 1 + ((pcicfgr32(p, 0xe8) & 0x3000)>>12);
		if(nb == nelem(cores))
			break;
	}
	if(nb == 0)
		return readstr(offset, a, n, "-1±-1 unsupported\n");
	buf = smalloc(MAXMACH*4*32);
	s = buf;
	e = buf + MAXMACH*4*32;
	nb = 0;
	c = 0;
	for(p = 0; p = pcimatch(p, 0x1022, 0x1203); nb++){
		i = pcicfgr32(p, 0xa4) & 0x7fffffff;
		i >>= 21;
		t = i/8;
		r = ".0";
		if(i % 8 >= 4)
			r = ".5";
		/*
		 * only one value per nb; repeat per core
		 */
		while(c++ < conf.nmach && cores[nb]--)
			s = seprint(s, e, "%ld%s±0.5%s\n", t, r, "");
	}
	i = readstr(offset, a, n, buf);
	free(buf);
	return i;
}

void
cputemplink(void)
{
	if(intelcputempok())
		addarchfile("cputemp", 0444, intelcputemprd, nil);
	if(m->cpuidfamily == 0x0f && !strcmp(m->cpuidid, "AuthenticAMD"))
		addarchfile("cputemp", 0444, amd0ftemprd, nil);
	if(m->cpuidfamily == 0x10 && !strcmp(m->cpuidid, "AuthenticAMD"))
		addarchfile("cputemp", 0444, amd10temprd, nil);
}
