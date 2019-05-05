#include "mem.h"
#include "sysreg.h"

#undef	SYSREG
#define	SYSREG(op0,op1,Cn,Cm,op2)	SPR(((op0)<<19|(op1)<<16|(Cn)<<12|(Cm)<<8|(op2)<<5))

TEXT _start(SB), 1, $-4
	MOV	$setSB-KZERO(SB), R28
	BL	svcmode<>(SB)

	/* use dedicated stack pointer per exception level */
	MOVWU	$1, R1
	MSR	R1, SPSel

	BL	mmudisable<>(SB)

	/* invalidate local caches */
	BL	cachedinv(SB)
	BL	cacheiinv(SB)

	MOV	$(MACHADDR(0)-KZERO), R27
	MRS	MPIDR_EL1, R1
	ANDW	$(MAXMACH-1), R1
	MOVWU	$MACHSIZE, R2
	MULW	R1, R2, R2
	SUB	R2, R27

	ADD	$(MACHSIZE-16), R27, R2
	MOV	R2, SP

	CBNZ	R1, _startup

	/* clear page table and machs */
	MOV	$(L1-KZERO), R1
	MOV	$(MACHADDR(-1)-KZERO), R2
_zerol1:
	MOV	ZR, (R1)8!
	CMP	R1, R2
	BNE	_zerol1

	/* clear BSS */
	MOV	$edata-KZERO(SB), R1
	MOV	$end-KZERO(SB), R2
_zerobss:
	MOV	ZR, (R1)8!
	CMP	R1, R2
	BNE	_zerobss

	/* setup page tables */
	MOV	$(L1-KZERO), R0
	BL	mmu0init(SB)

	BL	cachedwbinv(SB)
	BL	l2cacheuwbinv(SB)
	SEVL
_startup:
	WFE
	BL	mmuenable<>(SB)

	MOV	$0, R26
	ORR	$KZERO, R27
	MSR	R27, TPIDR_EL1
	MOV	$setSB(SB), R28

	BL	main(SB)

TEXT	stop<>(SB), 1, $-4
_stop:
	WFE
	B	_stop

TEXT sev(SB), 1, $-4
	SEV
	WFE
	RETURN

TEXT PUTC(SB), 1, $-4
	MOVWU $(0x3F000000+0x215040), R14
	MOVB R0, (R14)
	RETURN

TEXT svcmode<>(SB), 1, $-4
	MSR	$0xF, DAIFSet
	MRS	CurrentEL, R0
	ANDW	$(3<<2), R0
	CMPW	$(1<<2), R0
	BEQ	el1
	CMPW	$(2<<2), R0
	BEQ	el2
	B	stop<>(SB)
el2:
	MOV	$0, R0
	MSR	R0, MDCR_EL2
	ISB	$SY

	/* HCR = RW, HCD, SWIO, BSU, FB */
	MOVWU	$(1<<31 | 1<<29 | 1<<2 | 0<<10 | 0<<9), R0
	MSR	R0, HCR_EL2
	ISB	$SY

	/* SCTLR = RES1 */
	MOVWU	$(3<<4 | 1<<11 | 1<<16 | 1<<18 | 3<<22 | 3<<28), R0
	ISB	$SY
	MSR	R0, SCTLR_EL2
	ISB	$SY

	/* set VMID to zero */
	MOV	$0, R0
	MSR	R0, VTTBR_EL2
	ISB	$SY

	MOVWU	$(0xF<<6 | 4), R0
	MSR	R0, SPSR_EL2
	MSR	LR, ELR_EL2
	ERET
el1:
	RETURN

TEXT mmudisable<>(SB), 1, $-4
#define SCTLRCLR \
	/* RES0 */	( 3<<30 \
	/* RES0 */	| 1<<27 \
	/* UCI */	| 1<<26 \
	/* EE */	| 1<<25 \
	/* RES0 */	| 1<<21 \
	/* E0E */	| 1<<24 \
	/* WXN */	| 1<<19 \
	/* nTWE */	| 1<<18 \
	/* RES0 */	| 1<<17 \
	/* nTWI */	| 1<<16 \
	/* UCT */	| 1<<15 \
	/* DZE */	| 1<<14 \
	/* RES0 */	| 1<<13 \
	/* RES0 */	| 1<<10 \
	/* UMA */	| 1<<9 \
	/* SA0 */	| 1<<4 \
	/* SA */	| 1<<3 \
	/* A */		| 1<<1 )
