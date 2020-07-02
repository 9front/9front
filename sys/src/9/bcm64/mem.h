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

#define	MAXMACH		4			/* max # cpus system can run */
#define	MACHSIZE	(8*KiB)

#define KSTACK		(8*KiB)
#define STACKALIGN(sp)	((sp) & ~7)		/* bug: assure with alloc */
#define TRAPFRAMESIZE	(38*8)

#define KSEG0		(0xFFFFFFFC00000000ULL)

#define KMAP		(0xFFFFFFFC00000000ULL)
#define KMAPEND		(0xFFFFFFFF00000000ULL)

#define FRAMEBUFFER	(0xFFFFFFFFA0000000ULL|PTEWT)

#define VMAP		(0xFFFFFFFFB0000000ULL)

#define VIRTIO2		(0xFFFFFFFFBC000000ULL)	/* 0x7C000000	-		0xFC000000 */
#define VIRTIO1		(0xFFFFFFFFBD000000ULL)	/* 0x7D000000	-		0xFD000000 */
#define VIRTIO		(0xFFFFFFFFBE000000ULL) /* 0x7E000000	0x3F000000	0xFE000000 */
#define	ARMLOCAL	(0xFFFFFFFFBF800000ULL)	/* -		0x40000000	0xFF800000 */

#define	VGPIO		(0xFFFFFFFFBF900000ULL|PTEUNCACHED)	/* virtual gpio for pi3 ACT LED */

#define	KZERO		(0xFFFFFFFFC0000000ULL)	/* kernel address space */

#define SPINTABLE	(KZERO+0xd8)
#define CONFADDR	(KZERO+0x100)
#define	REBOOTADDR	(0x1c00)		/* reboot code - physical address */
#define	VCBUFFER	(KZERO+0x3400)		/* videocore mailbox buffer */

#define L1		(L1TOP-L1SIZE)
#define L1SIZE		((L1TABLES+PTLEVELS-2)*BY2PG)
#define L1TOP		((MACHADDR(MAXMACH-1)-L1TOPSIZE)&-BY2PG)

#define MACHADDR(n)	(KTZERO-((n)+1)*MACHSIZE)

#define	KTZERO		(KZERO+0x80000)		/* kernel text start */

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
 *	BUS  addresses as seen from the videocore gpu.
 */
#define	PHYSDRAM	0

#define MIN(a, b)	((a) < (b)? (a): (b))
#define MAX(a, b)	((a) > (b)? (a): (b))
