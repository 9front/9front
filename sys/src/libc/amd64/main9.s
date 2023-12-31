TEXT	_main(SB), 1, $0
	MOVQ	AX, _tos(SB)
	MOVQ	$main(SB), RARG
	PUSHQ	RARG
	PUSHQ	$0
	MOVQ	$_callmain(SB), AX
	JMP*	AX