#define SCTLRSET \
	/* RES1 */	( 3<<28 \
	/* RES1 */	| 3<<22 \
	/* RES1 */	| 1<<20 \
	/* RES1 */	| 1<<11 )
#define SCTLRMMU \
	/* I */		( 1<<12 \
	/* C */		| 1<<2 \
	/* M */		| 1<<0 )

	/* initialise SCTLR, MMU and caches off */
	ISB	$SY
	MRS	SCTLR_EL1, R0
	BIC	$(SCTLRCLR | SCTLRMMU), R0
	ORR	$SCTLRSET, R0
	ISB	$SY
	MSR	R0, SCTLR_EL1
	ISB	$SY

	B	flushlocaltlb(SB)

TEXT mmuenable<>(SB), 1, $-4
	/* return to virtual */
	ORR	$KZERO, LR
	MOV	LR, -16(RSP)!

	BL	cachedwbinv(SB)
	BL	flushlocaltlb(SB)

	/* memory attributes */
#define MAIRINIT \
	( 0xFF << MA_MEM_WB*8 \
	| 0x33 << MA_MEM_WT*8 \
	| 0x44 << MA_MEM_UC*8 \
	| 0x00 << MA_DEV_nGnRnE*8 \
	| 0x04 << MA_DEV_nGnRE*8 \
	| 0x08 << MA_DEV_nGRE*8 \
	| 0x0C << MA_DEV_GRE*8 )
	MOV	$MAIRINIT, R1
	MSR	R1, MAIR_EL1
	ISB	$SY

	/* translation control */
#define TCRINIT \
	/* TBI1 */	( 0<<38 \
	/* TBI0 */	| 0<<37 \
	/* AS */	| 0<<36 \
	/* TG1 */	| (((3<<16|1<<14|2<<12)>>PGSHIFT)&3)<<30 \
	/* SH1 */	| SHARE_INNER<<28 \
	/* ORGN1 */	| CACHE_WB<<26 \
	/* IRGN1 */	| CACHE_WB<<24 \
	/* EPD1 */	| 0<<23 \
	/* A1 */	| 0<<22 \
	/* T1SZ */	| (64-EVASHIFT)<<16 \
	/* TG0 */	| (((1<<16|2<<14|0<<12)>>PGSHIFT)&3)<<14 \
	/* SH0 */	| SHARE_INNER<<12 \
	/* ORGN0 */	| CACHE_WB<<10 \
	/* IRGN0 */	| CACHE_WB<<8 \
	/* EPD0 */	| 0<<7 \
	/* T0SZ */	| (64-EVASHIFT)<<0 )
	MOV	$TCRINIT, R1
	MRS	ID_AA64MMFR0_EL1, R2
	ANDW	$0xF, R2	// IPS
	ADD	R2<<32, R1
	MSR	R1, TCR_EL1
	ISB	$SY

	/* load the page tables */
	MOV	$(L1TOP-KZERO), R0
	ISB	$SY
	MSR	R0, TTBR0_EL1
	MSR	R0, TTBR1_EL1
	ISB	$SY

	/* enable MMU and caches */
	MRS	SCTLR_EL1, R1
	ORR	$SCTLRMMU, R1
	ISB	$SY
	MSR	R1, SCTLR_EL1
	ISB	$SY

	MOV	RSP, R1
	ORR	$KZERO, R1
	MOV	R1, RSP
	MOV	(RSP)16!, LR
	B	cacheiinv(SB)

