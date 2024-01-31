TEXT	_mainp(SB), 1, $0
	MOVQ	AX, _tos(SB)
	MOVQ	$_profmain(SB), RARG
	PUSHQ	RARG
	PUSHQ	$0
	JMPF	_callmain(SB)

TEXT	_savearg(SB), 1, $0
	MOVQ	RARG, AX
	RET

TEXT	_saveret(SB), 1, $0
	RET				/* we want RARG in RARG */

TEXT	_callpc(SB), 1, $0
	MOVQ	8(RARG), AX
	RET
