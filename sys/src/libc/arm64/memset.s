TEXT memset(SB), $-4
	MOVBU	c+8(FP), R1
	MOVWU	n+16(FP), R2

	MOV	R0, R5
	ADD	R0, R2, R3
	BIC	$15, R2, R4
	CBZ	R4, _loop1
	ADD	R0, R4, R4

	ORR	R1<<8, R1
	ORR	R1<<16, R1
	ORR	R1<<32, R1

_loop16:
	MOVP	R1, R1, (R5)16!
	CMP	R4, R5
	BNE	_loop16

_loop1:
	CMP	R3, R5
	BEQ	_done

	MOVBU	R1, (R5)1!
	B	_loop1

_done:
	RETURN
