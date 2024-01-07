TEXT	_main(SB), 1, $0
	MOVL	AX, _tos(SB)
	MOVL	$main(SB), AX
	PUSHL	AX
	PUSHL	$0
	JMPF	_callmain(SB)
