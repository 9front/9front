#include "mem.h"
#include "sysreg.h"

#undef	SYSREG
#define	SYSREG(op0,op1,Cn,Cm,op2)	SPR(((op0)<<19|(op1)<<16|(Cn)<<12|(Cm)<<8|(op2)<<5))

TEXT _start(SB), 1, $-4
	MOV	$setSB(SB), R28

	MRS	MPIDR_EL1, R27
	ANDW	$(MAXMACH-1), R27
	LSL	$3, R27
	ADD	$(SPINTABLE-KZERO), R27

	CBZ	R0, _mmuoff
	MOV	R0, (R27)
	MOV	code+8(FP), R1
	MOVWU	size+16(FP), R2
	BIC	$3, R2
	ADD	R1, R2, R3

_copy:
	MOVW	(R1)4!, R4
	MOVW	R4, (R0)4!
	CMP	R1, R3
	BNE	_copy

	BL	cachedwbinv(SB)
	BL	l2cacheuwbinv(SB)
	SEVL

_mmuoff:
	ISB	$SY
	MRS	SCTLR_EL1, R0
	BIC	$(1<<0 | 1<<2 | 1<<12), R0
	ISB	$SY
	MSR	R0, SCTLR_EL1
	ISB	$SY

	DSB	$NSHST
	TLBI	R0, 0,8,7,0	/* VMALLE1 */
	DSB	$NSH
	ISB	$SY

	BL	cachedwbinv(SB)
	BL	cacheiinv(SB)

	MOVWU	$0, R0
	MOVWU	$0, R1
	MOVWU	$0, R2
	MOVWU	$0, R3
_wait:
	WFE
	MOV	(R27), LR
	CBZ	LR, _wait
	RETURN
