#define NPRIVATES	16

GLOBL	_tos(SB), $4
GLOBL	_errnoloc(SB), $4
GLOBL	_privates(SB), $4
GLOBL	_nprivates(SB), $4

TEXT	_main(SB), 1, $(16+NPRIVATES*4)

	MOVW	$setSB(SB), R2

	/* _tos = arg */
	MOVW	R3, _tos(SB)
	MOVW	$12(SP), R1
	MOVW	R1, _errnoloc(SB)
	MOVW	$16(SP), R1
	MOVW	R1, _privates(SB)
	MOVW	$NPRIVATES, R1
	MOVW	R1, _nprivates(SB)

	BL	_envsetup(SB)
	MOVW	inargc-4(FP), R3
	MOVW	$inargv+0(FP), R4
	MOVW	R3, 4(R1)
	MOVW	R4, 8(R1)
	BL	main(SB)
loop:
	MOVW	R3, 4(R1)
	BL	exit(SB)
	BR	loop
