/*
 * Time.
 *
 * HZ should divide 1000 evenly, ideally.
 * 100, 125, 200, 250 and 333 are okay.
 */
#define	HZ		100			/* clock frequency */
#define	MS2HZ		(1000/HZ)		/* millisec per clock tick */
#define	TK2SEC(t)	((t)/HZ)		/* ticks to seconds */

enum {
	Mhz	= 1000 * 1000,
};

typedef struct Conf	Conf;
typedef struct Confmem	Confmem;
typedef struct FPsave	FPsave;
typedef struct PFPU	PFPU;
typedef struct ISAConf	ISAConf;
typedef struct Label	Label;
typedef struct Lock	Lock;
typedef struct Memcache	Memcache;
typedef struct MMMU	MMMU;
typedef struct Mach	Mach;
typedef struct Page	Page;
typedef struct PhysUart	PhysUart;
typedef struct Pcidev	Pcidev;
typedef struct PMMU	PMMU;
typedef struct Proc	Proc;
typedef u64int		PTE;
typedef struct Soc	Soc;
typedef struct Uart	Uart;
typedef struct Ureg	Ureg;
typedef uvlong		Tval;
typedef void		KMap;

#pragma incomplete Pcidev
#pragma incomplete Ureg

#define MAXSYSARG	5	/* for mount(fd, mpt, flag, arg, srv) */

/*
 *  parameters for sysproc.c
 */
#define AOUT_MAGIC	(R_MAGIC)

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
	uvlong	regs[32][2];

	ulong	control;
	ulong	status;
};

struct PFPU
{
	FPsave	fpsave[1];

	int	fpstate;
};

enum
{
	FPinit,
	FPactive,
	FPinactive,

	/* bits or'd with the state */
	FPillegal= 0x100,
};

struct Confmem
{
	uintptr	base;
	ulong	npage;
	uintptr	limit;
	uintptr	kbase;
	uintptr	klimit;
};

struct Conf
{
	ulong	nmach;		/* processors */
	ulong	nproc;		/* processes */
	Confmem	mem[3];		/* physical memory */
	ulong	npage;		/* total physical pages of memory */
	ulong	upages;		/* user page pool */
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

/*
 *  MMU stuff in Mach.
 */
struct MMMU
{
	PTE*	mmutop;		/* first level user page table */
};

/*
 *  MMU stuff in proc
 */
#define NCOLOR	1		/* 1 level cache, don't worry about VCE's */

struct PMMU
{
	union {
	Page	*mmufree;	/* mmuhead[0] is freelist head */
	Page	*mmuhead[PTLEVELS];
	};
	Page	*mmutail[PTLEVELS];
	int	asid;
	uintptr	tpidr;
};

#include "../port/portdat.h"

struct Mach
{
	int	machno;			/* physical id of processor */
	uintptr	splpc;			/* pc of last caller to splhi */
	Proc*	proc;			/* current process on this processor */
	/* end of offsets known to asm */

	MMMU;

	PMach;

	int	cputype;
	ulong	delayloop;
	int	cpumhz;
	uvlong	cpuhz;			/* speed of cpu */

	int	stack[1];
};

struct
{
	char	machs[MAXMACH];		/* active CPUs */
	int	exiting;		/* shutdown */
}active;

#define MACHP(n)	((Mach*)MACHADDR(n))

extern register Mach* m;			/* R27 */
extern register Proc* up;			/* R26 */
extern int normalprint;

/*
 *  a parsed plan9.ini line
 */
#define NISAOPT		8

struct ISAConf {
	char	*type;
	uvlong	port;
	int	irq;
	ulong	dma;
	ulong	mem;
	ulong	size;
	ulong	freq;

	int	nopt;
	char	*opt[NISAOPT];
};

/*
 * Horrid. But the alternative is 'defined'.
 */
#ifdef _DBGC_
#define DBGFLG		(dbgflg[_DBGC_])
#else
#define DBGFLG		(0)
#endif /* _DBGC_ */

int vflag;
extern char dbgflg[256];

#define dbgprint	print		/* for now */

/*
 *  hardware info about a device
 */
typedef struct {
	ulong	port;
	int	size;
} Devport;

struct DevConf
{
	ulong	intnum;			/* interrupt number */
	char	*type;			/* card type, malloced */
	int	nports;			/* Number of ports */
	Devport	*ports;			/* The ports themselves */
};
