#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

static int
identify(void)
{
	m->havepge = 0;
	return 0;
}

static void
intrinit(void)
{
	ulong machs;
	int i, ncpu;
	char *cp;
	char node[32];
	char buf[32];

	if((cp = getconf("*nomp")) != nil && strtol(cp, 0, 0) != 0)
		return;
	ncpu = MAX_VIRT_CPUS;
	if (cp = getconf("*ncpu")) {
		ncpu = strtol(cp, 0, 0);
		if (ncpu < 1)
			ncpu = 1;
	}
	machs = 1;
	for (i = 1; i < ncpu; i++) {
		sprint(node, "cpu/%d/availability", i);
		if (xenstore_read(node, buf, sizeof buf) <= 0)
			break;
		print("%s: %s\n", node, buf);
		if (strcmp(buf, "online") == 0) {
			machs |= 1<<i;
			conf.nmach++;
		}
	}
	if (conf.nmach > 1) {
		print("Sorry, SMP not supported yet: 1 of %lud CPUs startd\n", conf.nmach);
		conf.nmach = 1;
	}
}

static void
shutdown(void)
{
	HYPERVISOR_shutdown(1);
}

int xenintrenable(Vctl *v);
int xenintrvecno(int irq);
int xenintrdisable(int irq);
void	xentimerenable(void);
uvlong	xentimerread(uvlong*);
void	xentimerset(uvlong);

PCArch archxen = {
.id=		"Xen",	
.ident=		identify,
.reset=		shutdown,
.intrinit=	intrinit,
.intrenable=	xenintrenable,
.intrvecno=	xenintrvecno,
.intrdisable=	xenintrdisable,
.clockenable=	xentimerenable,
.fastclock=	xentimerread,
.timerset=	xentimerset,
};

/*
 * Placeholders to satisfy external references in generic devarch.c
 */
ulong	getcr4(void)	{ return 0; }
void	putcr4(ulong)	{}
int	inb(int)	{ return 0; }
ushort	ins(int)	{ return 0; }
ulong	inl(int)	{ return 0; }
void	outb(int, int)	{}
void	outs(int, ushort)	{}
void	outl(int, ulong)	{}
void	i8042reset(void)	{}
void	i8253enable(void)	{}
void	i8253init(void)	{}
void	i8253link(void)	{}
uvlong	i8253read(uvlong*)	{ return 0; }
void	i8253timerset(uvlong)	{}
int	i8259disable(int)	{ return 0; }
int	i8259enable(Vctl*)	{ return 0; }
void	i8259init(void)	{}
int	i8259isr(int)	{ return 0; }
void	i8259on(void)	{}
void	i8259off(void)	{}
int	i8259vecno(int)	{ return 0; }
int	mtrrprint(char*, long) { return 0; }
char*	mtrr(uvlong, uvlong, char *) { return nil; }
void	mtrrsync(void) {}

/*
 * XXX until fpsave is debugged
 */
void
fpssesave(FPsave* f)
{
	fpx87save(f);
}

void
fpsserestore(FPsave* f)
{
	fpx87restore(f);
}
