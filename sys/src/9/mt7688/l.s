/*
 * mips 24k machine assist for mt7688
 */

#include "mem.h"
#include "mips24k.s"


#define SANITY 0x12345678

	NOSCHED

/*
 * Boot only processor
 */

TEXT	start(SB), $-4
	MOVW	$setR30(SB), R30

	DI(0)

	MOVW	sanity(SB), R1
	CONST(SANITY, R2)
	SUBU	R1, R2, R2
	BNE	R2, insane
	NOP


	MOVW	R0, M(COMPARE)
	EHB

	/* don't enable any interrupts nor FP, but leave BEV on. */
	MOVW	$BEV,R1
	MOVW	R1, M(STATUS)
	UBARRIERS(7, R7, stshb)		/* returns to kseg1 space */
	MOVW	R0, M(CAUSE)
	EHB

	/* disable watchdog and other resets */
	MOVW	$(KSEG1|0x10000038), R1
	MOVW	R0, (R1)			/* set no action */
	SYNC

	MOVW	$PE, R1
	MOVW	R1, M(CACHEECC)		/* aka ErrCtl */
	EHB
	JAL	cleancache(SB)
	NOOP


	MOVW	$TLBROFF, R1
	MOVW	R1, M(WIRED)

	MOVW	R0, M(CONTEXT)
	EHB

	/* set KSEG0 cachability before trying LL/SC in lock code */
	MOVW	M(CONFIG), R1
	AND	$~CFG_K0, R1
	/* make kseg0 cachable, enable write-through merging */
	OR	$((PTECACHED>>3)|CFG_MM), R1
	MOVW	R1, M(CONFIG)
	BARRIERS(7, R7, cfghb)			/* back to kseg0 space */

	MOVW	$setR30(SB), R30		/* again */

	/* initialize Mach, including stack */
	MOVW	$MACHADDR, R(MACH)
	ADDU	$(MACHSIZE-BY2V), R(MACH), SP
	MOVW	R(MACH), R1
clrmach:
	MOVW	R0, (R1)
	ADDU	$BY2WD, R1
	BNE	R1, SP, clrmach
	NOOP

	MOVW	$edata(SB), R1
	MOVW	$end(SB), R2
clrbss:
	MOVB	R0, (R1)
	ADDU	$1, R1
	BNE	R1, R2, clrbss
	NOOP

	MOVW	$0x16, R16
	MOVW	$0x17, R17
	MOVW	$0x18, R18
	MOVW	$0x19, R19
	MOVW	$0x20, R20
	MOVW	$0x21, R21
	MOVW	$0x22, R22
	MOVW	$0x23, R23

	MOVW	R0, HI
	MOVW	R0, LO

	MOVW	R0, 0(R(MACH))			/* m->machno = 0 */
	MOVW	R0, R(USER)			/* up = nil */

	JAL	main(SB)
	NOOP

PUTC('X', R1, R2)
NOOP


insane:
	PUTC('D', R1, R2)
	NOOP

TEXT	arcs(SB), $256
	MOVW	R24, 0x80(SP)
	MOVW	R25, 0x84(SP)
	MOVW	R26, 0x88(SP)
	MOVW	R27, 0x8C(SP)

	MOVW	$SPBADDR, R4
	MOVW	0x20(R4), R5
	ADDU	R1, R5
	MOVW	(R5), R2

	MOVW	16(FP), R7
	MOVW	12(FP), R6
	MOVW	8(FP), R5
	MOVW	4(FP), R4

	JAL	(R2)
	NOOP

	MOVW	$setR30(SB), R30

	MOVW	0x80(SP), R24
	MOVW	0x84(SP), R25
	MOVW	0x88(SP), R26
	MOVW	0x8C(SP), R27

	MOVW	R2, R1
	RETURN

/*
 * Take first processor into user mode
 * 	- argument is stack pointer to user
 */

