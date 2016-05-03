#define NPRIVATES	16

GLOBL	_tos(SB), $4
GLOBL	_errnoloc(SB), $4
GLOBL	_plan9err(SB), $4
GLOBL	_privates(SB), $4
GLOBL	_nprivates(SB), $4

TEXT	_main(SB), 1, $(20+4+128+NPRIVATES*4)
	MOVQ	$setSB(SB), R29

	/* _tos = arg */
	MOVL	R0, _tos(SB)

	MOVL	$20(R30), R1
	MOVL	R1, _errnoloc(SB)
	ADDL	$4, R1
	MOVL	R1, _plan9err(SB)
	ADDL	$128, R1
	MOVL	R1, _privates(SB)
	MOVQ	$NPRIVATES, R1
	MOVL	R1, _nprivates(SB)

	JSR	_envsetup(SB)

	/* main(argc, argv, environ); */
	MOVL	inargc-4(FP), R0
	MOVL	$inargv+0(FP), R1
	MOVL	environ(SB), R2
	MOVL	R0, 8(R30)
	MOVL	R1, 12(R30)
	MOVL	R2, 16(R30)
	JSR	main(SB)
loop:
	MOVL	R0, 8(R30)
	JSR	exit(SB)
	MOVQ	$_divq(SB), R31		/* force loading of divq */
	MOVQ	$_divl(SB), R31			/* force loading of divl */
	JMP	loop
