#define NPRIVATES	16

GLOBL	_tos(SB), $4
GLOBL	_errnoloc(SB), $4
GLOBL	_privates(SB), $4
GLOBL	_nprivates(SB), $4

TEXT	_main(SB), 1, $(16+NPRIVATES*4)
	MOVW	$setR30(SB), R30

	/* _tos = arg */
	MOVW	R1, _tos(SB)
/*
	MOVW	$0,FCR31
	NOR	R0,R0
	MOVD	$0.5, F26
	SUBD	F26, F26, F24
	ADDD	F26, F26, F28
	ADDD	F28, F28, F30
*/
	MOVW	$12(SP), R1
	MOVW	R1, _errnoloc(SB)
	MOVW	$16(SP), R1
	MOVW	R1, _privates(SB)
	MOVW	$NPRIVATES, R1
	MOVW	R1, _nprivates(SB)

	JAL	_envsetup(SB)
	MOVW	inargc-4(FP), R1
	MOVW	$inargv+0(FP), R2
	MOVW	R1, 4(R29)
	MOVW	R2, 8(R29)
	JAL	main(SB)
loop:
	MOVW	R1, 4(R29)
	JAL	exit(SB)
	JMP	loop
