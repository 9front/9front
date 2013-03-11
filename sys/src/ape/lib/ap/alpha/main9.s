#define NPRIVATES	16

GLOBL	_tos(SB), $4
GLOBL	_errnoloc(SB), $4
GLOBL	_privates(SB), $4
GLOBL	_nprivates(SB), $4

TEXT	_main(SB), 1, $(16+NPRIVATES*4)
	MOVQ	$setSB(SB), R29

	/* _tos = arg */
	MOVL	R0, _tos(SB)
	MOVQ	$12(SP), R1
	MOVL	R1, _errnoloc(SB)
	MOVQ	$16(SP), R1
	MOVL	R1, _privates(SB)
	MOVQ	$NPRIVATES, R1
	MOVL	R1, _nprivates(SB)

	JSR	_envsetup(SB)
	MOVL	inargc-8(FP), R0
	MOVL	$inargv-4(FP), R1
	MOVL	R0, 8(R30)
	MOVL	R1, 12(R30)
	JSR	main(SB)
loop:
	MOVL	R0, 8(R30)
	JSR	exit(SB)
	MOVQ	$_divq(SB), R31		/* force loading of divq */
	MOVQ	$_divl(SB), R31			/* force loading of divl */
	JMP	loop
