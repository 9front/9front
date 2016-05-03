#define NPRIVATES	16

GLOBL	_tos(SB), $4
GLOBL	_errnoloc(SB), $4
GLOBL	_plan9err(SB), $4
GLOBL	_privates(SB), $4
GLOBL	_nprivates(SB), $4

TEXT	_mainp(SB), 1, $(12+4+128+NPRIVATES*4)

	MOVW	$setSB(SB), R2

	/* _tos = arg */
	MOVW	R3, _tos(SB)

	MOVW	$12(R1), R3
	MOVW	R3, _errnoloc(SB)
	ADD	$4, R3
	MOVW	R3, _plan9err(SB)
	ADD	$128, R3
	MOVW	R3, _privates(SB)
	MOVW	$NPRIVATES, R3
	MOVW	R3, _nprivates(SB)

	/* _profmain(); */
	BL	_envsetup(SB)

	/* _tos->prof.pp = _tos->prof.next; */
	MOVW	_tos+0(SB),R3
	MOVW	4(R3),R2
	MOVW	R2,(R3)

	/* main(argc, argv, environ); */
	MOVW	inargc-4(FP), R3
	MOVW	$inargv+0(FP), R4
	MOVW	environ(SB), R5
	MOVW	R3, 4(R1)
	MOVW	R4, 8(R1)
	MOVW	R5, 12(R1)
	BL	main(SB)
loop:
	MOVW	R3, 4(R1)
	BL	exit(SB)
	MOVW	$_profin(SB), R4	/* force loading of profile */
	BR	loop

TEXT	_saveret(SB), 1, $0
TEXT	_savearg(SB), 1, $0
	RETURN

TEXT	_callpc(SB), 1, $0
	MOVW	argp+0(FP), R3
	MOVW	4(R3), R3
	RETURN
