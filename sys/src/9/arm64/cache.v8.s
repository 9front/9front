#include "../arm64/sysreg.h"

#undef	SYSREG
#define	SYSREG(op0,op1,Cn,Cm,op2)	SPR(((op0)<<19|(op1)<<16|(Cn)<<12|(Cm)<<8|(op2)<<5))

/*
 * instruction cache operations
 */
TEXT cacheiinvse(SB), 1, $-4
	MOVWU	len+8(FP), R2
	ADD	R0, R2

	MRS	DAIF, R11
	MSR	$0x2, DAIFSet
	MOVWU	$1, R10
	MSR	R10, CSSELR_EL1
	ISB	$SY
	MRS	CCSIDR_EL1, R4

	ANDW	$7, R4
	ADDW	$4, R4		// log2(linelen)
	LSL	R4, R10
	LSR	R4, R0
	LSL	R4, R0

_iinvse:
	IC	R0, 3,7,5,1	// IVAU
	ADD	R10, R0
	CMP	R0, R2
	BGT	_iinvse
	DSB	$NSH
	ISB	$SY
	MSR	R11, DAIF
	RETURN

TEXT cacheiinv(SB), 1, $-4
	IC	R0, 0,7,5,0	// IALLU
	DSB	$NSH
	ISB	$SY
	RETURN

TEXT cacheuwbinv(SB), 1, $0
	BL	cachedwbinv(SB)
	BL	cacheiinv(SB)
	RETURN

/*
 * data cache operations
 */
TEXT cachedwbse(SB), 1, $-4
	MOV	LR, R29
	BL	cachedva<>(SB)
TEXT dccvac(SB), 1, $-4
	DC	R0, 3,7,10,1	// CVAC
	RETURN

TEXT cacheduwbse(SB), 1, $-4
	MOV	LR, R29
	BL	cachedva<>(SB)
TEXT dccvau(SB), 1, $-4
	DC	R0, 3,7,11,1	// CVAU
	RETURN

TEXT cachedinvse(SB), 1, $-4
	MOV	LR, R29
	BL	cachedva<>(SB)
TEXT dcivac(SB), 1, $-4
	DC	R0, 0,7,6,1	// IVAC
	RETURN

TEXT cachedwbinvse(SB), 1, $-4
	MOV	LR, R29
	BL	cachedva<>(SB)
TEXT dccivac(SB), 1, $-4
	DC	R0, 3,7,14,1	// CIVAC
	RETURN

TEXT cachedva<>(SB), 1, $-4
	MOV	LR, R1
	MOVWU	len+8(FP), R2
	ADD	R0, R2

	MRS	DAIF, R11
	MSR	$0x2, DAIFSet
	MOVWU	$0, R10
	MSR	R10, CSSELR_EL1
	ISB	$SY
	MRS	CCSIDR_EL1, R4

	ANDW	$7, R4
	ADDW	$4, R4		// log2(linelen)
	MOVWU	$1, R10
	LSL	R4, R10
	LSR	R4, R0
	LSL	R4, R0

	DSB	$SY
	ISB	$SY
_cachedva:
	BL	(R1)
	ADD	R10, R0
	CMP	R0, R2
	BGT	_cachedva
	DSB	$SY
	ISB	$SY
	MSR	R11, DAIF
	RET	R29

/*
 * l1 cache operations
 */
TEXT cachedwb(SB), 1, $-4
	MOVWU	$0, R0
_cachedwb:
	MOV	LR, R29
	BL	cachedsw<>(SB)
TEXT dccsw(SB), 1, $-4
	DC	R0, 0,7,10,2	// CSW
	RETURN

TEXT cachedinv(SB), 1, $-4
	MOVWU	$0, R0
_cachedinv:
	MOV	LR, R29
	BL	cachedsw<>(SB)
TEXT dcisw(SB), 1, $-4
	DC	R0, 0,7,6,2	// ISW
	RETURN

TEXT cachedwbinv(SB), 1, $-4
	MOVWU	$0, R0
_cachedwbinv:
	MOV	LR, R29
	BL	cachedsw<>(SB)
TEXT dccisw(SB), 1, $-4
	DC	R0, 0,7,14,2	// CISW
	RETURN

/*
 * l2 cache operations
 */
TEXT l2cacheuwb(SB), 1, $-4
	MOVWU	$1, R0
	B	_cachedwb
TEXT l2cacheuinv(SB), 1, $-4
	MOVWU	$1, R0
	B	_cachedinv
TEXT l2cacheuwbinv(SB), 1, $-4
	MOVWU	$1, R0
	B	_cachedwbinv

TEXT cachesize(SB), 1, $-4
	MRS	DAIF, R11
	MSR	$0x2, DAIFSet
	MSR	R0, CSSELR_EL1
	ISB	$SY
	MRS	CCSIDR_EL1, R0
	MSR	R11, DAIF
	RETURN

TEXT cachedsw<>(SB), 1, $-4
	MOV	LR, R1

	MRS	DAIF, R11
	MSR	$0x2, DAIFSet
	ADDW	R0, R0, R8
	MSR	R8, CSSELR_EL1
	ISB	$SY
	MRS	CCSIDR_EL1, R4

	LSR	$3, R4, R7
	ANDW	$1023, R7	// lastway
	ADDW	$1, R7, R5	// #ways

	LSR	$13, R4, R2
	ANDW	$32767, R2	// lastset
	ADDW	$1, R2		// #sets

	ANDW	$7, R4
	ADDW	$4, R4		// log2(linelen)

	MOVWU	$32, R3		// wayshift = 32 - log2(#ways)
_countlog2ways:
	CBZ	R7, _loop	// lastway == 0?
	LSR	$1, R7		// lastway >>= 1
	SUB	$1, R3		// wayshift--
	B _countlog2ways
_loop:
	DSB	$SY
	ISB	$SY
_nextway:
	MOVWU	$0, R6		// set
_nextset:
	LSL	R3, R7, R0	// way<<wayshift
	LSL	R4, R6, R9	// set<<log2(linelen)
	ORRW	R8, R0		// level
	ORRW	R9, R0		// setway

	BL	(R1)		// op(setway)

	ADDW	$1, R6		// set++
	CMPW	R2, R6
	BLT	_nextset

	ADDW	$1, R7		// way++
	CMPW	R5, R7
	BLT	_nextway

	DSB	$SY
	ISB	$SY
	MSR	R11, DAIF
	RET	R29