TEXT	touser(SB), $-4
	MOVW	M(STATUS), R4
	MOVW	$(UTZERO+32), R2	/* header appears in text */
	MOVW	R2, M(EPC)
	MOVW	R1, SP
	AND	$(~KMODEMASK), R4
	OR	$(KUSER|IE|EXL), R4	/* switch to user mode, intrs on, exc */
	MOVW	R4, M(STATUS)		/* " */
	NOOP
	ERET				/* clears EXL */
	NOOP


/* target for JALRHB in BARRIERS */
TEXT ret(SB), $-4
	JMP	(R22)
	NOP

/* the i and d caches may be different sizes, so clean them separately */
TEXT	cleancache(SB), $-4
	DI(10)				/* intrs off, old status -> R10 */

	UBARRIERS(7, R7, cchb);		/* return to kseg1 (uncached) */
	MOVW	R0, R1			/* index, not address */
	MOVW	$ICACHESIZE, R9
iccache:
	CACHE	PI+IWBI, (R1)		/* flush & invalidate I by index */
	SUBU	$CACHELINESZ, R9
	BGTZ	R9, iccache
	ADDU	$CACHELINESZ, R1	/* delay slot */

	BARRIERS(7, R7, cc2hb);		/* return to kseg0 (cached) */

	MOVW	R0, R1			/* index, not address */
	MOVW	$DCACHESIZE, R9
dccache:
	CACHE	PD+IWBI, (R1)		/* flush & invalidate D by index */
	SUBU	$CACHELINESZ, R9
	BGTZ	R9, dccache
	ADDU	$CACHELINESZ, R1	/* delay slot */

	SYNC
	MOVW	R10, M(STATUS)
	JRHB(31)			/* return and clear all hazards */

/*
 * manipulate interrupts
 */


/* enable an interrupt; bit is in R1 */
TEXT	intron(SB), $0
	MOVW	M(STATUS), R2
	OR	R1, R2
	MOVW	R2, M(STATUS)
	EHB
	RETURN

/* disable an interrupt; bit is in R1 */
TEXT	introff(SB), $0
	MOVW	M(STATUS), R2
	XOR	$-1, R1
	AND	R1, R2
	MOVW	R2, M(STATUS)
	EHB
	RETURN

TEXT	splhi(SB), $0
	EHB
	MOVW	R31, 12(R(MACH))	/* save PC in m->splpc */
	DI(1)				/* old M(STATUS) into R1 */
	EHB
	RETURN

TEXT	splx(SB), $0
	EHB
	MOVW	R31, 12(R(MACH))	/* save PC in m->splpc */
	MOVW	M(STATUS), R2
	AND	$IE, R1
	AND	$~IE, R2
	OR	R2, R1
	MOVW	R1, M(STATUS)
	EHB
	RETURN

TEXT	spllo(SB), $0
	EHB
	EI(1)				/* old M(STATUS) into R1 */
	EHB
	RETURN

TEXT	spldone(SB), $0
	RETURN

TEXT	islo(SB), $0
	MOVW	M(STATUS), R1
	AND	$IE, R1
	RETURN


TEXT	coherence(SB), $-4
	BARRIERS(7, R7, cohhb)
	SYNC
	EHB
	RETURN

TEXT	idle(SB), $-4
	EI(1)				/* old M(STATUS) into R1 */
	EHB
	/* fall through */

TEXT	wait(SB), $-4
	WAIT
	  NOP

	MOVW	R1, M(STATUS)		/* interrupts restored */
	EHB
	RETURN

/*
 * process switching
 */

TEXT	setlabel(SB), $-4
	MOVW	SP, 0(R1)
	MOVW	R31, 4(R1)
	MOVW	R0, R1
	RETURN

TEXT	gotolabel(SB), $-4
	MOVW	0(R1), SP
	MOVW	4(R1), R31
	MOVW	$1, R1
	RETURN

/*
 * the tlb routines need to be called at splhi.
 */

TEXT	getwired(SB),$0
	MOVW	M(WIRED), R1
	RETURN

