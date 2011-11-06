typedef struct Lock Lock;
typedef struct Label Label;
typedef struct Ureg Ureg;
typedef struct Mach Mach;
typedef struct FPsave FPsave;
typedef struct Notsave Notsave;
typedef struct PMMU PMMU;
typedef struct Confmem Confmem;
typedef struct Conf Conf;
typedef struct Proc Proc;
typedef struct ISAConf ISAConf;
typedef uvlong Tval;
typedef void KMap;
#define	VA(k)		((uintptr)(k))
#define	kmap(p)		(KMap*)((p)->pa|kseg0)
#define	kunmap(k)

#pragma incomplete Ureg

struct Lock
{
	ulong	key;
	u32int	sr;
	uintptr	pc;
	Proc*	p;
	Mach*	m;
	int	isilock;
};

struct Label
{
	ulong sp;
	ulong pc;
};

struct FPsave
{
	ulong status;
	ulong control;
	ulong regs[8][3];
	int fpstate;
};

enum
{
	FPinit,
	FPactive,
	FPinactive,
};

struct Notsave
{
	int emptiness;
};

struct PMMU
{
	ulong l1[USTKTOP / MiB];
};

#include "../port/portdat.h"

struct Mach
{
	int	machno;			/* physical id of processor */
	uintptr	splpc;			/* pc of last caller to splhi */

	Proc*	proc;			/* current process */
	Proc*	externup;

	int	flushmmu;		/* flush current proc mmu state */

	ulong	ticks;			/* of the clock since boot time */
	Label	sched;			/* scheduler wakeup */
	Lock	alarmlock;		/* access to alarm list */
	void*	alarm;			/* alarms bound to this clock */
	int	inclockintr;

	Proc*	readied;		/* for runproc */
	ulong	schedticks;		/* next forced context switch */

	int	cputype;
	ulong	delayloop;

	/* stats */
	int	tlbfault;
	int	tlbpurge;
	int	pfault;
	int	cs;
	int	syscall;
	int	load;
	int	intr;
	uvlong	fastclock;		/* last sampled value */
	uvlong	inidle;			/* time spent in idlehands() */
	ulong	spuriousintr;
	int	lastintr;
	int	ilockdepth;
	Perf	perf;
	uvlong	cyclefreq;		/* Frequency of user readable cycle counter */

};

struct Confmem
{
	uintptr	base;
	usize	npage;
	uintptr	limit;
	uintptr	kbase;
	uintptr	klimit;
};

struct Conf
{
	ulong	nmach;		/* processors */
	ulong	nproc;		/* processes */
	Confmem	mem[1];		/* physical memory */
	ulong	npage;		/* total physical pages of memory */
	usize	upages;		/* user page pool */
	ulong	copymode;	/* 0 is copy on write, 1 is copy on reference */
	ulong	ialloc;		/* max interrupt time allocation in bytes */
	ulong	pipeqsize;	/* size in bytes of pipe queues */
	ulong	nimage;		/* number of page cache image headers */
	ulong	nswap;		/* number of swap pages */
	int	nswppo;		/* max # of pageouts per segment pass */
	ulong	hz;		/* processor cycle freq */
	ulong	mhz;
	int	monitor;	/* flag */
};

struct
{
	Lock;
	int	machs;			/* bitmap of active CPUs */
	int	exiting;		/* shutdown */
	int	ispanic;		/* shutdown in response to a panic */
}active;

extern Mach *m;
#define up (((Mach*)MACHADDR)->externup)
extern Mach* machaddr[MAXMACH];
#define MACHP(n)	(machaddr[n])
extern uintptr kseg0;

#define AOUT_MAGIC (E_MAGIC)
#define NCOLOR 1

struct ISAConf
{
	char *type;
	ulong port, irq;
};
