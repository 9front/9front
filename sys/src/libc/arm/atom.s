/*
 * int cas32(u32int *p, u32int ov, u32int nv);
 * int cas(uint *p, int ov, int nv);
 * int casp(void **p, void *ov, void *nv);
 * int casl(ulong *p, ulong ov, ulong nv);
 */
TEXT cas32(SB), 1, $-4
TEXT cas(SB), 1, $-4
TEXT casp(SB), 1, $-4
TEXT casl(SB), 1, $-4
	MOVW	ov+4(FP), R1
	MOVW	nv+8(FP), R2
spincas:
	LDREX	(R0), R3
	CMP.S	R3, R1
	BNE	fail
	STREX	R2, (R0), R4
	CMP.S	$0, R4
	BNE	spincas
	MOVW	$1, R0
	MOVW	_barrier(SB), R5
	B	(R5)
fail:
	CLREX
	MOVW	$0, R0
	RET

TEXT _xinc(SB), 1, $-4	/* void	_xinc(long *); */
TEXT ainc(SB), 1, $-4	/* long ainc(long *); */
spinainc:
	LDREX	(R0), R3
	ADD	$1,R3
	STREX	R3, (R0), R4
	CMP.S	$0, R4
	BNE	spinainc
	MOVW	R3, R0
	MOVW	_barrier(SB), R5
	B	(R5)

TEXT _xdec(SB), 1, $-4	/* long _xdec(long *); */
TEXT adec(SB), 1, $-4	/* long adec(long *); */
spinadec:
	LDREX	(R0), R3
	SUB	$1,R3
	STREX	R3, (R0), R4
	CMP.S	$0, R4
	BNE	spinadec
	MOVW	R3, R0
	MOVW	_barrier(SB), R5
	B	(R5)
