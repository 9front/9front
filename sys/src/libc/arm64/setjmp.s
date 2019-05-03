TEXT setjmp(SB), 1, $-4
	MOV	LR, 8(R0)
	MOV	SP, R1
	MOV	R1, 0(R0)
	MOV	$0, R0
	RETURN

TEXT longjmp(SB), 1, $-4
	MOV	8(R0), LR
	MOV	0(R0), R1
	MOVW	arg+8(FP), R0
	MOV	R1, SP
	CBZ	R0, _one
	RETURN
_one:
	MOV	$1, R0
	RETURN
