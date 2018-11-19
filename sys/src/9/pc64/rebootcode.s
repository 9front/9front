#include "mem.h"

MODE $64

/*
 * Turn off MMU, then memmove the new kernel to its correct location
 * in physical memory.  Then jumps the to start of the kernel.
 */

TEXT	main(SB), 1, $-4
	MOVL	RARG, DI		/* destination */
	MOVL	p2+8(FP), SI		/* source */
	MOVL	n+16(FP), BX		/* byte count */

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

	MOVL	BX, CX			/* byte count */
	MOVL	DI, AX			/* save entry point */
	MOVL	AX, SP			/* move stack below entry */

	/* park cpu for zero entry point */
	ORL	AX, AX
	JZ	_idle


/*
 * the source and destination may overlap.
 * determine whether to copy forward or backwards
 */
	CMPL	SI, DI
	JGT	_forward
	MOVL	SI, DX
	ADDL	CX, DX
	CMPL	DX, DI
	JGT	_back

_forward:
	CLD
	REP;	MOVSB

_startkernel:
	/* jump to entry point */
	JMP*	AX

_back:
	ADDL	CX, DI
	ADDL	CX, SI
	SUBL	$1, DI
	SUBL	$1, SI
	STD
	REP;	MOVSB
	JMP	_startkernel

_idle:
	HLT
	JMP	_idle

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
