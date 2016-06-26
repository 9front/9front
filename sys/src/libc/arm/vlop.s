TEXT	_mulv(SB), 1, $0
	MOVW	4(FP),R8	/* l0 */
	MOVW	8(FP),R11	/* h0 */
	MOVW	12(FP),R4	/* l1 */
	MOVW	16(FP),R5	/* h1 */
	MULLU	R8,R4,(R7,R6)	/* l0*l1 */
	MUL	R8,R5,R5	/* l0*h1 */
	ADD	R5,R7
	MUL	R11,R4,R4	/* h0*l1 */
	ADD	R4,R7
	MOVM.IA	[R6,R7],(R0)
	RET

TEXT	_addv(SB), 1, $0
	MOVW	4(FP),R8	/* l0 */
	MOVW	8(FP),R11	/* h0 */
	MOVW	12(FP),R4	/* l1 */
	MOVW	16(FP),R5	/* h1 */
	ADD.S	R8,R4
	ADC	R11,R5
	MOVM.IA	[R4,R5],(R0)
	RET

TEXT	_subv(SB), 1, $0
	MOVW	4(FP),R8	/* l0 */
	MOVW	8(FP),R11	/* h0 */
	MOVW	12(FP),R4	/* l1 */
	MOVW	16(FP),R5	/* h1 */
	SUB.S	R4,R8,R4
	SBC	R5,R11,R5
	MOVM.IA	[R4,R5],(R0)
	RET
