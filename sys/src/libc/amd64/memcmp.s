TEXT	memcmp(SB),$0

	MOVQ	n+16(FP), BX
	CMPQ	BX, $0
	JEQ	none
	MOVQ	RARG, DI
	MOVQ	p2+8(FP), SI
	CLD
	MOVQ	DI, CX
	ORQ	SI, CX
	ANDL	$3, CX
	JNE	c3
/*
 * first by longs
 */

	MOVQ	BX, CX
	SHRQ	$2, CX

	REP;	CMPSL
	JNE	found

/*
 * then by bytes
 */
	ANDL	$3, BX
c3:
	MOVQ	BX, CX
	REP;	CMPSB
	JNE	found1

none:
	MOVQ	$0, AX
	RET

/*
 * if long found,
 * back up and look by bytes
 */
found:
	MOVL	$4, CX
	SUBQ	CX, DI
	SUBQ	CX, SI
	REP;	CMPSB

found1:
	JLS	lt
	MOVQ	$-1, AX
	RET
lt:
	MOVQ	$1, AX
	RET
