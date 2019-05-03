/*
 * int cas32(u32int *p, u32int ov, u32int nv);
 * int cas(uint *p, int ov, int nv);
 * int casl(ulong *p, ulong ov, ulong nv);
 */
TEXT cas32(SB), 1, $-4
TEXT cas(SB), 1, $-4
TEXT casl(SB), 1, $-4
	MOVWU	ov+8(FP), R1
	MOVWU	nv+16(FP), R2
_cas1:
	LDXRW	(R0), R3
	CMP	R3, R1
	BNE	_cas0
	STXRW	R2, (R0), R4
	CBNZ	R4, _cas1
	MOVW	$1, R0
	B	_barrier(SB)
_cas0:
	CLREX
	MOVW	$0, R0
	RETURN

TEXT casp(SB), 1, $-4
	MOV	ov+8(FP), R1
	MOV	nv+16(FP), R2
_casp1:
	LDXR	(R0), R3
	CMP	R3, R1
	BNE	_cas0
	STXR	R2, (R0), R4
	CBNZ	R4, _casp1
	MOVW	$1, R0
	B	_barrier(SB)

TEXT _xinc(SB), 1, $-4	/* void	_xinc(long *); */
TEXT ainc(SB), 1, $-4	/* long ainc(long *); */
spinainc:
	LDXRW	(R0), R3
	ADDW	$1,R3
	STXRW	R3, (R0), R4
	CBNZ	R4, spinainc
	MOVW	R3, R0
	B	_barrier(SB)

TEXT _xdec(SB), 1, $-4	/* long _xdec(long *); */
TEXT adec(SB), 1, $-4	/* long adec(long *); */
spinadec:
	LDXRW	(R0), R3
	SUBW	$1,R3
	STXRW	R3, (R0), R4
	CBNZ	R4, spinadec
	MOVW	R3, R0
	B	_barrier(SB)
