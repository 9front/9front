TEXT	_tas(SB), 1, $-4
	MOVW	$1, R2
_tas1:
	LDXRW	(R0), R1
	STXRW	R2, (R0), R3
	CBNZ	R3, _tas1
	MOVW	R1, R0
	B _barrier(SB)

TEXT	_barrier(SB), 1, $-4
	DMB	$0xB	// ISH
	RETURN
