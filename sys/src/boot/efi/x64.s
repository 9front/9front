MODE $64

TEXT start(SB), 1, $-4
	/* spill arguments */
	MOVQ CX, 8(SP)
	MOVQ DX, 16(SP)

	CALL reloc(SP)

TEXT reloc(SB), 1, $-4
	MOVQ 0(SP), SI
	SUBQ $reloc-IMAGEBASE(SB), SI
	MOVQ $IMAGEBASE, DI
	MOVQ $edata-IMAGEBASE(SB), CX
	CLD
	REP; MOVSB

	MOVQ 16(SP), BP
	MOVQ $efimain(SB), DI
	MOVQ DI, (SP)
	RET

TEXT eficall(SB), 1, $-4
	MOVQ SP, SI
	MOVQ SP, DI
	MOVL $(8*16), CX
	SUBQ CX, DI
	ANDQ $~15ULL, DI
	LEAQ 16(DI), SP
	CLD
	REP; MOVSB
	SUBQ $(8*16), SI

	MOVQ 0(SP), CX
	MOVQ 8(SP), DX
	MOVQ 16(SP), R8
	MOVQ 24(SP), R9
	CALL BP

	MOVQ SI, SP
	RET

TEXT rebase(SB), 1, $-4
	MOVQ BP, AX
	RET

#include "mem.h"

TEXT jump(SB), 1, $-4
	CLI

	/* load zero length idt */
	MOVL	$_idtptr64p<>(SB), AX
	MOVL	(AX), IDTR

	/* load temporary gdt */
	MOVL	$_gdtptr64p<>(SB), AX
	MOVL	(AX), GDTR

	/* load CS with 32bit code segment */
	PUSHQ	$SELECTOR(3, SELGDT, 0)
	PUSHQ	$_warp32<>(SB)
	RETFQ

MODE $32

TEXT	_warp32<>(SB), 1, $-4

	/* load 32bit data segments */
	MOVL	$SELECTOR(2, SELGDT, 0), AX
	MOVW	AX, DS
	MOVW	AX, ES
	MOVW	AX, FS
	MOVW	AX, GS
	MOVW	AX, SS

	/* turn off paging */
	MOVL	CR0, AX
	ANDL	$0x7fffffff, AX		/* ~(PG) */
	MOVL	AX, CR0

	MOVL	$0, AX
	MOVL	AX, CR3

	/* disable long mode */
	MOVL	$0xc0000080, CX		/* Extended Feature Enable */
	RDMSR
	ANDL	$0xfffffeff, AX		/* Long Mode Disable */
	WRMSR

	/* diable pae */
	MOVL	CR4, AX
	ANDL	$0xffffff5f, AX		/* ~(PAE|PGE) */
	MOVL	AX, CR4

	JMP	*BP

TEXT _gdt<>(SB), 1, $-4
	/* null descriptor */
	LONG	$0
	LONG	$0

	/* (KESEG) 64 bit long mode exec segment */
	LONG	$(0xFFFF)
	LONG	$(SEGL|SEGG|SEGP|(0xF<<16)|SEGPL(0)|SEGEXEC|SEGR)

	/* 32 bit data segment descriptor for 4 gigabytes (PL 0) */
	LONG	$(0xFFFF)
	LONG	$(SEGG|SEGB|(0xF<<16)|SEGP|SEGPL(0)|SEGDATA|SEGW)

	/* 32 bit exec segment descriptor for 4 gigabytes (PL 0) */
	LONG	$(0xFFFF)
	LONG	$(SEGG|SEGD|(0xF<<16)|SEGP|SEGPL(0)|SEGEXEC|SEGR)

TEXT _gdtptr64p<>(SB), 1, $-4
	WORD	$(4*8-1)
	QUAD	$_gdt<>(SB)

TEXT _idtptr64p<>(SB), 1, $-4
	WORD	$0
	QUAD	$0

GLOBL	confaddr(SB), $8
DATA	confaddr(SB)/8, $CONFADDR
