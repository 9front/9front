#define NPRIVATES	16

GLOBL	_tos(SB), $4
GLOBL	_privates(SB), $4
GLOBL	_nprivates(SB), $4

TEXT	_main(SB), 1, $(3*4+NPRIVATES*4)
	MOVW	$setSB(SB), R2

	/* _tos = arg */
	MOVW	R7, _tos(SB)
/*
	MOVW	_fpsr+0(SB), FSR
	FMOVD	$0.5, F26
	FSUBD	F26, F26, F24
	FADDD	F26, F26, F28
	FADDD	F28, F28, F30
*/
	MOVW	$8(SP), R1
	MOVW	R1, _privates(SB)
	MOVW	$NPRIVATES, R1
	MOVW	R1, _nprivates(SB)

	JMPL	_envsetup(SB)
	MOVW	inargc-4(FP), R7
	MOVW	$inargv+0(FP), R8
	MOVW	R7, 4(R1)
	MOVW	R8, 8(R1)
	JMPL	main(SB)

loop:
	MOVW	R7, 4(R1)
	JMPL	exit(SB)
	MOVW	$_mul(SB),R7
	JMP	loop
