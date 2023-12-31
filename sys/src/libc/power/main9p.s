TEXT	_mainp(SB), 1, $0
	SUB	$8, R1
	MOVW	$setSB(SB), R2
	MOVW	R3, _tos(SB)

	MOVW	$_profmain(SB), R3
	MOVW	R0, LR
	MOVW	$_callmain(SB), R4
	MOVW	R4, CTR
	BR	(CTR)

TEXT	_saveret(SB), 1, $0
TEXT	_savearg(SB), 1, $0
	RETURN

TEXT	_callpc(SB), 1, $0
	MOVW	argp-4(FP), R3
	RETURN
