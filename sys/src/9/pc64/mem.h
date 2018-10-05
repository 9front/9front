/*
 * Memory and machine-specific definitions.  Used in C and assembler.
 */
#define KiB		1024u			/* Kibi 0x0000000000000400 */
#define MiB		1048576u		/* Mebi 0x0000000000100000 */
#define GiB		1073741824u		/* Gibi 000000000040000000 */
#define TiB		1099511627776ull	/* Tebi 0x0000010000000000 */
#define PiB		1125899906842624ull	/* Pebi 0x0004000000000000 */
#define EiB		1152921504606846976ull	/* Exbi 0x1000000000000000 */

#define MIN(a, b)	((a) < (b)? (a): (b))
#define MAX(a, b)	((a) > (b)? (a): (b))

#define ALIGNED(p, a)	(!(((uintptr)(p)) & ((a)-1)))

/*
 * Sizes
 */
#define	BI2BY		8			/* bits per byte */
#define	BI2WD		32			/* bits per word */
#define	BY2WD		8			/* bytes per word */
#define	BY2V		8			/* bytes per double word */
#define	BY2PG		(0x1000ull)		/* bytes per page */
#define	WD2PG		(BY2PG/BY2WD)		/* words per page */
#define	PGSHIFT		12			/* log(BY2PG) */
#define	ROUND(s, sz)	(((s)+((sz)-1))&~((sz)-1))
#define	PGROUND(s)	ROUND(s, BY2PG)
#define	BLOCKALIGN	8
#define	FPalign		16

#define	MAXMACH		128			/* max # cpus system can run */

#define KSTACK		(16*KiB)		/* Size of Proc kernel stack */

/*
 * Time
 */
#define HZ		(100)			/* clock frequency */
#define MS2HZ		(1000/HZ)		/* millisec per clock tick */
#define TK2SEC(t)	((t)/HZ)		/* ticks to seconds */

/*
 *  Address spaces. User:
 */
#define UTZERO		(0x0000000000200000ull)		/* first address in user text */
#define UADDRMASK	(0x00007fffffffffffull)		/* canonical address mask */
#define TSTKTOP		(0x00007ffffffff000ull)
#define USTKSIZE	(16*MiB)			/* size of user stack */
#define USTKTOP		(TSTKTOP-USTKSIZE)		/* end of new stack in sysexec */

/*
 *  Address spaces. Kernel, sorted by address.
 */
#define KZERO		(0xffffffff80000000ull)
#define KTZERO		(KZERO+1*MiB+64*KiB)

#define VMAP		(0xffffff0000000000ull)
#define VMAPSIZE	(512ull*GiB)

#define	KMAP		(0xfffffe8000000000ull)
#define KMAPSIZE	(2*MiB)

/*
 * Fundamental addresses - bottom 64kB saved for return to real mode
 */
#define	CONFADDR	(KZERO+0x1200ull)		/* info passed from boot loader */
#define	APBOOTSTRAP	(KZERO+0x7000ull)		/* AP bootstrap code */
#define	IDTADDR		(KZERO+0x10000ull)		/* idt */
#define	REBOOTADDR	(0x11000)			/* reboot code - physical address */

#define CPU0PML4	(KZERO+0x13000ull)
#define CPU0PDP		(KZERO+0x14000ull)
#define CPU0PD0		(KZERO+0x15000ull)		/* KZERO */
#define CPU0PD1		(KZERO+0x16000ull)		/* KZERO+1GB */

#define	CPU0GDT		(KZERO+0x17000ull)		/* bootstrap processor GDT */

#define	CPU0MACH	(KZERO+0x18000ull)		/* Mach for bootstrap processor */
#define CPU0END		(CPU0MACH+MACHSIZE)

#define	MACHSIZE	(2*KSTACK)

/*
 * Where configuration info is left for the loaded programme.
 * There are 24064 bytes available at CONFADDR.
 */
#define BOOTLINE	((char*)CONFADDR)
#define BOOTLINELEN	64
#define BOOTARGS	((char*)(CONFADDR+BOOTLINELEN))
#define BOOTARGSLEN	(0x6000-0x200-BOOTLINELEN)

