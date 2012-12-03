#define NPRIVATES	16

GLOBL	_tos(SB), $4
GLOBL	_privates(SB), $4
GLOBL	_nprivates(SB), $4

TEXT	_main(SB), 1, $(3*4+NPRIVATES*4)

	/* _tos = arg */
	MOVL	AX, _tos(SB)
	LEAL	8(SP), AX
	MOVL	AX, _privates(SB)
	MOVL	$NPRIVATES, _nprivates(SB)

	CALL	_envsetup(SB)
	MOVL	inargc-4(FP), AX
	MOVL	AX, 0(SP)
	LEAL	inargv+0(FP), AX
	MOVL	AX, 4(SP)
	MOVL	environ(SB), AX
	MOVL	AX, 8(SP)
	CALL	main(SB)
	MOVL	AX, 0(SP)
	CALL	exit(SB)
	RET
