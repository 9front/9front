typedef struct BIOS32si	BIOS32si;
typedef struct BIOS32ci	BIOS32ci;
typedef struct Conf	Conf;
typedef struct Confmem	Confmem;
typedef struct FPsave	FPsave;
typedef struct PFPU	PFPU;
typedef struct ISAConf	ISAConf;
typedef struct Label	Label;
typedef struct Lock	Lock;
typedef struct MMU	MMU;
typedef struct Mach	Mach;
typedef struct PCArch	PCArch;
typedef struct Pcidev	Pcidev;
typedef struct PCMmap	PCMmap;
typedef struct PCMslot	PCMslot;
typedef struct Page	Page;
typedef struct PMMU	PMMU;
typedef struct Proc	Proc;
typedef struct Segdesc	Segdesc;
typedef vlong		Tval;
typedef struct Ureg	Ureg;
typedef struct Vctl	Vctl;

#pragma incomplete BIOS32si
#pragma incomplete Pcidev
#pragma incomplete Ureg

#define MAXSYSARG	5	/* for mount(fd, afd, mpt, flag, arg) */

/*
 *  parameters for sysproc.c
 */
#define AOUT_MAGIC	(S_MAGIC)

struct Lock
{
	ulong	key;
	ulong	sr;
	uintptr	pc;
	Proc	*p;
	Mach	*m;
	ushort	isilock;
	long	lockcycles;
};

struct Label
{
	uintptr	sp;
	uintptr	pc;
};

struct FPsave
{
	u16int	fcw;			/* x87 control word */
	u16int	fsw;			/* x87 status word */
	u8int	ftw;			/* x87 tag word */
	u8int	zero;			/* 0 */
	u16int	fop;			/* last x87 opcode */
	u64int	rip;			/* last x87 instruction pointer */
	u64int	rdp;			/* last x87 data pointer */
	u32int	mxcsr;			/* MMX control and status */
	u32int	mxcsrmask;		/* supported MMX feature bits */
	uchar	st[128];		/* shared 64-bit media and x87 regs */
	uchar	xmm[256];		/* 128-bit media regs */
	uchar	ign[96];		/* reserved, ignored */
};

enum
{
	/* this is a state */
	FPinit=		0,
	FPactive=	1,
	FPinactive=	2,

	/*
	 * the following are bits that can be or'd into the state.
	 *
	 * this is biased so that FPinit, FPactive and FPinactive
	 * without any flags refer to user fp state in fpslot[0].
	 */
	FPillegal=	1<<8,	/* fp forbidden in note handler */
	FPpush=		2<<8,	/* trap on use and initialize new fpslot */
	FPnouser=	4<<8,	/* fpslot[0] is kernel regs */
	FPkernel=	8<<8,	/* fp use in kernel (user in fpslot[0] when !FPnouser) */

	FPindexs=	16,
	FPindex1=	1<<FPindexs,
	FPindexm=	3<<FPindexs,
};

struct PFPU
{
	int	fpstate;
	FPsave	*fpsave;	/* fpslot[fpstate>>FPindexs] */
	FPsave	*fpslot[(FPindexm+1)>>FPindexs];
};

struct Confmem
{
	uintptr	base;
	ulong	npage;
	uintptr	kbase;
	uintptr	klimit;
};

struct Conf
{
	ulong	nmach;		/* processors */
	ulong	nproc;		/* processes */
	ulong	monitor;	/* has monitor? */
	Confmem	mem[16];	/* physical memory */
	ulong	npage;		/* total physical pages of memory */
	ulong	upages;		/* user page pool */
	ulong	nimage;		/* number of page cache image headers */
	ulong	nswap;		/* number of swap pages */
	int	nswppo;		/* max # of pageouts per segment pass */
	ulong	copymode;	/* 0 is copy on write, 1 is copy on reference */
	ulong	ialloc;		/* max interrupt time allocation in bytes */
	ulong	pipeqsize;	/* size in bytes of pipe queues */
	int	nuart;		/* number of uart devices */
};

struct Segdesc
{
	u32int	d0;
	u32int	d1;
};

/*
 *  MMU structure for PDP, PD, PT pages.
 */
struct MMU
{
	MMU	*next;
	uintptr	*page;
	int	index;
	int	level;
};

/*
 *  MMU stuff in proc
 */
#define NCOLOR 1
struct PMMU
{
	MMU*	mmuhead;
	MMU*	mmutail;
	MMU*	kmaphead;
	MMU*	kmaptail;
	ulong	kmapcount;
	ulong	kmapindex;
	ulong	mmucount;
	
	u64int	dr[8];
	void	*vmx;
};

#include "../port/portdat.h"

typedef struct {
	u32int	_0_;
	u32int	rsp0[2];
	u32int	rsp1[2];
	u32int	rsp2[2];
	u32int	_28_[2];
	u32int	ist[14];
	u16int	_92_[5];
	u16int	iomap;
} Tss;

struct Mach
{
	int	machno;			/* physical id of processor (KNOWN TO ASSEMBLY) */
	uintptr	splpc;			/* pc of last caller to splhi (KNOWN TO ASSEMBLY) */

	Proc*	proc;			/* current process on this processor (KNOWN TO ASSEMBLY) */

