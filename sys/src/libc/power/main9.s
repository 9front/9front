TEXT	_main(SB), 1, $0
	SUB	$8, R1
	MOVW	$setSB(SB), R2
	MOVW	R3, _tos(SB)

	MOVW	$main(SB), R3
	MOVW	R0, LR
	MOVW	$_callmain(SB), R4
	MOVW	R4, CTR
	BR	(CTR)
