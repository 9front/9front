TEXT	_mainp(SB), 1, $0
	MOVL	$a6base(SB), A6
	MOVL	R0, _tos(SB)
	PEA	_profmain(SB)
	MOVL	$0, TOS
	JMP	_callmain(SB)

TEXT	_saveret(SB), 1, $0
TEXT	_savearg(SB), 1, $0
	RTS

TEXT	_callpc(SB), 1, $0
	MOVL	argp+0(FP), A0
	MOVL	4(A0), R0
	RTS
