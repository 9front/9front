#define NPRIVATES	16

TEXT	_mainp(SB), 1, $(16 + NPRIVATES*8)
	MOV	$setSB(SB), R28
	MOV	R0, _tos(SB)

	ADD	$32, RSP, R1
	MOV	R1, _privates(SB)
	MOVW	$NPRIVATES, R2
	MOVW	R2, _nprivates(SB)

	BL	_profmain(SB)

	MOV	_tos(SB), R0	/* _tos->prof.pp = _tos->prof.next; */
	MOV	8(R0), R1
	MOV	R1, 0(R0)

	MOV	$inargv+0(FP), R1
	MOV	R1, 16(RSP)

	MOVW	inargc-8(FP), R0
	MOV	R0, 8(RSP)

	BL	main(SB)
loop:
	MOV	$_exitstr<>(SB), R0
	BL	exits(SB)
	MOV	$_profin(SB), R0
	B	loop

TEXT	_callpc(SB), 1, $-4
	MOV	0(SP), R0
TEXT	_saveret(SB), 1, $-4
TEXT	_savearg(SB), 1, $-4
	RETURN

DATA	_exitstr<>+0(SB)/4, $"main"
GLOBL	_exitstr<>+0(SB), $5