TEXT touser(SB), 1, $-4
	MSR	$0x3, DAIFSet	// interrupts off
	MOVWU	$0x10028, R1	// entry
	MOVWU	$0, R2		// psr
	MSR	R0, SP_EL0	// sp
	MSR	R1, ELR_EL1
	MSR	R2, SPSR_EL1
	ERET

TEXT cas(SB), 1, $-4
TEXT cmpswap(SB), 1, $-4
	MOVW	ov+8(FP), R1
	MOVW	nv+16(FP), R2
_cas1:
	LDXRW	(R0), R3
	CMP	R3, R1
	BNE	_cas0
	STXRW	R2, (R0), R4
	CBNZ	R4, _cas1
	MOVW	$1, R0
	DMB	$ISH
	RETURN
_cas0:
	CLREX
	MOVW	$0, R0
	RETURN

TEXT tas(SB), 1, $-4
TEXT _tas(SB), 1, $-4
	MOVW	$0xdeaddead, R2
_tas1:
	LDXRW	(R0), R1
	STXRW	R2, (R0), R3
	CBNZ	R3, _tas1
	MOVW	R1, R0

TEXT coherence(SB), 1, $-4
	DMB	$ISH
	RETURN

TEXT islo(SB), 1, $-4
	MRS	DAIF, R0
	AND	$(0x2<<6), R0
	EOR	$(0x2<<6), R0
	RETURN

TEXT splhi(SB), 1, $-4
	MRS	DAIF, R0
	MSR	$0x2, DAIFSet
	RETURN

TEXT splfhi(SB), 1, $-4
	MRS	DAIF, R0
	MSR	$0x3, DAIFSet
	RETURN

TEXT spllo(SB), 1, $-4
	MSR	$0x3, DAIFClr
	RETURN

TEXT splflo(SB), 1, $-4
	MSR	$0x1, DAIFClr
	RETURN

TEXT splx(SB), 1, $-4
	MSR	R0, DAIF
	RETURN

TEXT idlehands(SB), 1, $-4
	DMB	$ISH
	MOVW	nrdy(SB), R0
	CBNZ	R0, _ready
	WFI
_ready:
	RETURN

TEXT cycles(SB), 1, $-4
TEXT lcycles(SB), 1, $-4
	MRS	PMCCNTR_EL0, R0
	RETURN

TEXT setlabel(SB), 1, $-4
	MOV	LR, 8(R0)
	MOV	SP, R1
	MOV	R1, 0(R0)
	MOVW	$0, R0
	RETURN

TEXT gotolabel(SB), 1, $-4
	MOV	8(R0), LR	/* link */
	MOV	0(R0), R1	/* sp */
	MOV	R1, SP
	MOVW	$1, R0
	RETURN

TEXT returnto(SB), 1, $-4
	MOV	R0, 0(SP)
	RETURN

TEXT getfar(SB), 1, $-4
	MRS	FAR_EL1, R0
	RETURN

TEXT setttbr(SB), 1, $-4
	DSB	$ISHST
	MSR	R0, TTBR0_EL1
	DSB	$ISH
	ISB	$SY

	B	cacheiinv(SB)

TEXT magic(SB), 1, $-4
	DSB	$SY
	ISB	$SY
	DSB	$SY
	ISB	$SY
	DSB	$SY
	ISB	$SY
	DSB	$SY
	ISB	$SY
	RETURN

/*
 * TLB maintenance operations.
 * these broadcast to all cpu's in the cluser
 * (inner sharable domain).
 */
TEXT flushasidva(SB), 1, $-4
TEXT tlbivae1is(SB), 1, $-4
	DSB	$ISHST
	TLBI	R0, 0,8,3,1	/* VAE1IS */
	DSB	$ISH
	ISB	$SY
	RETURN

TEXT flushasidvall(SB), 1, $-4
TEXT tlbivale1is(SB), 1, $-4
	DSB	$ISHST
	TLBI	R0, 0,8,3,5	/* VALE1IS */
	DSB	$ISH
	ISB	$SY
	RETURN