TEXT	setwired(SB),$0
	MOVW	R1, M(WIRED)
	RETURN

TEXT	getrandom(SB),$0
	MOVW	M(RANDOM), R1
	RETURN

TEXT	getpagemask(SB),$0
	MOVW	M(PAGEMASK), R1
	RETURN

TEXT	setpagemask(SB),$0
	MOVW	R1, M(PAGEMASK)
	MOVW	R0, R1			/* prevent accidents */
	RETURN

TEXT	puttlbx(SB), $0	/* puttlbx(index, virt, phys0, phys1, pagemask) */
	MOVW	4(FP), R2
	MOVW	8(FP), R3
	MOVW	12(FP), R4
	MOVW	$((2*BY2PG-1) & ~0x1fff), R5
	MOVW	R2, M(TLBVIRT)
	MOVW	R3, M(TLBPHYS0)
	MOVW	R4, M(TLBPHYS1)
	MOVW	R5, M(PAGEMASK)
	MOVW	R1, M(INDEX)
	NOOP
	NOOP
	TLBWI
	NOOP
	RETURN

TEXT	tlbvirt(SB), $0
	MOVW	M(TLBVIRT), R1
	NOOP
	RETURN

TEXT	gettlbx(SB), $0			/* gettlbx(index, &entry) */
	MOVW	4(FP), R4
	MOVW	R1, M(INDEX)
	NOOP
	NOOP
	TLBR
	NOOP
	NOOP
	NOOP
	MOVW	M(TLBVIRT), R1
	MOVW	M(TLBPHYS0), R2
	MOVW	M(TLBPHYS1), R3
	NOOP
	MOVW	R1, 0(R4)
	MOVW	R2, 4(R4)
	MOVW	R3, 8(R4)
	RETURN

TEXT	gettlbp(SB), $0			/* gettlbp(tlbvirt, &entry) */
	MOVW	4(FP), R5
	MOVW	R1, M(TLBVIRT)
	NOOP
	NOOP
	NOOP
	TLBP
	NOOP
	NOOP
	MOVW	M(INDEX), R1
	NOOP
	BLTZ	R1, gettlbp1
	TLBR
	NOOP
	NOOP
	NOOP
	MOVW	M(TLBVIRT), R2
	MOVW	M(TLBPHYS0), R3
	MOVW	M(TLBPHYS1), R4
	NOOP
	MOVW	R2, 0(R5)
	MOVW	R3, 4(R5)
	MOVW	R4, 8(R5)
gettlbp1:
	RETURN

TEXT	gettlbvirt(SB), $0		/* gettlbvirt(index) */
	MOVW	R1, M(INDEX)
	NOOP
	NOOP
	TLBR
	NOOP
	NOOP
	NOOP
	MOVW	M(TLBVIRT), R1
	NOOP
	RETURN

/*
 * compute stlb hash index.
 *
 * M(TLBVIRT) [page & asid] in arg, result in arg.
 * stir in swizzled asid; we get best results with asid in both high & low bits.
 */
#define STLBHASH(arg, tmp)		\
	AND	$0xFF, arg, tmp;	\
	SRL	$(PGSHIFT+1), arg;	\
	XOR	tmp, arg;		\
	SLL	$(STLBLOG-8), tmp;	\
	XOR	tmp, arg;		\
	CONST	(STLBSIZE-1, tmp);	\
	AND	tmp, arg

TEXT	stlbhash(SB), $0		/* for mmu.c */
	STLBHASH(R1, R2)
	RETURN

TEXT	utlbmiss(SB), $-4
	GETMACH(R26)
	MOVW	R27, 12(R26)		/* m->splpc = R27 */

	MOVW	16(R26), R27
	ADDU	$1, R27
	MOVW	R27,16(R26)		/* m->tlbfault++ */

	MOVW	M(TLBVIRT), R27
	NOOP
	STLBHASH(R27, R26)

	/* scale to a byte index (multiply by 12) */
	SLL	$1, R27, R26		/* × 2 */
	ADDU	R26, R27		/* × 3 */
	SLL	$2, R27			/* × 12 */

	GETMACH(R26)
	MOVW	4(R26), R26
	ADDU	R26, R27		/* R27 = &m->stb[hash] */

	MOVW	M(BADVADDR), R26
	NOOP
	AND	$BY2PG, R26

	BNE	R26, utlbodd		/* odd page? */
	NOOP

