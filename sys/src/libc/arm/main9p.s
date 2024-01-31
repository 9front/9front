arg=0
sp=13
sb=12
lr=14

TEXT	_mainp(SB), 1, $0
	SUB	$8, R(sp)
	MOVW	$setR12(SB), R(sb)
	MOVW	R(arg), _tos(SB)

	MOVW	$_profmain(SB), R(arg)
	MOVW	$0, R(lr)
	B	_callmain(SB)

	MOVW	$_div(SB), R(arg)	/* force loading of div */

TEXT	_saveret(SB), 1, $0
TEXT	_savearg(SB), 1, $0
	RET

TEXT	_callpc(SB), 1, $-4
	MOVW	0(R13), R(arg)
	RET
