/*
 * Memory and machine-specific definitions.  Used in C and assembler.
 */

#define MIN(a, b)	((a) < (b)? (a): (b))
#define MAX(a, b)	((a) > (b)? (a): (b))

/*
 * Sizes
 */
#define	BI2BY		8			/* bits per byte */
#define	BI2WD		32			/* bits per word */
#define	BY2WD		4			/* bytes per word */
#define	BY2V		8			/* bytes per double word */
#define	BY2PG		4096			/* bytes per page */
#define	WD2PG		(BY2PG/BY2WD)		/* words per page */
#define	PGSHIFT		12			/* log(BY2PG) */
#define	ROUND(s, sz)	(((s)+((sz)-1))&~((sz)-1))
#define	PGROUND(s)	ROUND(s, BY2PG)
#define	BLOCKALIGN	8
#define FPalign		16			/* required for FXSAVE */

#define	MAXMACH		8			/* max # cpus system can run */
#define	MAX_VIRT_CPUS	MAXMACH
#define	KSTACK		4096			/* Size of kernel stack */

/*
 * Time
 */
#define	HZ		(100)			/* clock frequency */
#define	MS2HZ		(1000/HZ)		/* millisec per clock tick */
#define	TK2SEC(t)	((t)/HZ)		/* ticks to seconds */

/*
 * Fundamental addresses
 */
#define	REBOOTADDR	0x00001000		/* reboot code - physical address */
#define	APBOOTSTRAP	0x80001000		/* AP bootstrap code */
#define	MACHADDR	0x80002000		/* as seen by current processor */
#define	CPU0MACH	MACHADDR		/* Mach for bootstrap processor */
#define	XENCONSOLE	0x80003000		/* xen console ring */
#define	XENSHARED	0x80004000		/* xen shared page */
#define	XENBUS		0x80005000		/* xenbus aka xenstore ring */
#define	XENGRANTTAB	0x80006000		/* grant table */

#define	MACHSIZE	BY2PG

/*
 *  Address spaces
 *
 *  User is at 0-2GB
 *  Kernel is at 2GB-4GB
 */
#define	UZERO		0			/* base of user address space */
#define	UTZERO		(UZERO+BY2PG)		/* first address in user text */
#define UTROUND(t)	ROUNDUP((t), BY2PG)
#define	KZERO		0x80000000		/* base of kernel address space */
#define	KTZERO		0x80010000		/* first address in kernel text */
#define	USTKTOP		(KZERO-BY2PG)		/* byte just beyond user stack */
#define	USTKSIZE	(16*1024*1024)		/* size of user stack */

/*
 *  known x86 segments (in GDT) and their selectors
 *  using the selectors that xen gives us.
 */
#define KESEL FLAT_KERNEL_CS
#define KDSEL FLAT_KERNEL_DS
#define UESEL FLAT_USER_CS
#define UDSEL FLAT_USER_DS

#define	NPROCSEG	1	/* number of per process descriptors */

/*
 *  virtual MMU
 */
#define	PTEMAPMEM	(1024*1024)	
#define	PTEPERTAB	(PTEMAPMEM/BY2PG)
#define	SEGMAPSIZE	1984
#define	SSEGMAPSIZE	16
#define	PPN(x)		((x)&~(BY2PG-1))
#define	PGOFF(x)		((x)&(BY2PG-1))

/*
 *  physical MMU
 */
#define	PTEVALID	(1<<0)
#define	PTEWT		(1<<3)
#define	PTEUNCACHED	(1<<4)
#define	PTECACHED	(0<<4)
#define	PTEWRITE	(1<<1)
#define	PTERONLY	(0<<1)
#define	PTEKERNEL	(0<<2)
#define	PTEUSER		(1<<2)
#define	PTESIZE		(1<<7)
#define	PTEGLOBAL	(1<<8)

/*
 * Macros for calculating offsets within the page directory base
 * and page tables. 
 */
#define PAX(va)		(paemode? ((ulong)(va)>>29) & 0x6 : 0)
#define	PDX(va)		(paemode? (((ulong)(va))>>20) & 0x03FE : (((ulong)(va))>>22) & 0x03FF)
#define	PTX(va)		(paemode? (((ulong)(va))>>11) & 0x03FE : (((ulong)(va))>>12) & 0x03FF)
#define PDB(pdb,va)	(paemode? KADDR(MAPPN((pdb)[((ulong)(va)>>29) & 0x6])) : pdb)

#define	getpgcolor(a)	0