utlbeven:
	MOVW	4(R27), R26		/* R26 = m->stb[hash].phys0 */
	BEQ	R26, stlbm		/* nothing cached? do it the hard way */
	NOOP
	MOVW	R26, M(TLBPHYS0)
	MOVW	8(R27), R26		/* R26 = m->stb[hash].phys1 */
	JMP	utlbcom
	MOVW	R26, M(TLBPHYS1)	/* branch delay slot */

utlbodd:
	MOVW	8(R27), R26		/* R26 = m->stb[hash].phys1 */
	BEQ	R26, stlbm		/* nothing cached? do it the hard way */
	NOOP
	MOVW	R26, M(TLBPHYS1)
	MOVW	4(R27), R26		/* R26 = m->stb[hash].phys0 */
	MOVW	R26, M(TLBPHYS0)

utlbcom:
	EHB
	MOVW	M(TLBVIRT), R26
	MOVW	0(R27), R27		/* R27 = m->stb[hash].virt */
	BEQ	R27, stlbm		/* nothing cached? do it the hard way */
	NOOP
	/* is the stlb entry for the right virtual address? */
	BNE	R26, R27, stlbm		/* M(TLBVIRT) != m->stb[hash].virt? */
	NOOP

	/* if an entry exists, overwrite it, else write a random one */
	CONST	(PGSZ, R27)
	MOVW	R27, M(PAGEMASK)	/* select page size */
	TLBP				/* probe tlb */
	NOOP
	NOOP
	MOVW	M(INDEX), R26
	NOOP
	BGEZ	R26, utlbindex		/* if tlb entry found, rewrite it */
	NOOP
	MOVW	M(RANDOM), R26
	MOVW	R26, M(INDEX)
utlbindex:
	NOOP
	NOOP
	TLBWI				/* write indexed tlb entry */
	NOOP

utlbret:
	GETMACH(R26)
	MOVW	12(R26), R27		/* R27 = m->splpc */
	MOVW	M(EPC), R26
	JMP	(R27)
	NOOP

stlbm:
	GETMACH(R26)
	MOVW	12(R26), R27		/* R27 = m->splpc */

	/* fall through */

TEXT	gevector(SB), $-4
	MOVW	M(STATUS), R26
	NOOP
	AND	$KUSER, R26

	BNE	R26, wasuser
	MOVW	SP, R26			/* delay slot, old SP in R26 */

waskernel:
	JMP	dosave
	SUBU	$UREGSIZE, SP		/* delay slot, allocate frame on kernel stack */

wasuser:				/* get kernel stack for this user process */
	GETMACH	(SP)
	MOVW	8(SP), SP		/*  m->proc */
	SUBU	$(UREGSIZE), SP

dosave:
	MOVW	R31, 0x28(SP)

	JAL	saveregs(SB)
	MOVW	R26, 0x10(SP)		/* delay slot, save old SP */

	GETMACH(R(MACH))
	MOVW	8(R(MACH)), R(USER)	/* R24 = m->proc */
	NOOP
	MOVW	$setR30(SB), R30

	BEQ	R26, dosys		/* set by saveregs() */
	NOOP

dotrap:
	MOVW	$forkret(SB), R31
	JMP	trap(SB)
	MOVW	4(SP), R1		/* delay slot, first arg to trap() */

dosys:
	JAL	syscall(SB)
	MOVW	4(SP), R1		/* delay slot, first arg to syscall() */

	/* fall through */