	u64int*	pml4;			/* pml4 base for this processor (va) */
	Tss*	tss;			/* tss for this processor */
	Segdesc	*gdt;			/* gdt for this processor */

	u64int	mmumap[4];		/* bitmap of pml4 entries for zapping */
	MMU*	mmufree;		/* freelist for MMU structures */
	ulong	mmucount;		/* number of MMU structures in freelist */

	ulong	ticks;			/* of the clock since boot time */
	Label	sched;			/* scheduler wakeup */
	Lock	alarmlock;		/* access to alarm list */
	void*	alarm;			/* alarms bound to this clock */
	int	inclockintr;

	Proc*	readied;		/* for runproc */
	ulong	schedticks;		/* next forced context switch */

	int	tlbfault;
	int	tlbpurge;
	int	pfault;
	int	cs;
	int	syscall;
	int	load;
	int	intr;
	int	flushmmu;		/* make current proc flush it's mmu state */
	int	ilockdepth;
	Perf	perf;			/* performance counters */

	ulong	spuriousintr;
	int	lastintr;

	int	loopconst;

	int	cpumhz;
	uvlong	cyclefreq;		/* Frequency of user readable cycle counter */
	uvlong	cpuhz;
	int	cpuidax;
	int	cpuidcx;
	int	cpuiddx;
	char	cpuidid[16];
	char*	cpuidtype;
	uchar	cpuidfamily;
	uchar	cpuidmodel;
	uchar	cpuidstepping;
	int	havetsc;
	int	havepge;
	int	havewatchpt8;
	int	havenx;
	uvlong	tscticks;
	
	u64int	dr7;			/* shadow copy of dr7 */
	
	void*	vmx;

	uintptr	stack[1];
};

/*
 * KMap the structure
 */
typedef void KMap;
#define	VA(k)		((void*)k)

extern u32int MemMin;

struct
{
	char	machs[MAXMACH];		/* bitmap of active CPUs */
	int	exiting;		/* shutdown */
}active;

/*
 *  routines for things outside the PC model, like power management
 */
struct PCArch
{
	char*	id;
	int	(*ident)(void);		/* this should be in the model */
	void	(*reset)(void);		/* this should be in the model */
	int	(*serialpower)(int);	/* 1 == on, 0 == off */
	int	(*modempower)(int);	/* 1 == on, 0 == off */

	void	(*intrinit)(void);
	int	(*intrenable)(Vctl*);
	int	(*intrvecno)(int);
	int	(*intrdisable)(int);
	void	(*introff)(void);
	void	(*intron)(void);

	void	(*clockenable)(void);
	uvlong	(*fastclock)(uvlong*);
	void	(*timerset)(uvlong);
};

/* cpuid instruction result register bits */
enum {
	/* cx */
	Monitor	= 1<<3,

	/* dx */
	Fpuonchip = 1<<0,
	Vmex	= 1<<1,		/* virtual-mode extensions */
	Pse	= 1<<3,		/* page size extensions */
	Tsc	= 1<<4,		/* time-stamp counter */
	Cpumsr	= 1<<5,		/* model-specific registers, rdmsr/wrmsr */
	Pae	= 1<<6,		/* physical-addr extensions */
	Mce	= 1<<7,		/* machine-check exception */
	Cmpxchg8b = 1<<8,
	Cpuapic	= 1<<9,
	Mtrr	= 1<<12,	/* memory-type range regs.  */
	Pge	= 1<<13,	/* page global extension */
	Mca	= 1<<14,	/* machine-check architecture */
	Pat	= 1<<16,	/* page attribute table */
	Pse2	= 1<<17,	/* more page size extensions */
	Clflush = 1<<19,
	Acpif	= 1<<22,	/* therm control msr */
	Mmx	= 1<<23,
	Fxsr	= 1<<24,	/* have SSE FXSAVE/FXRSTOR */
	Sse	= 1<<25,	/* thus sfence instr. */
	Sse2	= 1<<26,	/* thus mfence & lfence instr.s */
	Rdrnd	= 1<<30,	/* RDRAND support bit */
};

enum {						/* MSRs */
	PerfEvtbase	= 0xc0010000,		/* Performance Event Select */
	PerfCtrbase	= 0xc0010004,		/* Performance Counters */

	Efer		= 0xc0000080,		/* Extended Feature Enable */
	Star		= 0xc0000081,		/* Legacy Target IP and [CS]S */
	Lstar		= 0xc0000082,		/* Long Mode Target IP */
	Cstar		= 0xc0000083,		/* Compatibility Target IP */
	Sfmask		= 0xc0000084,		/* SYSCALL Flags Mask */
	FSbase		= 0xc0000100,		/* 64-bit FS Base Address */
	GSbase		= 0xc0000101,		/* 64-bit GS Base Address */
	KernelGSbase	= 0xc0000102,		/* SWAPGS instruction */
};

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

extern PCArch	*arch;			/* PC architecture */

Mach* machp[MAXMACH];
	
#define	MACHP(n)	(machp[n])

extern register Mach* m;			/* R15 */
extern register Proc* up;			/* R14 */

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

typedef struct BIOS32ci {		/* BIOS32 Calling Interface */
	u32int	eax;
	u32int	ebx;
	u32int	ecx;
	u32int	edx;
	u32int	esi;
	u32int	edi;
} BIOS32ci;
