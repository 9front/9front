TEXT	_mainp(SB), 1, $0
	SUB	$8, R1
	MOVW	$setSB(SB), R2
	MOVW	R7, _tos(SB)

/*
	MOVW	_fpsr+0(SB), FSR
	FMOVD	$0.5, F26
	FSUBD	F26, F26, F24
	FADDD	F26, F26, F28
	FADDD	F28, F28, F30
*/

	MOVW	$_profmain(SB), R7
	MOVW	$_callmain(SB), R24
	MOVW	$0, R15
	JMP	(R24)
	MOVW	$_mul(SB), R8		/* force loading of muldiv */

TEXT	_saveret(SB), 1, $0
TEXT	_savearg(SB), 1, $0
	RETURN

TEXT	_callpc(SB), 1, $0
	MOVW	argp-4(FP), R7
	RETURN
