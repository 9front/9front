#include "x16.h"

#undef ORB

TEXT a20(SB), $0
	CALL rmode16(SB)
	STI
	LWI(0x2401, rAX)
	BIOSCALL(0x15)
	JC _biosfail
	CLI
	CALL16(pmode32(SB))
	RET

_biosfail:
	CLI
	CALL16(pmode32(SB))

	CALL a20wait(SB)
	MOVL $0x64, DX
	MOVB $0xAD, AL
	OUTB

	CALL a20wait(SB)
	MOVL $0x64, DX
	MOVB $0xD0, AL
	OUTB

	CALL a20wait2(SB)
	MOVL $0x60, DX
	INB
	PUSHL AX

	CALL a20wait(SB)
	MOVL $0x64, DX
	MOVB $0xD1, AL
	OUTB

	CALL a20wait(SB)
	MOVL $0x60, DX
	POPL AX
	ORB $2, AL
	OUTB

	CALL a20wait(SB)
	MOVL $0x64, DX
	MOVB $0xAE, AL
	OUTB

TEXT a20wait(SB), $0
_a20wait:
	MOVL $0x64, DX
	INB
	TESTB $1, AL
	JZ _a20wait2
	RET

TEXT a20wait2(SB), $0
_a20wait2:
	MOVL $0x64, DX
	INB
	TESTB $2, AL
	JNZ _a20wait
	RET
