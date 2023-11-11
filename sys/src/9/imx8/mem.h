/*
 * Memory and machine-specific definitions.  Used in C and assembler.
 */
#define KiB		1024u			/* Kibi 0x0000000000000400 */
#define MiB		1048576u		/* Mebi 0x0000000000100000 */
#define GiB		1073741824u		/* Gibi 000000000040000000 */

/*
 * Sizes:
 * 	L0	L1	L2	L3
 *	4K	2M	1G	512G
 *	16K	32M	64G	128T
 *	64K	512M	4T	-
 */
#define	PGSHIFT		16		/* log(BY2PG) */
#define	BY2PG		(1ULL<<PGSHIFT)	/* bytes per page */
#define	ROUND(s, sz)	(((s)+(sz-1))&~(sz-1))
#define	PGROUND(s)	ROUND(s, BY2PG)

/* effective virtual address space */
#define EVASHIFT	34
#define EVAMASK		((1ULL<<EVASHIFT)-1)

#define PTSHIFT		(PGSHIFT-3)
#define PTLEVELS	(((EVASHIFT-PGSHIFT)+PTSHIFT-1)/PTSHIFT)	
#define PTLX(v, l)	((((v) & EVAMASK) >> (PGSHIFT + (l)*PTSHIFT)) & ((1 << PTSHIFT)-1))
#define PGLSZ(l)	(1ULL << (PGSHIFT + (l)*PTSHIFT))

#define PTL1X(v, l)	(L1TABLEX(v, l) | PTLX(v, l))
#define L1TABLEX(v, l)	(L1TABLE(v, l) << PTSHIFT)
#define L1TABLES	((-KSEG0+PGLSZ(2)-1)/PGLSZ(2))
#define L1TABLE(v, l)	(L1TABLES - ((PTLX(v, 2) % L1TABLES) >> (((l)-1)*PTSHIFT)) + (l)-1)
#define L1TOPSIZE	(1ULL << (EVASHIFT - PTLEVELS*PTSHIFT))

#define MPIDMASK	3ULL			/* MPIDR_EL1 affinity bits signifying the CPUID */
#define	MAXMACH		4			/* max # cpus system can run */
#define	MACHSIZE	(8*KiB)

#define KSTACK		(8*KiB)
#define STACKALIGN(sp)	((sp) & ~7)		/* bug: assure with alloc */
#define TRAPFRAMESIZE	(38*8)

/* reserved dram for ucalloc() and fbmemalloc() at the end of KZERO (physical) */
#define	UCRAMBASE	(-KZERO - UCRAMSIZE)
#define	UCRAMSIZE	(8*MiB)

#define VDRAM		(0xFFFFFFFFC0000000ULL)	/* 0x40000000 - 0x80000000 */
#define	KTZERO		(VDRAM + 0x100000)	/* 0x40100000 - kernel text start */

#define	VIRTIO		(0xFFFFFFFFB0000000ULL)	/* 0x30000000 */

#define	KZERO		(0xFFFFFFFF80000000ULL)	/* 0x00000000 - kernel address space */

#define VMAP		(0xFFFFFFFF00000000ULL)	/* 0x00000000 - 0x40000000 */

#define KMAPEND		(0xFFFFFFFF00000000ULL)	/* 0x140000000 */
#define KMAP		(0xFFFFFFFE00000000ULL)	/*  0x40000000 */

#define KSEG0		(0xFFFFFFFE00000000ULL)

/* temporary identity map for TTBR0 (using only top-level) */
#define L1BOT		((L1-L1TOPSIZE)&-BY2PG)

/* shared kernel page table for TTBR1 */
#define L1		(L1TOP-L1SIZE)
#define L1SIZE		((L1TABLES+PTLEVELS-2)*BY2PG)
#define L1TOP		((MACHADDR(MAXMACH-1)-L1TOPSIZE)&-BY2PG)

#define MACHADDR(n)	(KTZERO-((n)+1)*MACHSIZE)

#define CONFADDR	(VDRAM + 0x10000)	/* 0x40010000 */

#define BOOTARGS	((char*)CONFADDR)
#define BOOTARGSLEN	0x10000

#define	REBOOTADDR	(VDRAM-KZERO + 0x20000)	/* 0x40020000 */

#define	UZERO		0ULL			/* user segment */
#define	UTZERO		(UZERO+0x10000)		/* user text start */
#define	USTKTOP		((EVAMASK>>1)-0xFFFF)	/* user segment end +1 */
#define	USTKSIZE	(16*1024*1024)		/* user stack size */

#define BLOCKALIGN	64			/* only used in allocb.c */

/*
 * Sizes
 */
#define BI2BY		8			/* bits per byte */
#define BY2SE		4
#define BY2WD		8
#define BY2V		8			/* only used in xalloc.c */

#define	PTEMAPMEM	(1024*1024)
#define	PTEPERTAB	(PTEMAPMEM/BY2PG)
#define	SEGMAPSIZE	8192
#define	SSEGMAPSIZE	16
#define	PPN(x)		((x)&~(BY2PG-1))

#define SHARE_NONE	0
#define SHARE_OUTER	2
#define SHARE_INNER	3

#define CACHE_UC	0
#define CACHE_WB	1
#define CACHE_WT	2
#define CACHE_WB_NA	3

#define MA_MEM_WB	0
#define MA_MEM_WT	1
#define MA_MEM_UC	2
#define MA_DEV_nGnRnE	3
#define MA_DEV_nGnRE	4
#define MA_DEV_nGRE	5
#define MA_DEV_GRE	6

#define	PTEVALID	1
#define PTEBLOCK	0
#define PTETABLE	2
#define PTEPAGE		2

#define PTEMA(x)	((x)<<2)
#define PTEAP(x)	((x)<<6)
#define PTESH(x)	((x)<<8)

#define PTEAF		(1<<10)
#define PTENG		(1<<11)
#define PTEPXN		(1ULL<<53)
#define PTEUXN		(1ULL<<54)

#define PTEKERNEL	PTEAP(0)
#define PTEUSER		PTEAP(1)
#define PTEWRITE	PTEAP(0)
#define PTERONLY	PTEAP(2)
#define PTENOEXEC	(PTEPXN|PTEUXN)

#define PTECACHED	PTEMA(MA_MEM_WB)
#define PTEWT		PTEMA(MA_MEM_WT)
#define PTEUNCACHED	PTEMA(MA_MEM_UC)
#define PTEDEVICE	PTEMA(MA_DEV_nGnRE)

/*
 * Physical machine information from here on.
 *	PHYS addresses as seen from the arm cpu.
 *	BUS  addresses as seen from peripherals
 */
#define	PHYSDRAM	0

#define MIN(a, b)	((a) < (b)? (a): (b))
#define MAX(a, b)	((a) > (b)? (a): (b))