TEXT flushasid(SB), 1, $-4
TEXT tlbiaside1is(SB), 1, $-4
	DSB	$ISHST
	TLBI	R0, 0,8,3,2	/* ASIDE1IS */
	DSB	$ISH
	ISB	$SY
	RETURN

TEXT flushtlb(SB), 1, $-4
TEXT tlbivmalle1is(SB), 1, $-4
	DSB	$ISHST
	TLBI	R0, 0,8,3,0	/* VMALLE1IS */
	DSB	$ISH
	ISB	$SY
	RETURN

/*
 * flush the tlb of this cpu. no broadcast.
 */
TEXT flushlocaltlb(SB), 1, $-4
TEXT tlbivmalle1(SB), 1, $-4
	DSB	$NSHST
	TLBI	R0, 0,8,7,0	/* VMALLE1 */
	DSB	$NSH
	ISB	$SY
	RETURN

TEXT fpsaveregs(SB), 1, $-4
	WORD	$(1<<30 | 3 << 26 | 2<<22 | 0x1F<<16 | 3<<10 | 0<<5 | 0)  /* MOV { V0, V1, V2, V3  }, (R0)64! */
	WORD	$(1<<30 | 3 << 26 | 2<<22 | 0x1F<<16 | 3<<10 | 0<<5 | 4)  /* MOV { V4, V5, V6, V7  }, (R0)64! */
	WORD	$(1<<30 | 3 << 26 | 2<<22 | 0x1F<<16 | 3<<10 | 0<<5 | 8)  /* MOV { V8, V9, V10,V11 }, (R0)64! */
	WORD	$(1<<30 | 3 << 26 | 2<<22 | 0x1F<<16 | 3<<10 | 0<<5 | 12) /* MOV { V12,V13,V14,V15 }, (R0)64! */
	WORD	$(1<<30 | 3 << 26 | 2<<22 | 0x1F<<16 | 3<<10 | 0<<5 | 16) /* MOV { V16,V17,V18,V19 }, (R0)64! */
	WORD	$(1<<30 | 3 << 26 | 2<<22 | 0x1F<<16 | 3<<10 | 0<<5 | 20) /* MOV { V20,V21,V22,V23 }, (R0)64! */
	WORD	$(1<<30 | 3 << 26 | 2<<22 | 0x1F<<16 | 3<<10 | 0<<5 | 24) /* MOV { V24,V25,V26,V27 }, (R0)64! */
	WORD	$(1<<30 | 3 << 26 | 2<<22 | 0x1F<<16 | 3<<10 | 0<<5 | 28) /* MOV { V28,V29,V30,V31 }, (R0)64! */
	RETURN

TEXT fploadregs(SB), 1, $-4
	WORD	$(1<<30 | 3 << 26 | 3<<22 | 0x1F<<16 | 3<<10 | 0<<5 | 0)  /* MOV (R0)64!, { V0, V1, V2, V3  } */
	WORD	$(1<<30 | 3 << 26 | 3<<22 | 0x1F<<16 | 3<<10 | 0<<5 | 4)  /* MOV (R0)64!, { V4, V5, V6, V7  } */
	WORD	$(1<<30 | 3 << 26 | 3<<22 | 0x1F<<16 | 3<<10 | 0<<5 | 8)  /* MOV (R0)64!, { V8, V9, V10,V11 } */
	WORD	$(1<<30 | 3 << 26 | 3<<22 | 0x1F<<16 | 3<<10 | 0<<5 | 12) /* MOV (R0)64!, { V12,V13,V14,V15 } */
	WORD	$(1<<30 | 3 << 26 | 3<<22 | 0x1F<<16 | 3<<10 | 0<<5 | 16) /* MOV (R0)64!, { V16,V17,V18,V19 } */
	WORD	$(1<<30 | 3 << 26 | 3<<22 | 0x1F<<16 | 3<<10 | 0<<5 | 20) /* MOV (R0)64!, { V20,V21,V22,V23 } */
	WORD	$(1<<30 | 3 << 26 | 3<<22 | 0x1F<<16 | 3<<10 | 0<<5 | 24) /* MOV (R0)64!, { V24,V25,V26,V27 } */
	WORD	$(1<<30 | 3 << 26 | 3<<22 | 0x1F<<16 | 3<<10 | 0<<5 | 28) /* MOV (R0)64!, { V28,V29,V30,V31 } */
	RETURN

