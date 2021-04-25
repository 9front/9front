typedef struct Conf	Conf;
typedef struct Confmem	Confmem;
typedef struct FPsave	FPsave;
typedef struct PFPU	PFPU;
typedef struct ISAConf	ISAConf;
typedef struct Label	Label;
typedef struct Lock	Lock;
typedef struct Mach	Mach;
typedef struct Page	Page;
typedef struct PCArch	PCArch;
typedef struct Pcidev	Pcidev;
typedef struct PMMU	PMMU;
typedef struct Proc	Proc;
typedef struct Sys	Sys;
typedef struct Ureg	Ureg;
typedef struct Vctl	Vctl;
typedef long		Tval;

#pragma incomplete Ureg

#define MAXSYSARG	5	/* for mount(fd, mpt, flag, arg, srv) */

/*
 *  parameters for sysproc.c
 */
#define AOUT_MAGIC	Q_MAGIC

/*
 *  machine dependent definitions used by ../port/dat.h
 */

struct Lock
{
	ulong	key;
	ulong	sr;
	ulong	pc;
	Proc	*p;
	Mach	*m;
	ushort	isilock;
};

struct Label
{
	ulong	sp;
	ulong	pc;
};

/*
 * This structure must agree with fpsave and fprestore asm routines
 */
struct	FPsave
{
	double	fpreg[32];
	union {
		double	fpscrd;
		struct {
			ulong	pad;
			ulong	fpscr;
		};
	};
};

struct	PFPU
{
	int	fpstate;
	FPsave	fpsave[1];
};

enum
{
	FPinit,
	FPactive,
	FPinactive,
};

struct Confmem
{
	ulong	base;
	ulong	npage;
	ulong	kbase;
	ulong	klimit;
};

struct Conf
{
	ulong	nmach;		/* processors */
	ulong	nproc;		/* processes */
	Confmem	mem[1];
	ulong	npage;		/* total physical pages of memory */
	ulong	upages;		/* user page pool */
	ulong	nimage;		/* number of page cache image headers */
	ulong	nswap;		/* number of swap pages */
	int	nswppo;		/* max # of pageouts per segment pass */
	ulong	copymode;	/* 0 is copy on write, 1 is copy on reference */
	int	monitor;		/* has display? */
	ulong	ialloc;		/* bytes available for interrupt time allocation */
	ulong	pipeqsize;	/* size in bytes of pipe queues */
};

/*
 *  mmu goo in the Proc structure
 */
#define NCOLOR 1
struct PMMU
{
	int	mmupid;
};

#include "../port/portdat.h"

/*
 *  machine dependent definitions not used by ../port/dat.h
 */
/*
 * Fake kmap
 */
typedef	void		KMap;
#define	VA(k)		((ulong)(k))
#define	kmap(p)		(KMap*)((p)->pa|KZERO)
#define	kunmap(k)

struct Mach
{
	/* OFFSETS OF THE FOLLOWING KNOWN BY l.s */
	int	machno;			/* physical id of processor */
	uintptr	splpc;			/* pc that called splhi() */
	Proc	*proc;			/* current process on this processor */

	/* ordering from here on irrelevant */
	PMach;

	uintptr	ptabbase;		/* start of page table in kernel virtual space */
	int	slotgen;		/* next pte (byte offset) when pteg is full */
	int	mmupid;			/* next mmu pid to use */
	int	sweepcolor;
	int	trigcolor;
	Rendez	sweepr;

	int	cputype;
	ulong	loopconst;
	vlong	cpuhz;
	ulong	bushz;
	ulong	dechz;
	ulong	tbhz;

	/* MUST BE LAST */
	uintptr	stack[1];
};

struct
{
	char	machs[MAXMACH];
	int	exiting;
}active;

/*
 *  a parsed plan9.ini line
 */
#define NISAOPT		8

struct ISAConf {
	char		*type;
	ulong	port;
	int	irq;
	ulong	dma;
	ulong	mem;
	ulong	size;
	ulong	freq;

	int	nopt;
	char	*opt[NISAOPT];
};

#define	MACHP(n)	((Mach *)((int)&mach0+n*BY2PG))
extern Mach		mach0;

extern register Mach	*m;
extern register Proc	*up;

extern FPsave initfp;
