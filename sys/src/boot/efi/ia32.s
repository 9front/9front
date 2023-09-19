#include "mem.h"

TEXT start(SB), 1, $0
	CALL reloc(SP)

TEXT reloc(SB), 1, $0
	MOVL 0(SP), SI
	SUBL $reloc-IMAGEBASE(SB), SI
	MOVL $IMAGEBASE, DI
	MOVL $edata-IMAGEBASE(SB), CX
	CLD
	REP; MOVSB
	MOVL $efimain(SB), DI
	MOVL DI, (SP)
	RET

TEXT jump(SB), $0
	CLI
	MOVL 4(SP), AX
	JMP *AX

TEXT eficall(SB), 1, $0
	MOVL SP, SI
	MOVL SP, DI
	MOVL $(4*16), CX
	SUBL CX, DI
	ANDL $~15ULL, DI
	SUBL $8, DI

	MOVL 4(SI), AX
	LEAL 8(DI), SP

	CLD
	REP; MOVSB
	SUBL $(4*16), SI

	CALL AX

	MOVL SI, SP
	RET

TEXT rebase(SB), 1, $0
	MOVL 4(SP), AX
	RET

GLOBL	confaddr(SB), $4
DATA	confaddr(SB)/4, $CONFADDR
