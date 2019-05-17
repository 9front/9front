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
	ANDW	$0x7, R2	// PARange
	ADD	R2<<32, R1	// IPS
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
	MOV	$nrdy(SB), R1
	LDXRW	(R1), R0
	CBZ	R0, _goodnight
	CLREX
	SEVL
_goodnight:
	WFE
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

	MOVP	R26, R27, 224(RSP)
	MOVP	R28, R29, 240(RSP)

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

	MOVP	224(RSP), R26, R27
	MOVP	240(RSP), R28, R29

	MOV	256(RSP), R30	// link

	ADD	$TRAPFRAMESIZE, RSP
	ERET

TEXT itsatrap<>(SB), 1, $-4
_itsatrap:
	MOVP	R1, R2, 24(RSP)
	MOVP	R3, R4, 40(RSP)
	MOVP	R5, R6, 56(RSP)
	MOVP	R7, R8, 72(RSP)
	MOVP	R9, R10, 88(RSP)
	MOVP	R11, R12, 104(RSP)
	MOVP	R13, R14, 120(RSP)
	MOVP	R15, R16, 136(RSP)

	MOVP	R18, R19, 160(RSP)
	MOVP	R20, R21, 176(RSP)
	MOVP	R22, R23, 192(RSP)
	MOVP	R24, R25, 208(RSP)

// trap/irq/fiq/serr from EL0
TEXT vtrap0(SB), 1, $-4
	MOVP	R26, R27, 224(RSP)
	MOVP	R28, R29, 240(RSP)

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

	MOVP	224(RSP), R26, R27
	MOVP	240(RSP), R28, R29

_intrreturn:
	MOVP	16(RSP), R0, R1
	MOVP	32(RSP), R2, R3
	MOVP	48(RSP), R4, R5
	MOVP	64(RSP), R6, R7
	MOVP	80(RSP), R8, R9
	MOVP	96(RSP), R10, R11
	MOVP	112(RSP), R12, R13
	MOVP	128(RSP), R14, R15
	MOVP	144(RSP), R16, R17
	MOVP	160(RSP), R18, R19
	MOVP	176(RSP), R20, R21
	MOVP	192(RSP), R22, R23
	MOVP	208(RSP), R24, R25

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

	MOVP	R0, R1, 16(RSP)
	MOVP	R2, R3, 32(RSP)
	MOVP	R4, R5, 48(RSP)
	MOVP	R6, R7, 64(RSP)
	MOVP	R8, R9, 80(RSP)
	MOVP	R10, R11, 96(RSP)
	MOVP	R12, R13, 112(RSP)
	MOVP	R14, R15, 128(RSP)
	MOVP	R16, R17, 144(RSP)
	MOVP	R18, R19, 160(RSP)
	MOVP	R20, R21, 176(RSP)
	MOVP	R22, R23, 192(RSP)
	MOVP	R24, R25, 208(RSP)

	MOV	R30, 256(RSP)	// link

	MRS	ESR_EL1, R0	// type

_vtrappatch:
	B	_vtrappatch	// branch to vtrapX() patched in

TEXT virq(SB), 1, $-4
	SUB	$TRAPFRAMESIZE, RSP

	MOVP	R0, R1, 16(RSP)
	MOVP	R2, R3, 32(RSP)
	MOVP	R4, R5, 48(RSP)
	MOVP	R6, R7, 64(RSP)
	MOVP	R8, R9, 80(RSP)
	MOVP	R10, R11, 96(RSP)
	MOVP	R12, R13, 112(RSP)
	MOVP	R14, R15, 128(RSP)
	MOVP	R16, R17, 144(RSP)
	MOVP	R18, R19, 160(RSP)
	MOVP	R20, R21, 176(RSP)
	MOVP	R22, R23, 192(RSP)
	MOVP	R24, R25, 208(RSP)

	MOV	R30, 256(RSP)	// link

	MOV	$(1<<32), R0	// type irq

_virqpatch:
	B	_virqpatch	// branch to vtrapX() patched in

TEXT vfiq(SB), 1, $-4
	SUB	$TRAPFRAMESIZE, RSP

	MOVP	R0, R1, 16(RSP)
	MOVP	R2, R3, 32(RSP)
	MOVP	R4, R5, 48(RSP)
	MOVP	R6, R7, 64(RSP)
	MOVP	R8, R9, 80(RSP)
	MOVP	R10, R11, 96(RSP)
	MOVP	R12, R13, 112(RSP)
	MOVP	R14, R15, 128(RSP)
	MOVP	R16, R17, 144(RSP)
	MOVP	R18, R19, 160(RSP)
	MOVP	R20, R21, 176(RSP)
	MOVP	R22, R23, 192(RSP)
	MOVP	R24, R25, 208(RSP)

	MOV	R30, 256(RSP)	// link
	MOV	$(2<<32), R0	// type fiq

_vfiqpatch:
	B	_vfiqpatch	// branch to vtrapX() patched in

TEXT vserr(SB), 1, $-4
	SUB	$TRAPFRAMESIZE, RSP

	MOVP	R0, R1, 16(RSP)
	MOVP	R2, R3, 32(RSP)
	MOVP	R4, R5, 48(RSP)
	MOVP	R6, R7, 64(RSP)
	MOVP	R8, R9, 80(RSP)
	MOVP	R10, R11, 96(RSP)
	MOVP	R12, R13, 112(RSP)
	MOVP	R14, R15, 128(RSP)
	MOVP	R16, R17, 144(RSP)
	MOVP	R18, R19, 160(RSP)
	MOVP	R20, R21, 176(RSP)
	MOVP	R22, R23, 192(RSP)
	MOVP	R24, R25, 208(RSP)

	MOV	R30, 256(RSP)	// link

	MRS	ESR_EL1, R0
	ORR	$(3<<32), R0	// type
_vserrpatch:
	B	_vserrpatch	// branch to vtrapX() patched in