/*
 *  known x86 segments (in GDT) and their selectors
 */
#define	NULLSEG	0	/* null segment */
#define	KESEG	1	/* kernel executable */
#define KDSEG	2	/* kernel data */
#define UE32SEG	3	/* user executable 32bit */
#define	UDSEG	4	/* user data/stack */
#define	UESEG	5	/* user executable 64bit */
#define	TSSSEG	8	/* task segment (two descriptors) */

#define	NGDT	10	/* number of GDT entries required */

#define	SELGDT	(0<<2)	/* selector is in gdt */
#define	SELLDT	(1<<2)	/* selector is in ldt */

#define	SELECTOR(i, t, p)	(((i)<<3) | (t) | (p))

#define	NULLSEL	SELECTOR(NULLSEG, SELGDT, 0)
#define KDSEL	NULLSEL
#define	KESEL	SELECTOR(KESEG, SELGDT, 0)
#define	UE32SEL	SELECTOR(UE32SEG, SELGDT, 3)
#define	UDSEL	SELECTOR(UDSEG, SELGDT, 3)
#define	UESEL	SELECTOR(UESEG, SELGDT, 3)
#define	TSSSEL	SELECTOR(TSSSEG, SELGDT, 0)

/*
 *  fields in segment descriptors
 */
#define	SEGDATA	(0x10<<8)	/* data/stack segment */
#define	SEGEXEC	(0x18<<8)	/* executable segment */
#define	SEGTSS	(0x9<<8)	/* TSS segment */
#define	SEGCG	(0x0C<<8)	/* call gate */
#define	SEGIG	(0x0E<<8)	/* interrupt gate */
#define	SEGTG	(0x0F<<8)	/* trap gate */
#define	SEGLDT	(0x02<<8)	/* local descriptor table */
#define	SEGTYPE	(0x1F<<8)

#define	SEGP	(1<<15)		/* segment present */
#define	SEGPL(x) ((x)<<13)	/* priority level */
#define	SEGB	(1<<22)		/* granularity 1==4k (for expand-down) */
#define	SEGD	(1<<22)		/* default 1==32bit (for code) */
#define	SEGE	(1<<10)		/* expand down */
#define	SEGW	(1<<9)		/* writable (for data/stack) */
#define	SEGR	(1<<9)		/* readable (for code) */
#define SEGL	(1<<21)		/* 64 bit */
#define	SEGG	(1<<23)		/* granularity 1==4k (for other) */

/*
 *  virtual MMU
 */
#define	PTEMAPMEM	(1ull*MiB)	
#define	PTEPERTAB	(PTEMAPMEM/BY2PG)
#define	SEGMAPSIZE	65536
#define	SSEGMAPSIZE	16
#define	PPN(x)		((x)&~(BY2PG-1))

/*
 *  physical MMU
 */
#define	PTEVALID	(1ull<<0)
#define	PTEWT		(1ull<<3)
#define	PTEUNCACHED	(1ull<<4)
#define	PTEWRITE	(1ull<<1)
#define	PTERONLY	(0ull<<1)
#define	PTEKERNEL	(0ull<<2)
#define	PTEUSER		(1ull<<2)
#define	PTESIZE		(1ull<<7)
#define	PTEGLOBAL	(1ull<<8)

/*
 * Hierarchical Page Tables.
 * For example, traditional IA-32 paging structures have 2 levels,
 * level 1 is the PD, and level 0 the PT pages; with IA-32e paging,
 * level 3 is the PML4(!), level 2 the PDP, level 1 the PD,
 * and level 0 the PT pages. The PTLX macro gives an index into the
 * page-table page at level 'l' for the virtual address 'v'.
 */
#define PTSZ		(4*KiB)			/* page table page size */
#define PTSHIFT		9			/*  */

#define PTLX(v, l)	(((v)>>(((l)*PTSHIFT)+PGSHIFT)) & ((1<<PTSHIFT)-1))
#define PGLSZ(l)	(1ull<<(((l)*PTSHIFT)+PGSHIFT))

#define	getpgcolor(a)	0

#define RMACH		R15			/* m-> */
#define RUSER		R14			/* up-> */