// syscall or trap from EL0
TEXT vsys0(SB), 1, $-4
	LSRW	$26, R0, R17	// ec
	CMPW	$0x15, R17	// SVC trap?
	BNE	_itsatrap	// nope.

	MOV	R26, 224(RSP)	// special
	MOV	R27, 232(RSP)	// special
	MOV	R28, 240(RSP)	// sb
	MOV	R29, 248(RSP)	// special

	MRS	SP_EL0, R1
	MRS	ELR_EL1, R2
	MRS	SPSR_EL1, R3

	MOV	R0, 288(RSP)	// type
	MOV	R1, 264(RSP)	// sp
	MOV	R2, 272(RSP)	// pc
	MOV	R3, 280(RSP)	// psr

	MOV	$setSB(SB), R28
	MRS	TPIDR_EL1, R27
	MOV	16(R27), R26

	ADD	$16, RSP, R0	// ureg
	BL	syscall(SB)

TEXT forkret(SB), 1, $-4
	MSR	$0x3, DAIFSet	// interrupts off

	ADD	$16, RSP, R0	// ureg

	MOV	16(RSP), R0	// ret
	MOV	264(RSP), R1	// sp
	MOV	272(RSP), R2	// pc
	MOV	280(RSP), R3	// psr

	MSR	R1, SP_EL0
	MSR	R2, ELR_EL1
	MSR	R3, SPSR_EL1

	MOV	224(RSP), R26	// special
	MOV	232(RSP), R27	// special
	MOV	240(RSP), R28	// sb
	MOV	248(RSP), R29	// special

	MOV	256(RSP), R30	// link

	ADD	$TRAPFRAMESIZE, RSP
	ERET

TEXT itsatrap<>(SB), 1, $-4
_itsatrap:
	MOV	R1, 24(RSP)
	MOV	R2, 32(RSP)
	MOV	R3, 40(RSP)
	MOV	R4, 48(RSP)
	MOV	R5, 56(RSP)
	MOV	R6, 64(RSP)
	MOV	R7, 72(RSP)
	MOV	R8, 80(RSP)
	MOV	R9, 88(RSP)
	MOV	R10, 96(RSP)
	MOV	R11, 104(RSP)
	MOV	R12, 112(RSP)
	MOV	R13, 120(RSP)
	MOV	R14, 128(RSP)
	MOV	R15, 136(RSP)
	MOV	R16, 144(RSP)

	MOV	R18, 160(RSP)
	MOV	R19, 168(RSP)
	MOV	R20, 176(RSP)
	MOV	R21, 184(RSP)
	MOV	R22, 192(RSP)
	MOV	R23, 200(RSP)
	MOV	R24, 208(RSP)
	MOV	R25, 216(RSP)

// trap/irq/fiq/serr from EL0
TEXT vtrap0(SB), 1, $-4
	MOV	R26, 224(RSP)	// special
	MOV	R27, 232(RSP)	// special
	MOV	R28, 240(RSP)	// sb
	MOV	R29, 248(RSP)	// special

	MRS	SP_EL0, R1
	MRS	ELR_EL1, R2
	MRS	SPSR_EL1, R3

	MOV	R0, 288(RSP)	// type
	MOV	R1, 264(RSP)	// sp
	MOV	R2, 272(RSP)	// pc
	MOV	R3, 280(RSP)	// psr

	MOV	$setSB(SB), R28
	MRS	TPIDR_EL1, R27
	MOV	16(R27), R26

	ADD	$16, RSP, R0	// ureg
	BL	trap(SB)

