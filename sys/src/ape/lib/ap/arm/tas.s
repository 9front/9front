TEXT tas(SB), 1, $-4
	MOVW	$1, R2
_tas1:
	LDREX	(R0), R1
	STREX	R2, (R0), R3
	CMP.S	$0, R3
	BNE	_tas1
	MOVW	R1, R0
	MOVW	_barrier(SB), R4
	B	(R4)

TEXT _dmb(SB), 1, $-4
	WORD $0xf57ff05f
	RET
