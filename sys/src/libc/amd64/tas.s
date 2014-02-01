TEXT	_tas(SB), 1, $0

	MOVL	$0xdeaddead,AX
	XCHGL	AX,(RARG)
	RET
