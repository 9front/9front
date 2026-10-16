/*		MOVW PC, R1		ADD 5, R1		BX R1 */
#define THUMB	WORD $0xE1A0100F;	WORD $0xE2811005;	WORD $0xE12FFF11

TEXT _main(SB), 1, $-4
	THUMB
TEXT _maint(SB), 4, $-4
	MOVW	$setR12(SB), R1	/* load the SB */
	MOVW	R1,R12

	/* all of our cart is mapped into ROM.
	 * We expect that the linker has chosen a suitable
	 * place in RAM for us to bootstrap DATA to. */
	MOVW	$etext(SB), R1
	/* assume the start of DATA in the ROM is word aligned,
	 * aout2gba does this */
	ADD	$3, R1
	MOVW	$-4, R6
	AND	R6, R1
	MOVW	$bdata(SB), R4
	MOVW	$edata(SB), R5
copy:
	CMP	R4, R5
	BEQ	bss
	MOVW	(R1), R6
	MOVW	R6, (R4)
	ADD	$4, R1
	ADD	$4, R4
	B	copy

bss:
	MOVW	$end(SB), R5
	MOVW	$0, R1
zero:
	CMP	R4, R5
	BEQ	done
	MOVW	R1, (R4)
	ADD	$4, R4
	B	zero

done:
	B	,main(SB)

TEXT _isr(SB), 1, $-4
	THUMB
TEXT _isrt(SB), 4, $-4
	SUB	$36,SP
	WORD	$0x4672		/* MOV LR,R2 */
	MOVW	R2,0(SP)

	MOVW	R4,4(SP)
	MOVW	R5,8(SP)
	MOVW	R6,12(SP)
	MOVW	R7,16(SP)

	MOVW	R8,R2
	MOVW	R2,20(SP)
	MOVW	R9,R2
	MOVW	R2,24(SP)
	MOVW	R10,R2
	MOVW	R2,28(SP)
	MOVW	R11,R2
	MOVW	R2,32(SP)

	BL	,isr(SB)

	MOVW	0(SP),R2
	WORD	$0x4696		/* MOV R2,LR */
	
	MOVW	4(SP),R4
	MOVW	8(SP),R5
	MOVW	12(SP),R6
	MOVW	16(SP),R7

	MOVW	20(SP),R2
	MOVW	R2,R8
	MOVW	24(SP),R2
	MOVW	R2,R9
	MOVW	28(SP),R2
	MOVW	R2,R10
	MOVW	32(SP),R2
	MOVW	R2,R11

	ADD	$36,SP
	WORD	$0x4770		/* BX LR */
