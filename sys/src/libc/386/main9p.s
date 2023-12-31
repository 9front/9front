TEXT	_mainp(SB), 1, $0
	MOVL	AX, _tos(SB)
	MOVL	$_profmain(SB), AX
	PUSHL	AX
	PUSHL	$0
	MOVL	$_callmain(SB), AX
	JMP*	AX
	MOVL	$_profin(SB), AX	/* force loading of profile */

TEXT	_saveret(SB), 1, $0
TEXT	_savearg(SB), 1, $0
	RET

TEXT	_callpc(SB), 1, $0
	MOVL	argp+0(FP), AX
	MOVL	4(AX), AX
	RET
