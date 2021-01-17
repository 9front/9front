#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

extern int i8259assign(Vctl*);
extern int i8259irqno(int, int);
extern void i8259init(void);
extern int i8259isr(int);
extern void i8259on(void);
extern void i8259off(void);
extern int i8259vecno(int);

void
archreset(void)
{
	i8042reset();

	/*
	 * Often the BIOS hangs during restart if a conventional 8042
	 * warm-boot sequence is tried. The following is Intel specific and
	 * seems to perform a cold-boot, but at least it comes back.
	 * And sometimes there is no keyboard...
	 *
	 * The reset register (0xcf9) is usually in one of the bridge
	 * chips. The actual location and sequence could be extracted from
	 * ACPI but why bother, this is the end of the line anyway.
	 */
	print("Takes a licking and keeps on ticking...\n");
	*(ushort*)KADDR(0x472) = 0x1234;	/* BIOS warm-boot flag */
	outb(0xcf9, 0x02);
	outb(0xcf9, 0x06);

	print("can't reset\n");
	for(;;)
		idle();
}

void
delay(int millisecs)
{
	millisecs *= m->loopconst;
	if(millisecs <= 0)
		millisecs = 1;
	aamloop(millisecs);
}

void
microdelay(int microsecs)
{
	microsecs *= m->loopconst;
	microsecs /= 1000;
	if(microsecs <= 0)
		microsecs = 1;
	aamloop(microsecs);
}

/*  
 *  performance measurement ticks.  must be low overhead.
 *  doesn't have to count over a second.
 */
ulong
perfticks(void)
{
	uvlong x;

	if(m->havetsc)
		cycles(&x);
	else
		x = 0;
	return x;
}

PCArch archgeneric = {
.id=		"generic",
.ident=		0,
.reset=		archreset,

.intrinit=	i8259init,
.intrassign=	i8259assign,
.intrirqno=	i8259irqno,
.intrvecno=	i8259vecno,
.intrspurious=	i8259isr,
.intron=	i8259on,
.introff=	i8259off,

.clockinit=	i8253init,
.clockenable=	i8253enable,
.fastclock=	i8253read,
.timerset=	i8253timerset,
};
