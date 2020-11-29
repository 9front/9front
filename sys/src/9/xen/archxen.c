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

int xenintrassign(Vctl *v);
void	xentimerenable(void);
uvlong	xentimerread(uvlong*);
void	xentimerset(uvlong);

PCArch archxen = {
.id=		"Xen",	
.ident=		identify,
.reset=		shutdown,
.intrinit=	intrinit,
.intrassign=	xenintrassign,
.clockenable=	xentimerenable,
.fastclock=	xentimerread,
.timerset=	xentimerset,
};

/*
 * Placeholders to satisfy external references in devarch.c
 */
ulong	getcr4(void)	{ return 0; }
void	putcr4(ulong)	{}
int	inb(int)	{ return 0; }
ushort	ins(int)	{ return 0; }
ulong	inl(int)	{ return 0; }
void	outb(int, int)	{}
void	outs(int, ushort)	{}
void	outl(int, ulong)	{}

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
