TEXT	_mainp(SB), 1, $0
	ADD	$-8, R29
	MOVW	$setR30(SB), R30
	MOVW	R1, _tos(SB)

	MOVW	$_profmain(SB), R1
	MOVW	$0, R31
	JMP	_callmain(SB)

TEXT	_saveret(SB), 1, $0
TEXT	_savearg(SB), 1, $0
	RET

TEXT	_callpc(SB), 1, $0
	MOVW	argp-4(FP), R1
	RET
