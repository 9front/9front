#include "x16.h"
#include "mem.h"

TEXT e820(SB), $0
	MOVL bx+4(SP), BX
	MOVL p+8(SP), DI

	MOVL $0xe820, AX
	MOVL $0x534D4150, DX
	CALL rmode16(SB)
	LWI(24, rCX)
	BIOSCALL(0x15)
	JC _bad
	CALL16(pmode32(SB))
	MOVL BX, AX
	RET
_bad:
	CALL16(pmode32(SB))
	XORL AX, AX
	RET
