TEXT main(SB), 1, $8
	MOV	$setSB(SB), R28		/* load the SB */
	MOV	$boot(SB), R0
	B	startboot(SB)
