typedef struct Conf	Conf;
typedef struct Confmem	Confmem;
typedef struct FPsave	FPsave;
typedef struct L1	L1;
typedef struct Label	Label;
typedef struct Lock	Lock;
typedef struct KMap	KMap;
typedef struct MMMU	MMMU;
typedef struct Mach	Mach;
typedef struct Notsave	Notsave;
typedef struct Page	Page;
typedef struct Proc	Proc;
typedef struct PMMU	PMMU;
typedef u32int		PTE;
typedef struct Ureg	Ureg;
typedef struct ISAConf	ISAConf;
typedef uvlong		Tval;

#pragma incomplete Ureg

#define MAXSYSARG	5	/* for mount(fd, mpt, flag, arg, srv) */

#define AOUT_MAGIC	(E_MAGIC)

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
	uintptr	sp;
	uintptr	pc;
};

struct FPsave
{
	ulong	exc, scr;
	uchar	regs[256];
};

/*
 * FPsave.status
 */
enum
{
	FPinit,
	FPactive,
	FPinactive,
	FPillegal = 0x100
};

struct Confmem
{
	uintptr	base;
	uintptr	limit;
	usize	npage;
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
	int	monitor;
};

/*
 *  things saved in the Proc structure during a notify
 */
struct Notsave {
	int	emptiness;
};

/*
 *  MMU stuff in proc
 */
#define NCOLOR	1

struct PMMU
{
	L1 *l1;
	Page *mmuused, *mmufree;
	
	int nkmap;
	Page *kmaptable;
};

#include "../port/portdat.h"

struct L1
{
	Ref;
	uintptr pa;
	ulong *va;
	L1 *next;
};

struct MMMU
{
	L1 l1;
	L1 *l1free;
	int nfree;
	uchar asid;
};

struct Mach
{
	/* known to assembly */
	int	machno;			/* physical id of processor */
	uintptr	splpc;			/* pc of last caller to splhi */
	Proc*	proc;			/* current process */
	ulong	excregs[3];
	ulong	cycleshi;
	/* end of known to assembly */

	int	flushmmu;		/* flush current proc mmu state */

	ulong	ticks;			/* of the clock since boot time */
	Label	sched;			/* scheduler wakeup */
	Lock	alarmlock;		/* access to alarm list */
	void*	alarm;			/* alarms bound to this clock */
	int	inclockintr;

	Proc*	readied;		/* for runproc */
	ulong	schedticks;		/* next forced context switch */

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
//	ulong	spuriousintr;
	int	lastintr;
	int	ilockdepth;
	Perf	perf;			/* performance counters */


	int	cpumhz;
	uvlong	cpuhz;			/* speed of cpu */
	uvlong	cyclefreq;		/* Frequency of user readable cycle counter */
	
	MMMU;

	int	stack[1];
};

struct ISAConf
{
	int dummy;
	char *type;
	ulong port;
	int irq;
};
#define BUSUNKNOWN -1

struct
{
	Lock;
	int	machs;			/* bitmap of active CPUs */
	int	exiting;		/* shutdown */
	int	ispanic;		/* shutdown in response to a panic */
}active;

extern register Mach* m;			/* R10 */
extern register Proc* up;			/* R9 */

extern int normalprint;

extern ulong *mpcore, *slcr;

void nope(void);
#define NOPE nope();
