TEXT	_main(SB), 1, $0
	MOVL	$a6base(SB), A6
	MOVL	R0, _tos(SB)
	PEA	main(SB)
	MOVL	$0, TOS
	JMP	_callmain(SB)
