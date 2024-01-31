TEXT	_mainp(SB), 1, $0
	SUB	$16, SP
	MOV	$setSB(SB), R28
	MOV	R0, _tos(SB)

	MOV	$_profmain(SB), R0

	MOV	$0, R30
	B	_callmain(SB)

TEXT	_callpc(SB), 1, $-4
	MOV	0(SP), R0
TEXT	_saveret(SB), 1, $-4
TEXT	_savearg(SB), 1, $-4
	RETURN
