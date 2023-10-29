/*
 * bcm2711 (e.g.raspberry pi 4) architecture-specific stuff
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"
#include "../port/pci.h"
#include "../arm64/sysreg.h"

typedef struct Mbox Mbox;
typedef struct Mboxes Mboxes;

#define	POWERREGS	(VIRTIO+0x100000)

Soc soc = {
	.dramsize	= 0xFC000000,
	.busdram	= 0xC0000000,
	.iosize		= 0x03000000,
	.busio		= 0x7C000000,
	.physio		= 0xFC000000,
	.virtio		= VIRTIO2,
	.armlocal	= 0xFF800000,
	.pciwin		= 0x0600000000ULL,
	.oscfreq	= 54000000,
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
	case 0xd08:
		p = seprint(buf, buf + size, "Cortex-A72");
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
archbcm4link(void)
{
	Pcidev *p;

	/*
	 * The firmware resets PCI before starting the host OS because
	 * without SDRAM the VL805 makes inbound requests to page-in firmware
	 * from SDRAM. If the OS has a different PCI mapping that would all break.
	 * There's no way to pause and move the mappings and it's not really desirable
	 * for the firmware to dictate the PCI configuration. Consequently, the mailbox
	 * is required so that the OS can reset the VLI after asserting PCI chip reset.
	 */
	if((p = pcimatch(nil, 0x1106, 0x3483)) != nil)
		xhcireset(BUSBNO(p->tbdf)<<20 | BUSDNO(p->tbdf)<<15 | BUSFNO(p->tbdf)<<12);

	// addclock0link(wdogfeed, HZ);
}
