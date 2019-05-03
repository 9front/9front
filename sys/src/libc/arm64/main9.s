#define NPRIVATES	16

TEXT	_main(SB), 1, $(16 + NPRIVATES*8)
	MOV	$setSB(SB), R28
	MOV	R0, _tos(SB)

	ADD	$32, RSP, R1
	MOV	R1, _privates(SB)
	MOVW	$NPRIVATES, R2
	MOVW	R2, _nprivates(SB)

	MOV	$inargv+0(FP), R1
	MOV	R1, 16(RSP)

	MOVW	inargc-8(FP), R0
	MOV	R0, 8(RSP)

	BL	main(SB)
loop:
	MOV	$_exitstr<>(SB), R0
	BL	exits(SB)
	B	loop

DATA	_exitstr<>+0(SB)/4, $"main"
GLOBL	_exitstr<>+0(SB), $5
