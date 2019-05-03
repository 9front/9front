#define NPRIVATES	16

GLOBL	_tos(SB), $8
GLOBL	_errnoloc(SB), $8
GLOBL	_plan9err(SB), $128
GLOBL	_privates(SB), $8
GLOBL	_nprivates(SB), $4

TEXT	_main(SB), 1, $(32 + 8+128 + NPRIVATES*8)
	MOV	$setSB(SB), R28
	MOV	R0, _tos(SB)

	ADD	$32, RSP, R1

	MOV	R1, _errnoloc(SB)
	ADD	$8, R1

	MOV	R1, _plan9err(SB)
	ADD	$128, R1

	MOV	R1, _privates(SB)
	MOVW	$NPRIVATES, R2
	MOVW	R2, _nprivates(SB)

	BL	_envsetup(SB)

	MOV	environ(SB), R2
	MOV	R2, 24(RSP)

	MOV	$inargv+0(FP), R1
	MOV	R1, 16(RSP)

	MOVW	inargc-8(FP), R0
	MOV	R0, 8(RSP)

	BL	main(SB)
loop:
	BL	exit(SB)
	B	loop
