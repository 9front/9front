TEXT memcpy(SB), $-4
TEXT memmove(SB), $-4
	MOV	from+8(FP), R1
	MOVWU	n+16(FP), R2

	CMP	R0, R1
	BEQ	_done
	BLT	_backward

_forward:
	ADD	R0, R2, R3
	BIC	$7, R2, R4
	CBZ	R4, _floop1
	ADD	R0, R4, R4

_floop8:
	MOV	(R1)8!, R5
	MOV	R5, (R0)8!
	CMP	R4, R0
	BNE	_floop8

_floop1:
	CMP	R3, R0
	BEQ	_done
	MOVBU	(R1)1!, R5
	MOVBU	R5, (R0)1!
	B	_floop1

_done:
	RETURN

_backward:
	ADD	R2, R1, R1
	ADD	R2, R0, R3
	BIC	$7, R2, R4
	CBZ	R4, _bloop1
	SUB	R4, R3, R4

_bloop8:
	MOV	-8(R1)!, R5
	MOV	R5, -8(R3)!
	CMP	R4, R3
	BNE	_bloop8

_bloop1:
	CMP	R0, R3
	BEQ	_done
	MOVBU	-1(R1)!, R5
	MOVBU	R5, -1(R3)!
	B	_bloop1
