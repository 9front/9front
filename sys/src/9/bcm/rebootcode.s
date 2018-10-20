/*
 * armv6/armv7 reboot code
 */
#include "arm.s"

#define PTEDRAM		(Dom0|L1AP(Krw)|Section)

#define WFI	WORD	$0xe320f003	/* wait for interrupt */
#define WFE	WORD	$0xe320f002	/* wait for event */

/*
 * Turn off MMU, then copy the new kernel to its correct location
 * in physical memory.  Then jump to the start of the kernel.
 */

/* main(PADDR(entry), PADDR(code), size); */
TEXT	main(SB), 1, $-4
	MOVW	$setR12(SB), R12

	/* copy in arguments before stack gets unmapped */
	MOVW	R0, R8			/* entry point */
	MOVW	p2+4(FP), R9		/* source */
	MOVW	n+8(FP), R6		/* byte count */

	/* SVC mode, interrupts disabled */
	MOVW	$(PsrDirq|PsrDfiq|PsrMsvc), R1
	MOVW	R1, CPSR

	/* prepare to turn off mmu  */
	BL	cachesoff(SB)

	/* turn off mmu */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BIC	$CpCmmu, R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl

	/* continue with reboot only on cpu0 */
	CPUID(R2)
	BEQ	bootcpu

	/* other cpus wait for inter processor interrupt from cpu0 */
	/* turn icache back on */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	ORR	$(CpCicache), R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BARRIERS
dowfi:
	WFI
	MOVW	$0x40000060, R1
	ADD		R2<<2, R1
	MOVW	0(R1), R0
	AND		$0x10, R0
	BEQ		dowfi
	MOVW	$0x8000, R1
	BL		(R1)
	B		dowfi

bootcpu:
	/* set up a tiny stack for local vars and memmove args */
	MOVW	R8, SP			/* stack top just before kernel dest */
	SUB	$20, SP			/* allocate stack frame */

	/* copy the kernel to final destination */
	MOVW	R8, 16(SP)		/* save dest (entry point) */
	MOVW	R8, R0			/* first arg is dest */
	MOVW	R9, 8(SP)		/* push src */
	MOVW	R6, 12(SP)		/* push size */
	BL	memmove(SB)
	MOVW	16(SP), R8		/* restore entry point */

	/* jump to kernel physical entry point */
	ORR	R8,R8
	B	(R8)
	B	0(PC)

/*
 * turn the caches off, double map PHYSDRAM & KZERO, invalidate TLBs, revert
 * to tiny addresses.  upon return, it will be safe to turn off the mmu.
 * clobbers R0-R2, and returns with SP invalid.
 */
TEXT cachesoff(SB), 1, $-4
	MOVM.DB.W [R14,R1-R10], (R13)		/* save regs on stack */

	/* turn caches off, invalidate icache */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BIC	$(CpCdcache|CpCicache|CpCpredict), R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	MOVW	$0, R0
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEinvi), CpCACHEall

	/* invalidate stale TLBs before changing them */
	BARRIERS
	MOVW	$0, R0
	MCR	CpSC, 0, R0, C(CpTLB), C(CpTLBinvu), CpTLBinv
	BARRIERS

	/* redo double map of first MiB PHYSDRAM = KZERO */
	MOVW	12(R(MACH)), R2		/* m->mmul1 (virtual addr) */
	MOVW	$PTEDRAM, R1			/* PTE bits */
	MOVW	R1, (R2)
	DSB
	MCR	CpSC, 0, R2, C(CpCACHE), C(CpCACHEwb), CpCACHEse

	/* invalidate stale TLBs again */
	BARRIERS
	MOVW	$0, R0
	MCR	CpSC, 0, R0, C(CpTLB), C(CpTLBinvu), CpTLBinv
	BARRIERS

	/* relocate SB and return address to PHYSDRAM addressing */
	MOVW	$KSEGM, R1		/* clear segment bits */
	BIC	R1, R12			/* adjust SB */
	MOVM.IA.W (R13), [R14,R1-R10]		/* restore regs from stack */

	MOVW	$KSEGM, R1		/* clear segment bits */
	BIC	R1, R14			/* adjust return address */

	RET
