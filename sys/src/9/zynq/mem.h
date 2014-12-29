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
#define LINSIZ		32
#define	BLOCKALIGN	LINSIZ
#define	FPalign		16

#define MAXMACH 2
#define KSTACK 4096

#define HZ (1000)
#define MS2HZ (1000/HZ)
#define TK2SEC(t) ((t)/HZ)

#define KZERO 0xF0000000
#define KTZERO (KZERO+0x80000)
#define VMAPSZ (SECSZ * 4)
#define VMAP (KZERO - VMAPSZ)
#define TMAPSZ SECSZ
#define TMAP (VMAP - TMAPSZ)
#define KMAPSZ SECSZ
#define KMAP (TMAP - KMAPSZ)
#define NKMAP (KMAPSZ / BY2PG - 1)
#define MACHSIZE 8192
#define MACH(n) (KZERO+(n)*MACHSIZE)
#define MACHP(n) ((Mach *)MACH(n))
#define MACHL1(n) (ROUND(MACH(MAXMACH), L1SZ) + (n)*L1SZ)
#define VMAPL2 MACHL1(MAXMACH)
#define VMAPL2SZ (L2SZ * (VMAPSZ / SECSZ))
#define TMAPL2(n) (VMAPL2 + VMAPL2SZ + (n) * L2SZ)
#define TMAPL2SZ (MAXMACH * L2SZ)
#define CONFSIZE 65536
#define CONFADDR (KTZERO-CONFSIZE)

#define UZERO 0
#define UTZERO BY2PG
#define UTROUND(t) ROUNDUP(t, BY2PG)
#define USTKTOP 0xE0000000
#define USTKSIZE (16*1024*1024)

#define PTEMAPMEM (1024*1024)
#define PTEPERTAB (PTEMAPMEM/BY2PG)
#define SEGMAPSIZE 1984
#define SSEGMAPSIZE 16

#define PTEVALID L2VALID
#define PTERONLY L2RONLY
#define PTEWRITE L2WRITE
#define PTEUNCACHED L2DEVICE
#define PPN(x) ((x)&~(BY2PG-1))

#define PsrDirq (1<<7)
#define PsrDfiq (1<<6)
#define PsrMask 0x1f
#define PsrMusr 0x10
#define PsrMfiq 0x11
#define PsrMirq 0x12
#define PsrMsvc 0x13
#define PsrMabt 0x17
#define PsrMiabt 0x16 /* not an actual mode; for ureg->type */
#define PsrMund 0x1b

#define DMB WORD $0xf57ff05f
#define DSB WORD $0xf57ff04f
#define ISB WORD $0xf57ff06f
#define WFE WORD $0xe320f002
#define SEV WORD $0xe320f004
#define CPS(m) WORD $(0xf1000000|(m))
#define CPSMODE (1<<17)
#define CPSIE (3<<6|2<<18)
#define CPSID (3<<6|3<<18)
#define Rmach 10
#define Rup 9

#define VMSR(c, r1, r2) WORD $(0x0ee00a10|(c)<<28|(r2)<<16|(r1)<<12)
#define VMRS(c, r1, r2) WORD $(0x0ef00a10|(c)<<28|(r2)<<12|(r1)<<16)
#define FPSID 0x0
#define FPSCR 0x1
#define MVFR1 0x6
#define MVFR0 0x7
#define FPEXC 0x8

#define L1PT 1
#define L1SEC (2|1<<10)
#define L1DEVICE (1<<4)
#define L1CACHED (1<<16|1<<14|1<<12|1<<2)
#define L1KERRW 0
#define L1SZ (4096*4)
#define L2SZ (256*4)
#define SECSZ 1048576
#define SECSH 20
#define NL2 256

#define L1X(va) (((ulong)(va)) >> 20)
#define L1RX(va) (((ulong)(va)) >> 20 & ~3)
#define L2X(va) (((ulong)(va)) >> 12 & 0xff)
#define L2RX(va) (((ulong)(va)) >> 12 & 0x3ff)

#define L2VALID (2|1<<4)
#define L2CACHED (1<<10|1<<8|1<<6|1<<2)
#define L2DEVICE (1<<0)
#define L2KERRW L2KERNEL
#define L2KERNEL 0
#define L2USER (1<<5)
#define L2RONLY (1<<9)
#define L2WRITE 0
#define L2LOCAL (1<<11)

#define TTBATTR (1<<6|1<<3|1<<1)