TEXT	forkret(SB), $-4
	JAL	restregs(SB)		/* restores old PC in R26 */
	MOVW	0x14(SP), R1		/* delay slot, CAUSE */

	MOVW	0x28(SP), R31

	JMP	(R27)
	MOVW	0x10(SP), SP		/* delay slot */

/*
 * SP->	0x00	--- (spill R31)
 *	0x04	--- (trap()/syscall() arg1)
 *	0x08	status
 *	0x0C	pc
 *	0x10	sp/usp
 *	0x14	cause
 *	0x18	badvaddr
 *	0x1C	tlbvirt
 *	0x20	hi
 *	0x24	lo
 *	0x28	r31
 *	.....
 *	0x9c	r1
 */

TEXT	saveregs(SB), $-4
	MOVW	R1, 0x9C(SP)
	MOVW	R2, 0x98(SP)
	MOVW	M(STATUS), R2
	ADDU	$8, SP, R1
	MOVW	R1, 0x04(SP)		/* arg to base of regs */
	MOVW	$~KMODEMASK, R1
	AND	R2, R1
	MOVW	R1, M(STATUS)		/* so we can take another trap */
	MOVW	R2, 0x08(SP)
	MOVW	M(EPC), R2
	MOVW	M(CAUSE), R1
	MOVW	R2, 0x0C(SP)
	MOVW	R1, 0x14(SP)
	AND	$(EXCMASK<<2), R1
	SUBU	$(CSYS<<2), R1, R26

	BEQ	R26, notsaved		/* is syscall? */
	MOVW	R27, 0x34(SP)		/* delay slot */

	MOVW	M(BADVADDR), R1
	MOVW	M(TLBVIRT), R2
	MOVW	R1, 0x18(SP)
	MOVW	R2, 0x1C(SP)

	MOVW	HI, R1
	MOVW	LO, R2
	MOVW	R1, 0x20(SP)
	MOVW	R2, 0x24(SP)

	MOVW	R25, 0x3C(SP)
	MOVW	R24, 0x40(SP)
	MOVW	R23, 0x44(SP)
	MOVW	R22, 0x48(SP)
	MOVW	R21, 0x4C(SP)
	MOVW	R20, 0x50(SP)
	MOVW	R19, 0x54(SP)
	MOVW	R18, 0x58(SP)
	MOVW	R17, 0x5C(SP)
	MOVW	R16, 0x60(SP)
	MOVW	R15, 0x64(SP)
	MOVW	R14, 0x68(SP)
	MOVW	R13, 0x6C(SP)
	MOVW	R12, 0x70(SP)
	MOVW	R11, 0x74(SP)
	MOVW	R10, 0x78(SP)
	MOVW	R9, 0x7C(SP)
	MOVW	R8, 0x80(SP)
	MOVW	R7, 0x84(SP)
	MOVW	R6, 0x88(SP)
	MOVW	R5, 0x8C(SP)
	MOVW	R4, 0x90(SP)
	MOVW	R3, 0x94(SP)

notsaved:
	MOVW	R30, 0x2C(SP)

	RET
	MOVW	R28, 0x30(SP)		/* delay slot */

TEXT	restregs(SB), $-4
	AND	$(EXCMASK<<2), R1
	SUBU	$(CSYS<<2), R1, R26

	BEQ	R26, notrestored	/* is syscall? */
	MOVW	0x34(SP), R27		/* delay slot */

	MOVW	0x3C(SP), R25
	MOVW	0x40(SP), R24
	MOVW	0x44(SP), R23
	MOVW	0x48(SP), R22
	MOVW	0x4C(SP), R21
	MOVW	0x50(SP), R20
	MOVW	0x54(SP), R19
	MOVW	0x58(SP), R18
	MOVW	0x5C(SP), R17
	MOVW	0x60(SP), R16
	MOVW	0x64(SP), R15
	MOVW	0x68(SP), R14
	MOVW	0x6C(SP), R13
	MOVW	0x70(SP), R12
	MOVW	0x74(SP), R11
	MOVW	0x78(SP), R10
	MOVW	0x7C(SP), R9
	MOVW	0x80(SP), R8
	MOVW	0x84(SP), R7
	MOVW	0x88(SP), R6
	MOVW	0x8C(SP), R5
	MOVW	0x90(SP), R4
	MOVW	0x94(SP), R3

	MOVW	0x24(SP), R2
	MOVW	0x20(SP), R1
	MOVW	R2, LO
	MOVW	R1, HI

	MOVW	0x98(SP), R2

