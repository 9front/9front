TEXT _main(SB), $-4
	MOVW $setR12(SB), R12
	MOVW 4(R13), R0
	ADD $4, R13, R1
	SUB $4, R13
	MOVW R1, 8(R13)
	MOVW $startboot(SB), R15