TEXT noteret(SB), 1, $-4
	MSR	$0x3, DAIFSet	// interrupts off

	ADD	$16, RSP, R0	// ureg

	MOV	264(RSP), R1	// sp
	MOV	272(RSP), R2	// pc
	MOV	280(RSP), R3	// psr

	MSR	R1, SP_EL0
	MSR	R2, ELR_EL1
	MSR	R3, SPSR_EL1

	MOV	224(RSP), R26	// special
	MOV	232(RSP), R27	// special
	MOV	240(RSP), R28	// sb
	MOV	248(RSP), R29	// special

_intrreturn:
	MOV	16(RSP), R0
	MOV	24(RSP), R1
	MOV	32(RSP), R2
	MOV	40(RSP), R3
	MOV	48(RSP), R4
	MOV	56(RSP), R5
	MOV	64(RSP), R6
	MOV	72(RSP), R7
	MOV	80(RSP), R8
	MOV	88(RSP), R9
	MOV	96(RSP), R10
	MOV	104(RSP), R11
	MOV	112(RSP), R12
	MOV	120(RSP), R13
	MOV	128(RSP), R14
	MOV	136(RSP), R15
	MOV	144(RSP), R16
	MOV	152(RSP), R17
	MOV	160(RSP), R18
	MOV	168(RSP), R19
	MOV	176(RSP), R20
	MOV	184(RSP), R21
	MOV	192(RSP), R22
	MOV	200(RSP), R23
	MOV	208(RSP), R24
	MOV	216(RSP), R25

	MOV	256(RSP), R30	// link

	ADD	$TRAPFRAMESIZE, RSP
	ERET

// irq/fiq/trap/serr from EL1
TEXT vtrap1(SB), 1, $-4
	MOV	R29, 248(RSP)	// special

	ADD	$TRAPFRAMESIZE, RSP, R1
	MRS	ELR_EL1, R2
	MRS	SPSR_EL1, R3

	MOV	R0, 288(RSP)	// type
	MOV	R1, 264(RSP)	// sp
	MOV	R2, 272(RSP)	// pc
	MOV	R3, 280(RSP)	// psr

	ADD	$16, RSP, R0	// ureg
	BL	trap(SB)

	MSR	$0x3, DAIFSet	// interrupts off

	MOV	272(RSP), R2	// pc
	MOV	280(RSP), R3	// psr

	MSR	R2, ELR_EL1
	MSR	R3, SPSR_EL1

	MOV	248(RSP), R29	// special
	B	_intrreturn	

// vector tables
TEXT vsys(SB), 1, $-4
	SUB	$TRAPFRAMESIZE, RSP

	MOV	R0, 16(RSP)
	MOV	R30, 256(RSP)	// link

	MOV	R17, 152(RSP)	// temp

	MRS	ESR_EL1, R0	// type

_vsyspatch:
	B	_vsyspatch	// branch to vsys0() patched in

TEXT vtrap(SB), 1, $-4
	SUB	$TRAPFRAMESIZE, RSP

	MOV	R0, 16(RSP)
	MOV	R1, 24(RSP)
	MOV	R2, 32(RSP)
	MOV	R3, 40(RSP)
	MOV	R4, 48(RSP)
	MOV	R5, 56(RSP)
	MOV	R6, 64(RSP)
	MOV	R7, 72(RSP)
	MOV	R8, 80(RSP)
	MOV	R9, 88(RSP)
	MOV	R10, 96(RSP)
	MOV	R11, 104(RSP)
	MOV	R12, 112(RSP)
	MOV	R13, 120(RSP)
	MOV	R14, 128(RSP)
	MOV	R15, 136(RSP)
	MOV	R16, 144(RSP)
	MOV	R17, 152(RSP)
	MOV	R18, 160(RSP)
	MOV	R19, 168(RSP)
	MOV	R20, 176(RSP)
	MOV	R21, 184(RSP)
	MOV	R22, 192(RSP)
	MOV	R23, 200(RSP)
	MOV	R24, 208(RSP)
	MOV	R25, 216(RSP)

	MOV	R30, 256(RSP)	// link

	MRS	ESR_EL1, R0	// type

_vtrappatch:
	B	_vtrappatch	// branch to vtrapX() patched in