notrestored:
	MOVW	0x08(SP), R1
	MOVW	R1, M(STATUS)
	MOVW	0x0C(SP), R26		/* old PC */
	MOVW	R26, M(EPC)

	MOVW	0x30(SP), R28
	MOVW	0x2C(SP), R30

	RET
	MOVW	0x9C(SP), R1		/* delay slot */

/*
 * hardware interrupt vectors
 */

TEXT	vector0(SB), $-4
	NOOP
	CONST	(SPBADDR+0x18, R26)
	MOVW	$eret(SB), R27
	MOVW	(R26), R26
	JMP	(R26)
	NOOP

TEXT	vector180(SB), $-4
	NOOP
	CONST	(SPBADDR+0x14, R26)
	MOVW	$eret(SB), R27
	MOVW	(R26), R26
	JMP	(R26)
	NOOP

TEXT	eret(SB), $-4		
	ERET
	NOOP

/*
 *  floating-point stuff
 */

/*
 * degenerate floating-point stuff ad9!
 */

TEXT	clrfpintr(SB), $0
	RETURN

TEXT	savefpregs(SB), $0
	RETURN

TEXT	restfpregs(SB), $0
	RETURN

TEXT	fcr31(SB), $0			/* fp csr */
	MOVW	R0, R1
	RETURN



/*
 * Emulate 68020 test and set: load linked / store conditional
 */

TEXT	tas(SB), $0
TEXT	_tas(SB), $0
	MOVW	R1, R2		/* address of key */
tas1:
	MOVW	$1, R3
	LL(2, 1)
	NOOP
	SC(2, 3)
	NOOP
	BEQ	R3, tas1
	NOOP
	RETURN

/* used by the semaphore implementation */
TEXT cmpswap(SB), $0
	MOVW	R1, R2		/* address of key */
	MOVW	old+4(FP), R3	/* old value */
	MOVW	new+8(FP), R4	/* new value */
	LL(2, 1)		/* R1 = (R2) */
	NOOP
	BNE	R1, R3, fail
	NOOP
	MOVW	R4, R1
	SC(2, 1)	/* (R2) = R1 if (R2) hasn't changed; R1 = success */
	NOOP
	RETURN
fail:
	MOVW	R0, R1
	RETURN

/*
 *  cache manipulation
 */

TEXT	icflush(SB), $-4			/* icflush(virtaddr, count) */
	MOVW	4(FP), R9
	DI(10)						/* intrs off, old status -> R10 */
	UBARRIERS(7, R7, ichb);		/* return to kseg1 (uncached) */
	ADDU	R1, R9			/* R9 = last address */
	MOVW	$(~0x3f), R8
	AND	R1, R8			/* R8 = first address, rounded down */
	ADDU	$0x3f, R9
	AND	$(~0x3f), R9		/* round last address up */
	SUBU	R8, R9			/* R9 = revised count */
icflush1:			/* primary cache line size is 16 bytes */
	CACHE	PD+HWB, 0x00(R8)
	CACHE	PI+HINV, 0x00(R8)
	CACHE	PD+HWB, 0x10(R8)
	CACHE	PI+HINV, 0x10(R8)
	CACHE	PD+HWB, 0x20(R8)
	CACHE	PI+HINV, 0x20(R8)
	CACHE	PD+HWB, 0x30(R8)
	CACHE	PI+HINV, 0x30(R8)
	SUBU	$0x40, R9
	BGTZ	R9, icflush1
	ADDU	$0x40, R8			/* delay slot */
	BARRIERS(7, R7, ic2hb);		/* return to kseg0 (cached) */
	MOVW	R10, M(STATUS)
	JRHB(31)

