/*
 * Start an Application Processor. This must be placed on a 4KB boundary
 * somewhere in the 1st MB of conventional memory (APBOOTSTRAP). However,
 * due to some shortcuts below it's restricted further to within the 1st
 * 64KB. The AP starts in real-mode, with
 *   CS selector set to the startup memory address/16;
 *   CS base set to startup memory address;
 *   CS limit set to 64KB;
 *   CPL and IP set to 0.
 */
#include "mem.h"

#define NOP		BYTE $0x90		/* NOP */

#define pFARJMP32(s, o)	BYTE $0xea;		/* far jmp ptr32:16 */	\
			LONG $o; WORD $s
#define rFARJMP16(s, o)	BYTE $0xea;		/* far jump ptr16:16 */	\
			WORD $o; WORD $s;
#define rFARJMP32(s, o)	BYTE $0x66;		/* far jump ptr32:16 */	\
			pFARJMP32(s, o)

#define rLGDT(gdtptr)	BYTE $0x0f;		/* LGDT */		\
			BYTE $0x01; BYTE $0x16;				\
			WORD $gdtptr

#define rMOVAX(i)	BYTE $0xb8;		/* i -> AX */		\
			WORD $i;

#define	DELAY		BYTE $0xEB;		/* JMP .+2 */			\
			BYTE $0x00

MODE $16

TEXT apbootstrap(SB), 1, $-4 
	rFARJMP16(0, _apbootstrap-KZERO(SB))
	NOP; NOP; NOP;
TEXT _apvector(SB), 1, $-4 			/* address APBOOTSTRAP+0x08 */
	QUAD $0
TEXT _appml4(SB), 1, $-4 			/* address APBOOTSTRAP+0x10 */
	QUAD $0
TEXT _apapic(SB), 1, $-4 			/* address APBOOTSTRAP+0x18 */
	QUAD $0
TEXT _apmach(SB), 1, $-4 			/* address APBOOTSTRAP+0x20 */
	QUAD $0
TEXT _apbootstrap(SB), 1, $-4 
	MOVW	CS, AX
	MOVW	AX, DS				/* initialise DS */

	rLGDT(_gdtptr32p<>-KZERO(SB))		/* load a basic gdt */

	MOVL	CR0, AX
	ORL	$1, AX
	MOVL	AX, CR0				/* turn on protected mode */
	DELAY					/* JMP .+2 */

	rFARJMP16(SELECTOR(3, SELGDT, 0), _ap32-KZERO(SB))

/*
 * Enable and activate Long Mode. From the manual:
 * 	make sure Page Size Extentions are off, and Page Global
 *	Extensions and Physical Address Extensions are on in CR4;
 *	set Long Mode Enable in the Extended Feature Enable MSR;
 *	set Paging Enable in CR0;
 *	make an inter-segment jump to the Long Mode code.
 * It's all in 32-bit mode until the jump is made.
 */
MODE $32

TEXT _ap32(SB), 1, $-4
	MOVW	$SELECTOR(2, SELGDT, 0), AX
	MOVW	AX, DS
	MOVW	AX, ES
	MOVW	AX, FS
	MOVW	AX, GS
	MOVW	AX, SS

	MOVL	_appml4-KZERO(SB), AX	/* physical address of PML4 */
	MOVL	AX, CR3			/* load the mmu */
	DELAY

	MOVL	CR4, AX
	ANDL	$~0x00000010, AX		/* Page Size */
	ORL	$0x000000A0, AX			/* Page Global, Phys. Address */
	MOVL	AX, CR4

	MOVL	$0xc0000080, CX			/* Extended Feature Enable */
	RDMSR
	ORL	$0x00000100, AX			/* Long Mode Enable */
	WRMSR

	MOVL	CR0, DX
	ANDL	$~0x6000000a, DX
	ORL	$0x80010000, DX			/* Paging Enable, Write Protect */
	MOVL	DX, CR0

	pFARJMP32(SELECTOR(KESEG, SELGDT, 0), _ap64-KZERO(SB))

/*
 * Long mode. Welcome to 2003.
 * Jump out of the identity map space;
 * load a proper long mode GDT;
 * initialise the stack and call the
 * C startup code in m->splpc.
 */
MODE $64

TEXT _ap64(SB), 1, $-4
	MOVQ	$_gdtptr64v<>(SB), AX
	MOVL	(AX), GDTR

	XORQ	AX, AX
	MOVW	AX, DS				/* not used in long mode */
	MOVW	AX, ES				/* not used in long mode */
	MOVW	AX, FS
	MOVW	AX, GS
	MOVW	AX, SS				/* not used in long mode */

	MOVW	AX, LDTR

	MOVQ	_apmach(SB), SP

	MOVQ	AX, RUSER			/* up = 0; */
	MOVQ	SP, RMACH			/* m = apmach */

	ADDQ	$MACHSIZE, SP

	PUSHQ	AX				/* clear flags */
	POPFQ

	MOVQ	_apvector(SB), AX
	MOVQ	_apapic(SB), RARG
	PUSHQ	RARG

	CALL	*AX

_halt:
	HLT
	JMP _halt
	
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


TEXT _gdtptr32p<>(SB), 1, $-4
	WORD	$(4*8-1)
	LONG	$_gdt<>-KZERO(SB)

TEXT _gdtptr64p<>(SB), 1, $-4
	WORD	$(4*8-1)
	QUAD	$_gdt<>-KZERO(SB)

TEXT _gdtptr64v<>(SB), 1, $-4
	WORD	$(4*8-1)
	QUAD	$_gdt<>(SB)