TEXT virq(SB), 1, $-4
	SUB	$TRAPFRAMESIZE, RSP

	MOV	R0, 16(RSP)
	MOV	R1, 24(RSP)
	MOV	R2, 32(RSP)
	MOV	R3, 40(RSP)
	MOV	R4, 48(RSP)
	MOV	R5, 56(RSP)
	MOV	R6, 64(RSP)
	MOV	R7, 72(RSP)
	MOV	R8, 80(RSP)
	MOV	R9, 88(RSP)
	MOV	R10, 96(RSP)
	MOV	R11, 104(RSP)
	MOV	R12, 112(RSP)
	MOV	R13, 120(RSP)
	MOV	R14, 128(RSP)
	MOV	R15, 136(RSP)
	MOV	R16, 144(RSP)
	MOV	R17, 152(RSP)
	MOV	R18, 160(RSP)
	MOV	R19, 168(RSP)
	MOV	R20, 176(RSP)
	MOV	R21, 184(RSP)
	MOV	R22, 192(RSP)
	MOV	R23, 200(RSP)
	MOV	R24, 208(RSP)
	MOV	R25, 216(RSP)

	MOV	R30, 256(RSP)	// link

	MOV	$(1<<32), R0	// type irq

_virqpatch:
	B	_virqpatch	// branch to vtrapX() patched in

TEXT vfiq(SB), 1, $-4
	SUB	$TRAPFRAMESIZE, RSP

	MOV	R0, 16(RSP)
	MOV	R1, 24(RSP)
	MOV	R2, 32(RSP)
	MOV	R3, 40(RSP)
	MOV	R4, 48(RSP)
	MOV	R5, 56(RSP)
	MOV	R6, 64(RSP)
	MOV	R7, 72(RSP)
	MOV	R8, 80(RSP)
	MOV	R9, 88(RSP)
	MOV	R10, 96(RSP)
	MOV	R11, 104(RSP)
	MOV	R12, 112(RSP)
	MOV	R13, 120(RSP)
	MOV	R14, 128(RSP)
	MOV	R15, 136(RSP)
	MOV	R16, 144(RSP)
	MOV	R17, 152(RSP)
	MOV	R18, 160(RSP)
	MOV	R19, 168(RSP)
	MOV	R20, 176(RSP)
	MOV	R21, 184(RSP)
	MOV	R22, 192(RSP)
	MOV	R23, 200(RSP)
	MOV	R24, 208(RSP)
	MOV	R25, 216(RSP)

	MOV	R30, 256(RSP)	// link
	MOV	$(2<<32), R0	// type fiq

_vfiqpatch:
	B	_vfiqpatch	// branch to vtrapX() patched in

TEXT vserr(SB), 1, $-4
	SUB	$TRAPFRAMESIZE, RSP

	MOV	R0, 16(RSP)
	MOV	R1, 24(RSP)
	MOV	R2, 32(RSP)
	MOV	R3, 40(RSP)
	MOV	R4, 48(RSP)
	MOV	R5, 56(RSP)
	MOV	R6, 64(RSP)
	MOV	R7, 72(RSP)
	MOV	R8, 80(RSP)
	MOV	R9, 88(RSP)
	MOV	R10, 96(RSP)
	MOV	R11, 104(RSP)
	MOV	R12, 112(RSP)
	MOV	R13, 120(RSP)
	MOV	R14, 128(RSP)
	MOV	R15, 136(RSP)
	MOV	R16, 144(RSP)
	MOV	R17, 152(RSP)
	MOV	R18, 160(RSP)
	MOV	R19, 168(RSP)
	MOV	R20, 176(RSP)
	MOV	R21, 184(RSP)
	MOV	R22, 192(RSP)
	MOV	R23, 200(RSP)
	MOV	R24, 208(RSP)
	MOV	R25, 216(RSP)

	MOV	R30, 256(RSP)	// link

	MRS	ESR_EL1, R0
	ORR	$(3<<32), R0	// type
_vserrpatch:
	B	_vserrpatch	// branch to vtrapX() patched in
