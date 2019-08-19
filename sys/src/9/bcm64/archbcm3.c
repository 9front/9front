/*
 * bcm2836 (e.g.raspberry pi 3) architecture-specific stuff
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"
#include "sysreg.h"

typedef struct Mbox Mbox;
typedef struct Mboxes Mboxes;

#define	POWERREGS	(VIRTIO+0x100000)

Soc soc = {
	.dramsize	= 0x3E000000,
	.busdram	= 0xC0000000,
	.iosize		= 0x01000000,
	.busio		= 0x7E000000,
	.physio		= 0x3F000000,
	.virtio		= VIRTIO,
	.armlocal	= 0x40000000,
	.oscfreq	= 19200000,
};

enum {
	Wdogfreq	= 65536,
	Wdogtime	= 10,	/* seconds, ≤ 15 */
};

/*
 * Power management / watchdog registers
 */
enum {
	Rstc		= 0x1c>>2,
		Password	= 0x5A<<24,
		CfgMask		= 0x03<<4,
		CfgReset	= 0x02<<4,
	Rsts		= 0x20>>2,
	Wdog		= 0x24>>2,
};

/*
 * Arm local regs for smp
 */
struct Mbox {
	u32int	doorbell;
	u32int	mbox1;
	u32int	mbox2;
	u32int	startcpu;
};
struct Mboxes {
	Mbox	set[4];
	Mbox	clr[4];
};

enum {
	Mboxregs	= 0x80,
};

void
archreset(void)
{
}

void
archreboot(void)
{
	u32int *r;

	r = (u32int*)POWERREGS;
	r[Wdog] = Password | 1;
	r[Rstc] = Password | (r[Rstc] & ~CfgMask) | CfgReset;
	coherence();
	for(;;)
		;
}

void
wdogfeed(void)
{
	u32int *r;

	r = (u32int*)POWERREGS;
	r[Wdog] = Password | (Wdogtime * Wdogfreq);
	r[Rstc] = Password | (r[Rstc] & ~CfgMask) | CfgReset;
}

void
wdogoff(void)
{
	u32int *r;

	r = (u32int*)POWERREGS;
	r[Rstc] = Password | (r[Rstc] & ~CfgMask);
}


char *
cputype2name(char *buf, int size)
{
	u32int r, part;
	char *p;

	r = sysrd(MIDR_EL1);
	part = (r >> 4) & 0xFFF;
	switch(part){
	case 0xc07:
		p = seprint(buf, buf + size, "Cortex-A7");
		break;
	case 0xd03:
		p = seprint(buf, buf + size, "Cortex-A53");
		break;
	default:
		p = seprint(buf, buf + size, "Unknown-%#x", part);
		break;
	}
	seprint(p, buf + size, " r%udp%ud", (r >> 20) & 0xF, r & 0xF);
	return buf;
}

void
cpuidprint(void)
{
	char name[64];

	cputype2name(name, sizeof name);
	iprint("cpu%d: %dMHz ARM %s\n", m->machno, m->cpumhz, name);
}

int
getncpus(void)
{
	int n, max;
	char *p;
	n = 4;
	if(n > MAXMACH)
		n = MAXMACH;
	p = getconf("*ncpu");
	if(p && (max = atoi(p)) > 0 && n > max)
		n = max;
	return n;
}

void
mboxclear(uint cpu)
{
	Mboxes *mb;

	mb = (Mboxes*)(ARMLOCAL + Mboxregs);
	mb->clr[cpu].mbox1 = 1;
}

void
wakecpu(uint cpu)
{
	Mboxes *mb;

	mb = (Mboxes*)(ARMLOCAL + Mboxregs);
	mb->set[cpu].mbox1 = 1;
}

void
archbcm3link(void)
{
	// addclock0link(wdogfeed, HZ);
}
