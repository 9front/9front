#define NPRIVATES	16

GLOBL	_tos(SB), $4
GLOBL	_errnoloc(SB), $4
GLOBL	_plan9err(SB), $4
GLOBL	_privates(SB), $4
GLOBL	_nprivates(SB), $4

TEXT	_main(SB), 1, $(4+128+NPRIVATES*4)
	MOVL	$a6base(SB), A6

	/* _tos = arg */
	MOVL	R0, _tos(SB)		/* return value of sys exec!! */

	MOVL	A7, A1
	MOVL	A1, _errnoloc(SB)
	ADDL	$4, A1
	MOVL	A1, _plan9err(SB)
	ADDL	$128, A1
	MOVL	A1, _privates(SB)
	MOVL	$NPRIVATES, _nprivates(SB)

	BSR	_envsetup(SB)

	/* main(argc, argv, environ); */
	MOVL	environ(SB), TOS
	PEA	inargv+0(FP)
	MOVL	inargc-4(FP), TOS
	BSR	main(SB)

	MOVL	R0,TOS
	BSR	exit(SB)
	RTS