TEXT	dcflush(SB), $-4			/* dcflush(virtaddr, count) */
	MOVW	4(FP), R9
	DI(10)						/* intrs off, old status -> R10 */
	SYNC
	EHB
	ADDU	R1, R9			/* R9 = last address */
	MOVW	$(~0x3f), R8
	AND	R1, R8			/* R8 = first address, rounded down */
	ADDU	$0x3f, R9
	AND	$(~0x3f), R9		/* round last address up */
	SUBU	R8, R9			/* R9 = revised count */
dcflush1:			/* primary cache line size is 16 bytes */
	CACHE	PD+HWB, 0x00(R8)
	CACHE	PD+HWB, 0x10(R8)
	CACHE	PD+HWB, 0x20(R8)
	CACHE	PD+HWB, 0x30(R8)
	SUBU	$0x40, R9
	BGTZ	R9, dcflush1
	ADDU	$0x40, R8			/* delay slot */
	SYNC
	EHB
	MOVW	R10, M(STATUS)
	RETURN

TEXT	outl(SB), $0
	MOVW	4(FP), R2
	MOVW	8(FP), R3
	SLL	$2, R3
	ADDU	R2, R3
outl1:
	BEQ	R2, R3, outl2
	MOVW	(R2), R4
	MOVW	R4, (R1)
	JMP	outl1
	ADDU	$4, R2
outl2:
	RETURN

/*
 * access to CP0 registers
 */

TEXT	prid(SB), $0
	MOVW	M(PRID), R1
	RETURN

TEXT	rdcount(SB), $0
	MOVW	M(COUNT), R1
	RETURN

TEXT	wrcount(SB), $0
	MOVW	R1, M(COUNT)
	RETURN

TEXT	wrcompare(SB), $0
	MOVW	R1, M(COMPARE)
	RETURN

TEXT	rdcompare(SB), $0
	MOVW	M(COMPARE), R1
	RETURN

TEXT	getstatus(SB), $0
	MOVW	M(STATUS), R1
	RETURN

TEXT	setstatus(SB), $0
	MOVW	R1, M(STATUS)
	EHB
	RETURN

TEXT	getcause(SB), $-4
	MOVW	M(CAUSE), R1
	RETURN

TEXT	getconfig(SB), $-4
	MOVW	M(CONFIG), R1
	RETURN

TEXT	getconfig1(SB), $-4
	MFC0(CONFIG, 1, 1)
	RETURN

TEXT	getconfig2(SB), $-4
	MFC0(CONFIG, 2, 1)
	RETURN

TEXT	getconfig3(SB), $-4
	MFC0(CONFIG, 3, 1)
	RETURN

TEXT	getconfig4(SB), $-4
	MFC0(CONFIG, 4, 1)
	RETURN

TEXT	getconfig7(SB), $-4
	MFC0(CONFIG, 7, 1)
	RETURN

TEXT	gethwreg3(SB), $-4
	RDHWR(3, 1)
	RETURN

TEXT	getdebugreg(SB), $0
	MOVW	M(DEBUGREG), R1
	RETURN

TEXT	setwatchhi0(SB), $0
	MOVW	R1, M(WATCHHI)
	EHB
	RETURN

/*
 * beware that the register takes a double-word address, so it's not
 * precise to the individual instruction.
 */
TEXT	setwatchlo0(SB), $0
	MOVW	R1, M(WATCHLO)
	EHB
	RETURN

TEXT	getfcr0(SB), $0
	MOVW	FCR0, R1
	RET

/* zoot is just for debug waves */
TEXT	zoot(SB), $0
	PUTC('W', R1, R2)
	NOP
	RETURN

	GLOBL	sanity(SB), $4
	DATA	sanity(SB)/4, $SANITY

	SCHED
