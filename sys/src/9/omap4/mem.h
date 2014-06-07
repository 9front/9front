#define KiB		1024u			/* Kibi 0x0000000000000400 */
#define MiB		1048576u		/* Mebi 0x0000000000100000 */
#define GiB		1073741824u		/* Gibi 000000000040000000 */

#define HOWMANY(x, y)	(((x)+((y)-1))/(y))
#define ROUNDUP(x, y)	(HOWMANY((x), (y))*(y))	/* ceiling */
#define ROUNDDN(x, y)	(((x)/(y))*(y))		/* floor */
#define MIN(a, b)	((a) < (b)? (a): (b))
#define MAX(a, b)	((a) > (b)? (a): (b))
#define	PGROUND(s)	ROUNDUP(s, BY2PG)
#define	ROUND(s, sz)	(((s)+(sz-1))&~(sz-1))
#define	PPN(x)		((x)&~(BY2PG-1))
#define F(v, o, w)	(((v) & ((1<<(w))-1))<<(o))

#define FMASK(o, w)	(((1<<(w))-1)<<(o))

#define KZERO		0xF0000000
#define KTZERO		0xF2000000
#define VECTORS		0xFFFF0000
#define MACHADDR	0xFFFF1000

#define UZERO		0
#define UTZERO		BY2PG
#define USTKTOP		0xE0000000

/* we map MMIO somewhere here */
#define IZERO		0xE0000000
#define NIOPAGES	ROUNDDN((KZERO - IZERO) / BY2PG, 256)

#define KSTKSIZ		(16*KiB)
#define KSTACK		KSTKSIZ
#define USTKSIZE		(8*MiB)

#define	HZ		(100)			/* clock frequency */
#define	MS2HZ		(1000/HZ)		/* millisec per clock tick */

#define	MAXSYSARG	7
#define MAXMACH		2

#define BI2BY		8
#define BY2WD		4
#define BY2V		8
#define BY2PG		4096
#define PGSHIFT		12
#define	PTEMAPMEM	1048576
#define	PTEPERTAB	(PTEMAPMEM/BY2PG)
#define SEGMAPSIZE	1984
#define SSEGMAPSIZE	16
#define BLOCKALIGN	32

#define	PTEVALID	(1<<0)
#define	PTERONLY	0
#define	PTEWRITE	(1<<1)
#define	PTEUNCACHED	(1<<2)
#define PTEKERNEL	(1<<3)

#define PHYSDRAM	0x80000000
#define DRAMSIZ		(1024 * MiB)

#define L1PT		PHYSDRAM
#define L1SIZ		(16 * KiB)
#define IOPT		(L1PT + L1SIZ)
#define L2SIZ		(1 * KiB)
#define PRIVL2		(IOPT + L2SIZ * (NIOPAGES / 256))
#define PHYSVECTORS	ROUNDUP(PRIVL2 + L2SIZ, BY2PG)
#define	FIRSTMACH	(PHYSVECTORS + BY2PG)
