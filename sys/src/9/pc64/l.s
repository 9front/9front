#include "mem.h"

MODE $32

#define DELAY		BYTE $0xEB; BYTE $0x00	/* JMP .+2 */

#define pFARJMP32(s, o)	BYTE $0xea;		/* far jump to ptr32:16 */\
			LONG $o; WORD $s

/*
 * Enter here in 32-bit protected mode. Welcome to 1982.
 * Make sure the GDT is set as it should be:
 *	disable interrupts;
 *	load the GDT with the table in _gdt32p;
 *	load all the data segments
 *	load the code segment via a far jump.
 */
TEXT _protected<>(SB), 1, $-4
	CLI

	MOVL	$_gdtptr32p<>-KZERO(SB), AX
	MOVL	(AX), GDTR

	MOVL	$SELECTOR(2, SELGDT, 0), AX
	MOVW	AX, DS
	MOVW	AX, ES
	MOVW	AX, FS
	MOVW	AX, GS
	MOVW	AX, SS

	pFARJMP32(SELECTOR(3, SELGDT, 0), _warp64<>-KZERO(SB))

	BYTE	$0x90	/* align */

/*
 * Must be 4-byte aligned.
 */
TEXT _multibootheader<>(SB), 1, $-4
	LONG	$0x1BADB002			/* magic */
	LONG	$0x00010007			/* flags */
	LONG	$-(0x1BADB002 + 0x00010007)	/* checksum */
	LONG	$_multibootheader<>-KZERO(SB)	/* header_addr */
	LONG	$_protected<>-KZERO(SB)		/* load_addr */
	LONG	$edata-KZERO(SB)		/* load_end_addr */
	LONG	$end-KZERO(SB)			/* bss_end_addr */
	LONG	$_multibootentry<>-KZERO(SB)	/* entry_addr */
	LONG	$0				/* mode_type */
	LONG	$0				/* width */
	LONG	$0				/* height */
	LONG	$32				/* depth */

/* 
 * the kernel expects the data segment to be page-aligned
 * multiboot bootloaders put the data segment right behind text
 */
TEXT _multibootentry<>(SB), 1, $-4
	MOVL	$etext-KZERO(SB), SI
	MOVL	SI, DI
	ADDL	$(BY2PG-1), DI
	ANDL	$~(BY2PG-1), DI
	MOVL	$edata-KZERO(SB), CX
	SUBL	DI, CX
	ADDL	CX, SI
	ADDL	CX, DI
	INCL	CX	/* one more for post decrement */
	STD
	REP; MOVSB
	MOVL	BX, multibootptr-KZERO(SB)
	MOVL	$_protected<>-KZERO(SB), AX
	JMP*	AX

/* multiboot structure pointer (physical address) */
TEXT multibootptr(SB), 1, $-4
	LONG	$0

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

/*
 * Macros for accessing page table entries; change the
 * C-style array-index macros into a page table byte offset
 */
#define PML4O(v)	((PTLX((v), 3))<<3)
#define PDPO(v)		((PTLX((v), 2))<<3)
#define PDO(v)		((PTLX((v), 1))<<3)
#define PTO(v)		((PTLX((v), 0))<<3)

TEXT _warp64<>(SB), 1, $-4

	/* clear mach and page tables */
	MOVL	$((CPU0END-CPU0PML4)>>2), CX
	MOVL	$(CPU0PML4-KZERO), SI
	MOVL	SI, DI
	XORL	AX, AX
	CLD
	REP;	STOSL

	MOVL	SI, AX				/* PML4 */
	MOVL	AX, DX
	ADDL	$(PTSZ|PTEWRITE|PTEVALID), DX	/* PDP at PML4 + PTSZ */
	MOVL	DX, PML4O(0)(AX)		/* PML4E for double-map */
	MOVL	DX, PML4O(KZERO)(AX)		/* PML4E for KZERO */

	ADDL	$PTSZ, AX			/* PDP at PML4 + PTSZ */
	ADDL	$PTSZ, DX			/* PD0 at PML4 + 2*PTSZ */
	MOVL	DX, PDPO(0)(AX)			/* PDPE for double-map */
	MOVL	DX, PDPO(KZERO)(AX)		/* PDPE for KZERO */

	/*
	 * add PDPE for KZERO+1GB early as Vmware
	 * hangs when modifying kernel PDP
	 */
	ADDL	$PTSZ, DX			/* PD1 */
	MOVL	DX, PDPO(KZERO+GiB)(AX)

	ADDL	$PTSZ, AX			/* PD0 at PML4 + 2*PTSZ */
	MOVL	$(PTESIZE|PTEGLOBAL|PTEWRITE|PTEVALID), DX
	MOVL	DX, PDO(0)(AX)			/* PDE for double-map */

	/*
	 * map from KZERO to end using 2MB pages
	 */
	ADDL	$PDO(KZERO), AX
	MOVL	$end-KZERO(SB), CX

	ADDL	$(16*1024), CX			/* qemu puts multiboot data after the kernel */

	ADDL	$(PGLSZ(1)-1), CX
	ANDL	$~(PGLSZ(1)-1), CX
	MOVL	CX, MemMin-KZERO(SB)		/* see memory.c */
	SHRL	$(1*PTSHIFT+PGSHIFT), CX
memloop:
	MOVL	DX, (AX)
	ADDL	$PGLSZ(1), DX
	ADDL	$8, AX
	LOOP	memloop

/*
 * Enable and activate Long Mode. From the manual:
 * 	make sure Page Size Extentions are off, and Page Global
 *	Extensions and Physical Address Extensions are on in CR4;
 *	set Long Mode Enable in the Extended Feature Enable MSR;
 *	set Paging Enable in CR0;
 *	make an inter-segment jump to the Long Mode code.
 * It's all in 32-bit mode until the jump is made.
 */
TEXT _lme<>(SB), 1, $-4
	MOVL	SI, CR3				/* load the mmu */
	DELAY

	MOVL	CR4, AX
	ANDL	$~0x00000010, AX			/* Page Size */
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

	pFARJMP32(SELECTOR(KESEG, SELGDT, 0), _identity<>-KZERO(SB))

/*
 * Long mode. Welcome to 2003.
 * Jump out of the identity map space;
 * load a proper long mode GDT.
 */
MODE $64

TEXT _identity<>(SB), 1, $-4
	MOVQ	$_start64v<>(SB), AX
	JMP*	AX

TEXT _start64v<>(SB), 1, $-4
	MOVQ	$_gdtptr64v<>(SB), AX
	MOVL	(AX), GDTR

	XORQ	AX, AX
	MOVW	AX, DS				/* not used in long mode */
	MOVW	AX, ES				/* not used in long mode */
	MOVW	AX, FS
	MOVW	AX, GS
	MOVW	AX, SS				/* not used in long mode */

	MOVW	AX, LDTR

	MOVQ	$(CPU0MACH+MACHSIZE), SP
	MOVQ	$(CPU0MACH), RMACH
	MOVQ	AX, RUSER			/* up = 0; */

_clearbss:
	MOVQ	$edata(SB), DI
	MOVQ	$end(SB), CX
	SUBQ	DI, CX				/* end-edata bytes */
	SHRQ	$2, CX				/* end-edata doublewords */

	CLD
	REP;	STOSL				/* clear BSS */

	PUSHQ	AX				/* clear flags */
	POPFQ

	CALL	main(SB)

/*
 * Park a processor. Should never fall through a return from main to here,
 * should only be called by application processors when shutting down.
 */
TEXT idle(SB), 1, $-4
_idle:
	STI
	HLT
	JMP	_idle

/*
 * The CPUID instruction is always supported on the amd64.
 */
TEXT cpuid(SB), $-4
	MOVL	RARG, AX			/* function in AX */
	CPUID

	MOVQ	info+8(FP), BP
	MOVL	AX, 0(BP)
	MOVL	BX, 4(BP)
	MOVL	CX, 8(BP)
	MOVL	DX, 12(BP)
	RET

/*
 * Port I/O.
 */
TEXT inb(SB), 1, $-4
	MOVL	RARG, DX			/* MOVL	port+0(FP), DX */
	XORL	AX, AX
	INB
	RET

TEXT insb(SB), 1, $-4
	MOVL	RARG, DX			/* MOVL	port+0(FP), DX */
	MOVQ	address+8(FP), DI
	MOVL	count+16(FP), CX
	CLD
	REP;	INSB
	RET

TEXT ins(SB), 1, $-4
	MOVL	RARG, DX			/* MOVL	port+0(FP), DX */
	XORL	AX, AX
	INW
	RET

TEXT inss(SB), 1, $-4
	MOVL	RARG, DX			/* MOVL	port+0(FP), DX */
	MOVQ	address+8(FP), DI
	MOVL	count+16(FP), CX
	CLD
	REP;	INSW
	RET

TEXT inl(SB), 1, $-4
	MOVL	RARG, DX			/* MOVL	port+0(FP), DX */
	INL
	RET

TEXT insl(SB), 1, $-4
	MOVL	RARG, DX			/* MOVL	port+0(FP), DX */
	MOVQ	address+8(FP), DI
	MOVL	count+16(FP), CX
	CLD
	REP; INSL
	RET

TEXT outb(SB), 1, $-1
	MOVL	RARG, DX			/* MOVL	port+0(FP), DX */
	MOVL	byte+8(FP), AX
	OUTB
	RET

TEXT outsb(SB), 1, $-4
	MOVL	RARG, DX			/* MOVL	port+0(FP), DX */
	MOVQ	address+8(FP), SI
	MOVL	count+16(FP), CX
	CLD
	REP; OUTSB
	RET

TEXT outs(SB), 1, $-4
	MOVL	RARG, DX			/* MOVL	port+0(FP), DX */
	MOVL	short+8(FP), AX
	OUTW
	RET

TEXT outss(SB), 1, $-4
	MOVL	RARG, DX			/* MOVL	port+0(FP), DX */
	MOVQ	address+8(FP), SI
	MOVL	count+16(FP), CX
	CLD
	REP; OUTSW
	RET

TEXT outl(SB), 1, $-4
	MOVL	RARG, DX			/* MOVL	port+0(FP), DX */
	MOVL	long+8(FP), AX
	OUTL
	RET

TEXT outsl(SB), 1, $-4
	MOVL	RARG, DX			/* MOVL	port+0(FP), DX */
	MOVQ	address+8(FP), SI
	MOVL	count+16(FP), CX
	CLD
	REP; OUTSL
	RET

TEXT getgdt(SB), 1, $-4
	MOVQ	RARG, AX
	MOVL	GDTR, (AX)			/* Note: 10 bytes returned */
	RET

TEXT lgdt(SB), $0				/* GDTR - global descriptor table */
	MOVQ	RARG, AX
	MOVL	(AX), GDTR
	RET

TEXT lidt(SB), $0				/* IDTR - interrupt descriptor table */
	MOVQ	RARG, AX
	MOVL	(AX), IDTR
	RET

TEXT ltr(SB), 1, $-4
	MOVW	RARG, AX
	MOVW	AX, TASK
	RET

/*
 * Read/write various system registers.
 */
TEXT getcr0(SB), 1, $-4				/* Processor Control */
	MOVQ	CR0, AX
	RET

TEXT putcr0(SB), 1, $-4
	MOVQ	RARG, CR0
	RET

TEXT getcr2(SB), 1, $-4				/* #PF Linear Address */
	MOVQ	CR2, AX
	RET

TEXT putcr2(SB), 1, $-4
	MOVQ	BP, CR2
	RET

TEXT getcr3(SB), 1, $-4				/* PML4 Base */
	MOVQ	CR3, AX
	RET

TEXT putcr3(SB), 1, $-4
	MOVQ	RARG, CR3
	RET

TEXT getcr4(SB), 1, $-4				/* Extensions */
	MOVQ	CR4, AX
	RET

TEXT putcr4(SB), 1, $-4
	MOVQ	RARG, CR4
	RET

TEXT mb386(SB), 1, $-4				/* hack */
TEXT mb586(SB), 1, $-4
	XORL	AX, AX
	CPUID
	RET

/*
 * BIOS32.
 */
TEXT bios32call(SB), 1, $-4
	XORL	AX, AX
	INCL	AX
	RET

/*
 * Basic timing loop to determine CPU frequency.
 */
TEXT aamloop(SB), 1, $-4
	MOVL	RARG, CX
_aamloop:
	LOOP	_aamloop
	RET

TEXT _cycles(SB), 1, $-4			/* time stamp counter */
	RDTSC
	MOVL	AX, 0(RARG)			/* lo */
	MOVL	DX, 4(RARG)			/* hi */
	RET

TEXT rdmsr(SB), 1, $-4				/* Model-Specific Register */
	MOVL	RARG, CX
	MOVQ	$0, BP
TEXT _rdmsrinst(SB), $0
	RDMSR
	MOVQ	vlong+8(FP), CX			/* &vlong */
	MOVL	AX, 0(CX)			/* lo */
	MOVL	DX, 4(CX)			/* hi */
	MOVQ	BP, AX				/* BP set to -1 if traped */
	RET
	
TEXT wrmsr(SB), 1, $-4
	MOVL	RARG, CX
	MOVL	lo+8(FP), AX
	MOVL	hi+12(FP), DX
	MOVQ	$0, BP
TEXT _wrmsrinst(SB), $0
	WRMSR
	MOVQ	BP, AX				/* BP set to -1 if traped */
	RET

/* fault-proof memcpy */
TEXT peek(SB), 1, $-4
	MOVQ	RARG, SI
	MOVQ	dst+8(FP), DI
	MOVL	cnt+16(FP), CX
	CLD
TEXT _peekinst(SB), $0
	REP; MOVSB
	MOVL	CX, AX
	RET
	

TEXT invlpg(SB), 1, $-4
	INVLPG	(RARG)
	RET

TEXT wbinvd(SB), 1, $-4
	WBINVD
	RET

/*
 * Serialisation.
 */
TEXT lfence(SB), 1, $-4
	LFENCE
	RET

TEXT mfence(SB), 1, $-4
	MFENCE
	RET

TEXT sfence(SB), 1, $-4
	SFENCE
	RET

/*
 * Note: CLI and STI are not serialising instructions.
 * Is that assumed anywhere?
 */
TEXT splhi(SB), 1, $-4
_splhi:
	PUSHFQ
	POPQ	AX
	TESTQ	$0x200, AX			/* 0x200 - Interrupt Flag */
	JZ	_alreadyhi			/* use CMOVLEQ etc. here? */

	MOVQ	(SP), BX
	MOVQ	BX, 8(RMACH) 			/* save PC in m->splpc */

_alreadyhi:
	CLI
	RET

TEXT spllo(SB), 1, $-4
_spllo:
	PUSHFQ
	POPQ	AX
	TESTQ	$0x200, AX			/* 0x200 - Interrupt Flag */
	JNZ	_alreadylo			/* use CMOVLEQ etc. here? */

	MOVQ	$0, 8(RMACH)			/* clear m->splpc */

_alreadylo:
	STI
	RET

TEXT splx(SB), 1, $-4
	TESTQ	$0x200, RARG			/* 0x200 - Interrupt Flag */
	JNZ	_spllo
	JMP	_splhi

TEXT spldone(SB), 1, $-4
	RET

TEXT islo(SB), 1, $-4
	PUSHFQ
	POPQ	AX
	ANDQ	$0x200, AX			/* 0x200 - Interrupt Flag */
	RET

/*
 * Synchronisation
 */
TEXT tas(SB), 1, $-4
TEXT _tas(SB), 1, $-4
	MOVL	$0xdeaddead, AX
	XCHGL	AX, (RARG)			/*  */
	RET

TEXT cmpswap486(SB), 1, $-4
TEXT cas(SB), 1, $-4
	MOVL	exp+8(FP), AX
	MOVL	new+16(FP), BX
	LOCK; CMPXCHGL BX, (RARG)
	MOVL	$1, AX				/* use CMOVLEQ etc. here? */
	JNZ	_cas32r0
_cas32r1:
	RET
_cas32r0:
	DECL	AX
	RET

/*
 * Label consists of a stack pointer and a programme counter
 */
TEXT gotolabel(SB), 1, $-4
	MOVQ	0(RARG), SP			/* restore SP */
	MOVQ	8(RARG), AX			/* put return PC on the stack */
	MOVQ	AX, 0(SP)
	MOVL	$1, AX				/* return 1 */
	RET

TEXT setlabel(SB), 1, $-4
	MOVQ	SP, 0(RARG)			/* store SP */
	MOVQ	0(SP), BX			/* store return PC */
	MOVQ	BX, 8(RARG)
	MOVL	$0, AX				/* return 0 */
	RET

TEXT halt(SB), 1, $-4
	CLI
	CMPL	nrdy(SB), $0
	JEQ	_nothingready
	STI
	RET
_nothingready:
	STI
	HLT
	RET

TEXT mwait(SB), 1, $-4
	MOVQ	RARG, AX
	MOVL	(AX), CX
	ORL	CX, CX
	JNZ	_mwaitdone
	XORL	DX, DX
	BYTE $0x0f; BYTE $0x01; BYTE $0xc8	/* MONITOR */
	MOVL	(AX), CX
	ORL	CX, CX
	JNZ	_mwaitdone
	XORL	AX, AX
	BYTE $0x0f; BYTE $0x01; BYTE $0xc9	/* MWAIT */
_mwaitdone:
	RET

/*
 * SIMD Floating Point.
 * Note: for x87 instructions which have both a 'wait' and 'nowait' version,
 * 8a only knows the 'wait' mnemonic but does NOT insertthe WAIT prefix byte
 * (i.e. they act like their FNxxx variations) so WAIT instructions must be
 * explicitly placed in the code if necessary.
 */
TEXT _clts(SB), 1, $-4
	CLTS
	RET

TEXT _fldcw(SB), 1, $-4				/* Load x87 FPU Control Word */
	MOVQ	RARG, cw+0(FP)
	FLDCW	cw+0(FP)
	RET

TEXT _fnclex(SB), 1, $-4
	FCLEX
	RET

TEXT _fninit(SB), 1, $-4
	FINIT					/* no WAIT */
	RET

TEXT _fxrstor(SB), 1, $-4
	FXRSTOR64 (RARG)
	RET

TEXT _fxsave(SB), 1, $-4
	FXSAVE64 (RARG)
	RET

TEXT _fwait(SB), 1, $-4
	WAIT
	RET

TEXT _ldmxcsr(SB), 1, $-4			/* Load MXCSR */
	MOVQ	RARG, mxcsr+0(FP)
	LDMXCSR	mxcsr+0(FP)
	RET

TEXT _stts(SB), 1, $-4
	MOVQ	CR0, AX
	ORQ	$8, AX				/* Ts */
	MOVQ	AX, CR0
	RET

TEXT mul64fract(SB), 1, $-4
	MOVQ	a+8(FP), AX
	MULQ	b+16(FP)			/* a*b */
	SHRQ	$32, AX:DX
	MOVQ	AX, (RARG)
	RET

#define	RDRANDAX	BYTE $0x0f; BYTE $0xc7; BYTE $0xf0
#define	RDRAND64AX	BYTE $0x48; BYTE $0x0f; BYTE $0xc7;  BYTE $0xf0

TEXT rdrand32(SB), $-4
loop32:
	RDRANDAX
	JCC		loop32
	RET

TEXT rdrand64(SB), $-4
loop64:
	RDRAND64AX
	JCC		loop64
	RET

TEXT rdrandbuf(SB), $0
	MOVQ	RARG, DX

	MOVLQZX	cnt+8(FP), CX
	SHRQ	$3, CX
eights:
	CMPL	CX, $0
	JLE	f1
	CALL	rdrand64(SB)
	MOVQ	AX, 0(DX)
	ADDQ	$8, DX
	SUBL	$1, CX
	JMP	eights

f1:
	MOVLQZX	cnt+8(FP), CX
	ANDL	$7, CX
	SHRQ	$2, CX
fours:
	CMPL	CX, $0
	JLE	f2
	CALL	rdrand32(SB)
	MOVL	AX, 0(DX)
	ADDQ	$4, DX
	SUBL	$1, CX
	JMP	fours

f2:
	MOVLQZX	cnt+8(FP), CX
	ANDL	$3, CX
ones:
	CMPL	CX, $0
	JLE	f3
	CALL	rdrand32(SB)
	MOVB	AX, 0(DX)
	ADDQ	$1, DX
	SUBL	$1, CX
	JMP	ones

f3:
	RET

/* debug register access */

TEXT putdr(SB), 1, $-4
	MOVQ	56(BP), AX
	MOVQ	AX, DR7
	/* wet floor */
TEXT putdr01236(SB), 1, $-4
	MOVQ	0(BP), AX
	MOVQ	AX, DR0
	MOVQ	8(BP), AX
	MOVQ	AX, DR1
	MOVQ	16(BP), AX
	MOVQ	AX, DR2
	MOVQ	24(BP), AX
	MOVQ	AX, DR3
	MOVQ	48(BP), AX
	MOVQ	AX, DR6
	RET

TEXT getdr6(SB), 1, $-4
	MOVQ	DR6, AX
	RET

TEXT putdr6(SB), 1, $-4
	MOVQ	BP, DR6
	RET

TEXT putdr7(SB), 1, $-4
	MOVQ	BP, DR7
	RET

/* VMX instructions */
TEXT vmxon(SB), 1, $-4
	MOVQ	BP, 8(SP)
	/* VMXON 8(SP) */
	BYTE	$0xf3; BYTE $0x0f; BYTE $0xc7; BYTE $0x74; BYTE $0x24; BYTE $0x08
	JMP	_vmout

TEXT vmxoff(SB), 1, $-4
	BYTE	$0x0f; BYTE $0x01; BYTE $0xc4
	JMP	_vmout

TEXT vmclear(SB), 1, $-4
	MOVQ	BP, 8(SP)
	/* VMCLEAR 8(SP) */
	BYTE	$0x66;	BYTE $0x0f; BYTE $0xc7; BYTE $0x74; BYTE $0x24; BYTE $0x08
	JMP	_vmout

TEXT vmlaunch(SB), 1, $-4
	MOVL	$0x6C14, DI
	MOVQ	SP, DX
	BYTE	$0x0f; BYTE $0x79; BYTE $0xfa /* VMWRITE DX, DI */
	JBE	_vmout
	MOVL	$0x6C16, DI
	MOVQ	$vmrestore(SB), DX
	BYTE	$0x0f; BYTE $0x79; BYTE $0xfa /* VMWRITE DX, DI */
	JBE	_vmout
	
	MOVQ	BP, ureg+0(FP)
	MOVL	resume+8(FP), AX
	TESTL	AX, AX
	MOVQ	0x00(BP), AX
	MOVQ	0x08(BP), BX
	MOVQ	0x10(BP), CX
	MOVQ	0x18(BP), DX
	MOVQ	0x20(BP), SI
	MOVQ	0x28(BP), DI
	MOVQ	0x38(BP), R8
	MOVQ	0x40(BP), R9
	MOVQ	0x48(BP), R10
	MOVQ	0x50(BP), R11
	MOVQ	0x58(BP), R12
	MOVQ	0x60(BP), R13
	MOVQ	0x68(BP), R14
	MOVQ	0x70(BP), R15
	MOVQ	0x30(BP), BP
	JNE	_vmresume
	BYTE	$0x0f; BYTE $0x01; BYTE	$0xc2 /* VMLAUNCH */
	JMP	_vmout
_vmresume:
	BYTE	$0x0f; BYTE $0x01; BYTE $0xc3 /* VMRESUME */
	JMP _vmout
	
TEXT vmrestore(SB), 1, $-4
	PUSHQ	BP
	MOVQ	ureg+0(FP), BP
	MOVQ	AX, 0x00(BP)
	MOVQ	BX, 0x08(BP)
	MOVQ	CX, 0x10(BP)
	MOVQ	DX, 0x18(BP)
	MOVQ	SI, 0x20(BP)
	MOVQ	DI, 0x28(BP)
	POPQ	0x30(BP)
	MOVQ	R8, 0x38(BP)
	MOVQ	R9, 0x40(BP)
	MOVQ	R10, 0x48(BP)
	MOVQ	R11, 0x50(BP)
	MOVQ	R12, 0x58(BP)
	MOVQ	R13, 0x60(BP)
	MOVQ	R14, 0x68(BP)
	MOVQ	R15, 0x70(BP)
	
	BYTE	$0x65; MOVQ 0, RMACH /* MOVQ GS:(0), RMACH */
	MOVQ	16(RMACH), RUSER
	XORL	AX, AX
	RET

TEXT vmptrld(SB), 1, $-4
	MOVQ	BP, 8(SP)
	/* VMMPTRLD 8(SP) */
	BYTE	$0x0f; BYTE $0xc7; BYTE $0x74; BYTE $0x24; BYTE $0x08
	JMP _vmout

TEXT vmwrite(SB), 1, $-4
	MOVQ	val+8(FP), DX
	/* VMWRITE DX, BP */
	BYTE	$0x0f; BYTE $0x79; BYTE $0xea
	JMP _vmout

TEXT vmread(SB), 1, $-4
	MOVQ	valp+8(FP), DI
	/* VMREAD BP, (DI) */
	BYTE	$0x0f; BYTE $0x78; BYTE $0x2f
	JMP _vmout

TEXT invept(SB), 1, $-4
	/* INVEPT BP, 16(SP) */
	BYTE	$0x66; BYTE $0x0f; BYTE $0x38; BYTE $0x80; BYTE $0x6c; BYTE $0x24; BYTE $0x10
	JMP _vmout

TEXT invvpid(SB), 1, $-4
	/* INVVPID BP, 16(SP) */
	BYTE	$0x66; BYTE $0x0f; BYTE $0x38; BYTE $0x81; BYTE $0x6c; BYTE $0x24; BYTE $0x10
	JMP _vmout

_vmout:
	JC	_vmout1
	JZ	_vmout2
	XORL	AX, AX
	RET
_vmout1:
	MOVQ	$-1, AX
	RET
_vmout2:
	MOVQ	$-2, AX
	RET

/*
 */
TEXT touser(SB), 1, $-4
	CLI
	SWAPGS

	MOVL	$0, RMACH
	MOVL	$0, RUSER

	MOVQ	$(UTZERO+0x28), CX		/* ip */
	MOVL	$0x200, R11			/* flags */
	MOVQ	RARG, SP			/* sp */

	BYTE $0x48; SYSRET			/* SYSRETQ */

/*
 */
TEXT syscallentry(SB), 1, $-4
	SWAPGS
	BYTE $0x65; MOVQ 0, AX			/* m-> (MOVQ GS:0x0, AX) */
	MOVQ	16(AX), BX			/* m->proc */
	MOVQ	SP, R13
	MOVQ	16(BX), SP			/* m->proc->kstack */
	ADDQ	$KSTACK, SP

	PUSHQ	$UDSEL				/* old stack segment */
	PUSHQ	R13				/* old sp */
	PUSHQ	R11				/* old flags */
	PUSHQ	$UESEL				/* old code segment */
	PUSHQ	CX				/* old ip */
	PUSHQ	$0				/* error code */
	PUSHQ	$64				/* trap number (VectorSYSCALL) */

	SUBQ	$(8 + 23*8-7*8), SP		/* arg + sizeof(Ureg)-pushed */

	MOVQ	RMACH, (15*8)(SP)		/* old r15 */
	MOVQ	RUSER, (14*8)(SP)		/* old r14 */

	MOVQ	RARG, (7*8)(SP)			/* system call number */

	MOVQ	AX, RMACH			/* m */
	MOVQ	BX, RUSER			/* up */

	LEAQ	8(SP), RARG			/* Ureg* arg */

	CALL	syscall(SB)

TEXT forkret(SB), 1, $-4
	CLI
	SWAPGS

	MOVQ	8(SP), AX			/* return value */

	MOVQ	(15*8)(SP), RMACH		/* r15 */
	MOVQ	(14*8)(SP), RUSER		/* r14 */

	MOVQ	(19*8)(SP), CX			/* ip */
	MOVQ	(21*8)(SP), R11			/* flags */
	MOVQ	(22*8)(SP), SP			/* sp */

	BYTE $0x48; SYSRET			/* SYSRETQ */

/*
 * Interrupt/exception handling.
 */

TEXT _strayintr(SB), 1, $-4			/* no error code pushed */
	PUSHQ	AX				/* save AX */
	MOVQ	8(SP), AX			/* vectortable(SB) PC */
	JMP	_intrcommon

TEXT _strayintrx(SB), 1, $-4			/* error code pushed */
	XCHGQ	AX, (SP)
_intrcommon:
	MOVBQZX	(AX), AX
	XCHGQ	AX, (SP)

	SUBQ	$24, SP				/* R1[45], [DEFG]S */
	CMPW	48(SP), $KESEL			/* old CS */
	JEQ	_intrnested

	MOVQ	RUSER, 0(SP)
	MOVQ	RMACH, 8(SP)

	SWAPGS
	BYTE $0x65; MOVQ 0, RMACH		/* m-> (MOVQ GS:0x0, R15) */
	MOVQ	16(RMACH), RUSER		/* up */

_intrnested:
	PUSHQ	R13
	PUSHQ	R12
	PUSHQ	R11
	PUSHQ	R10
	PUSHQ	R9
	PUSHQ	R8
	PUSHQ	BP
	PUSHQ	DI
	PUSHQ	SI
	PUSHQ	DX
	PUSHQ	CX
	PUSHQ	BX
	PUSHQ	AX

	MOVQ	SP, RARG
	PUSHQ	SP
	CALL	trap(SB)

TEXT _intrr(SB), 1, $-4
_intrestore:
	POPQ	AX

	POPQ	AX
	POPQ	BX
	POPQ	CX
	POPQ	DX
	POPQ	SI
	POPQ	DI
	POPQ	BP
	POPQ	R8
	POPQ	R9
	POPQ	R10
	POPQ	R11
	POPQ	R12
	POPQ	R13

	CMPQ	48(SP), $KESEL
	JEQ	_iretnested

	SWAPGS

	MOVQ	8(SP), RMACH
	MOVQ	0(SP), RUSER

_iretnested:
	ADDQ	$40, SP
	IRETQ

TEXT noteret(SB), 1, $-4
	CLI
	JMP	_intrestore

TEXT vectortable(SB), $0
	CALL _strayintr(SB); BYTE $0x00		/* divide error */
	CALL _strayintr(SB); BYTE $0x01		/* debug exception */
	CALL _strayintr(SB); BYTE $0x02		/* NMI interrupt */
	CALL _strayintr(SB); BYTE $0x03		/* breakpoint */
	CALL _strayintr(SB); BYTE $0x04		/* overflow */
	CALL _strayintr(SB); BYTE $0x05		/* bound */
	CALL _strayintr(SB); BYTE $0x06		/* invalid opcode */
	CALL _strayintr(SB); BYTE $0x07		/* no coprocessor available */
	CALL _strayintrx(SB); BYTE $0x08	/* double fault */
	CALL _strayintr(SB); BYTE $0x09		/* coprocessor segment overflow */
	CALL _strayintrx(SB); BYTE $0x0A	/* invalid TSS */
	CALL _strayintrx(SB); BYTE $0x0B	/* segment not available */
	CALL _strayintrx(SB); BYTE $0x0C	/* stack exception */
	CALL _strayintrx(SB); BYTE $0x0D	/* general protection error */
	CALL _strayintrx(SB); BYTE $0x0E	/* page fault */
	CALL _strayintr(SB); BYTE $0x0F		/*  */
	CALL _strayintr(SB); BYTE $0x10		/* coprocessor error */
	CALL _strayintrx(SB); BYTE $0x11	/* alignment check */
	CALL _strayintr(SB); BYTE $0x12		/* machine check */
	CALL _strayintr(SB); BYTE $0x13		/* simd error */
	CALL _strayintr(SB); BYTE $0x14
	CALL _strayintr(SB); BYTE $0x15
	CALL _strayintr(SB); BYTE $0x16
	CALL _strayintr(SB); BYTE $0x17
	CALL _strayintr(SB); BYTE $0x18
	CALL _strayintr(SB); BYTE $0x19
	CALL _strayintr(SB); BYTE $0x1A
	CALL _strayintr(SB); BYTE $0x1B
	CALL _strayintr(SB); BYTE $0x1C
	CALL _strayintr(SB); BYTE $0x1D
	CALL _strayintr(SB); BYTE $0x1E
	CALL _strayintr(SB); BYTE $0x1F
	CALL _strayintr(SB); BYTE $0x20		/* VectorLAPIC */
	CALL _strayintr(SB); BYTE $0x21
	CALL _strayintr(SB); BYTE $0x22
	CALL _strayintr(SB); BYTE $0x23
	CALL _strayintr(SB); BYTE $0x24
	CALL _strayintr(SB); BYTE $0x25
	CALL _strayintr(SB); BYTE $0x26
	CALL _strayintr(SB); BYTE $0x27
	CALL _strayintr(SB); BYTE $0x28
	CALL _strayintr(SB); BYTE $0x29
	CALL _strayintr(SB); BYTE $0x2A
	CALL _strayintr(SB); BYTE $0x2B
	CALL _strayintr(SB); BYTE $0x2C
	CALL _strayintr(SB); BYTE $0x2D
	CALL _strayintr(SB); BYTE $0x2E
	CALL _strayintr(SB); BYTE $0x2F
	CALL _strayintr(SB); BYTE $0x30
	CALL _strayintr(SB); BYTE $0x31
	CALL _strayintr(SB); BYTE $0x32
	CALL _strayintr(SB); BYTE $0x33
	CALL _strayintr(SB); BYTE $0x34
	CALL _strayintr(SB); BYTE $0x35
	CALL _strayintr(SB); BYTE $0x36
	CALL _strayintr(SB); BYTE $0x37
	CALL _strayintr(SB); BYTE $0x38
	CALL _strayintr(SB); BYTE $0x39
	CALL _strayintr(SB); BYTE $0x3A
	CALL _strayintr(SB); BYTE $0x3B
	CALL _strayintr(SB); BYTE $0x3C
	CALL _strayintr(SB); BYTE $0x3D
	CALL _strayintr(SB); BYTE $0x3E
	CALL _strayintr(SB); BYTE $0x3F
	CALL _strayintr(SB); BYTE $0x40		/* was VectorSYSCALL */
	CALL _strayintr(SB); BYTE $0x41
	CALL _strayintr(SB); BYTE $0x42
	CALL _strayintr(SB); BYTE $0x43
	CALL _strayintr(SB); BYTE $0x44
	CALL _strayintr(SB); BYTE $0x45
	CALL _strayintr(SB); BYTE $0x46
	CALL _strayintr(SB); BYTE $0x47
	CALL _strayintr(SB); BYTE $0x48
	CALL _strayintr(SB); BYTE $0x49
	CALL _strayintr(SB); BYTE $0x4A
	CALL _strayintr(SB); BYTE $0x4B
	CALL _strayintr(SB); BYTE $0x4C
	CALL _strayintr(SB); BYTE $0x4D
	CALL _strayintr(SB); BYTE $0x4E
	CALL _strayintr(SB); BYTE $0x4F
	CALL _strayintr(SB); BYTE $0x50
	CALL _strayintr(SB); BYTE $0x51
	CALL _strayintr(SB); BYTE $0x52
	CALL _strayintr(SB); BYTE $0x53
	CALL _strayintr(SB); BYTE $0x54
	CALL _strayintr(SB); BYTE $0x55
	CALL _strayintr(SB); BYTE $0x56
	CALL _strayintr(SB); BYTE $0x57
	CALL _strayintr(SB); BYTE $0x58
	CALL _strayintr(SB); BYTE $0x59
	CALL _strayintr(SB); BYTE $0x5A
	CALL _strayintr(SB); BYTE $0x5B
	CALL _strayintr(SB); BYTE $0x5C
	CALL _strayintr(SB); BYTE $0x5D
	CALL _strayintr(SB); BYTE $0x5E
	CALL _strayintr(SB); BYTE $0x5F
	CALL _strayintr(SB); BYTE $0x60
	CALL _strayintr(SB); BYTE $0x61
	CALL _strayintr(SB); BYTE $0x62
	CALL _strayintr(SB); BYTE $0x63
	CALL _strayintr(SB); BYTE $0x64
	CALL _strayintr(SB); BYTE $0x65
	CALL _strayintr(SB); BYTE $0x66
	CALL _strayintr(SB); BYTE $0x67
	CALL _strayintr(SB); BYTE $0x68
	CALL _strayintr(SB); BYTE $0x69
	CALL _strayintr(SB); BYTE $0x6A
	CALL _strayintr(SB); BYTE $0x6B
	CALL _strayintr(SB); BYTE $0x6C
	CALL _strayintr(SB); BYTE $0x6D
	CALL _strayintr(SB); BYTE $0x6E
	CALL _strayintr(SB); BYTE $0x6F
	CALL _strayintr(SB); BYTE $0x70
	CALL _strayintr(SB); BYTE $0x71
	CALL _strayintr(SB); BYTE $0x72
	CALL _strayintr(SB); BYTE $0x73
	CALL _strayintr(SB); BYTE $0x74
	CALL _strayintr(SB); BYTE $0x75
	CALL _strayintr(SB); BYTE $0x76
	CALL _strayintr(SB); BYTE $0x77
	CALL _strayintr(SB); BYTE $0x78
	CALL _strayintr(SB); BYTE $0x79
	CALL _strayintr(SB); BYTE $0x7A
	CALL _strayintr(SB); BYTE $0x7B
	CALL _strayintr(SB); BYTE $0x7C
	CALL _strayintr(SB); BYTE $0x7D
	CALL _strayintr(SB); BYTE $0x7E
	CALL _strayintr(SB); BYTE $0x7F
	CALL _strayintr(SB); BYTE $0x80		/* Vector[A]PIC */
	CALL _strayintr(SB); BYTE $0x81
	CALL _strayintr(SB); BYTE $0x82
	CALL _strayintr(SB); BYTE $0x83
	CALL _strayintr(SB); BYTE $0x84
	CALL _strayintr(SB); BYTE $0x85
	CALL _strayintr(SB); BYTE $0x86
	CALL _strayintr(SB); BYTE $0x87
	CALL _strayintr(SB); BYTE $0x88
	CALL _strayintr(SB); BYTE $0x89
	CALL _strayintr(SB); BYTE $0x8A
	CALL _strayintr(SB); BYTE $0x8B
	CALL _strayintr(SB); BYTE $0x8C
	CALL _strayintr(SB); BYTE $0x8D
	CALL _strayintr(SB); BYTE $0x8E
	CALL _strayintr(SB); BYTE $0x8F
	CALL _strayintr(SB); BYTE $0x90
	CALL _strayintr(SB); BYTE $0x91
	CALL _strayintr(SB); BYTE $0x92
	CALL _strayintr(SB); BYTE $0x93
	CALL _strayintr(SB); BYTE $0x94
	CALL _strayintr(SB); BYTE $0x95
	CALL _strayintr(SB); BYTE $0x96
	CALL _strayintr(SB); BYTE $0x97
	CALL _strayintr(SB); BYTE $0x98
	CALL _strayintr(SB); BYTE $0x99
	CALL _strayintr(SB); BYTE $0x9A
	CALL _strayintr(SB); BYTE $0x9B
	CALL _strayintr(SB); BYTE $0x9C
	CALL _strayintr(SB); BYTE $0x9D
	CALL _strayintr(SB); BYTE $0x9E
	CALL _strayintr(SB); BYTE $0x9F
	CALL _strayintr(SB); BYTE $0xA0
	CALL _strayintr(SB); BYTE $0xA1
	CALL _strayintr(SB); BYTE $0xA2
	CALL _strayintr(SB); BYTE $0xA3
	CALL _strayintr(SB); BYTE $0xA4
	CALL _strayintr(SB); BYTE $0xA5
	CALL _strayintr(SB); BYTE $0xA6
	CALL _strayintr(SB); BYTE $0xA7
	CALL _strayintr(SB); BYTE $0xA8
	CALL _strayintr(SB); BYTE $0xA9
	CALL _strayintr(SB); BYTE $0xAA
	CALL _strayintr(SB); BYTE $0xAB
	CALL _strayintr(SB); BYTE $0xAC
	CALL _strayintr(SB); BYTE $0xAD
	CALL _strayintr(SB); BYTE $0xAE
	CALL _strayintr(SB); BYTE $0xAF
	CALL _strayintr(SB); BYTE $0xB0
	CALL _strayintr(SB); BYTE $0xB1
	CALL _strayintr(SB); BYTE $0xB2
	CALL _strayintr(SB); BYTE $0xB3
	CALL _strayintr(SB); BYTE $0xB4
	CALL _strayintr(SB); BYTE $0xB5
	CALL _strayintr(SB); BYTE $0xB6
	CALL _strayintr(SB); BYTE $0xB7
	CALL _strayintr(SB); BYTE $0xB8
	CALL _strayintr(SB); BYTE $0xB9
	CALL _strayintr(SB); BYTE $0xBA
	CALL _strayintr(SB); BYTE $0xBB
	CALL _strayintr(SB); BYTE $0xBC
	CALL _strayintr(SB); BYTE $0xBD
	CALL _strayintr(SB); BYTE $0xBE
	CALL _strayintr(SB); BYTE $0xBF
	CALL _strayintr(SB); BYTE $0xC0
	CALL _strayintr(SB); BYTE $0xC1
	CALL _strayintr(SB); BYTE $0xC2
	CALL _strayintr(SB); BYTE $0xC3
	CALL _strayintr(SB); BYTE $0xC4
	CALL _strayintr(SB); BYTE $0xC5
	CALL _strayintr(SB); BYTE $0xC6
	CALL _strayintr(SB); BYTE $0xC7
	CALL _strayintr(SB); BYTE $0xC8
	CALL _strayintr(SB); BYTE $0xC9
	CALL _strayintr(SB); BYTE $0xCA
	CALL _strayintr(SB); BYTE $0xCB
	CALL _strayintr(SB); BYTE $0xCC
	CALL _strayintr(SB); BYTE $0xCD
	CALL _strayintr(SB); BYTE $0xCE
	CALL _strayintr(SB); BYTE $0xCF
	CALL _strayintr(SB); BYTE $0xD0
	CALL _strayintr(SB); BYTE $0xD1
	CALL _strayintr(SB); BYTE $0xD2
	CALL _strayintr(SB); BYTE $0xD3
	CALL _strayintr(SB); BYTE $0xD4
	CALL _strayintr(SB); BYTE $0xD5
	CALL _strayintr(SB); BYTE $0xD6
	CALL _strayintr(SB); BYTE $0xD7
	CALL _strayintr(SB); BYTE $0xD8
	CALL _strayintr(SB); BYTE $0xD9
	CALL _strayintr(SB); BYTE $0xDA
	CALL _strayintr(SB); BYTE $0xDB
	CALL _strayintr(SB); BYTE $0xDC
	CALL _strayintr(SB); BYTE $0xDD
	CALL _strayintr(SB); BYTE $0xDE
	CALL _strayintr(SB); BYTE $0xDF
	CALL _strayintr(SB); BYTE $0xE0
	CALL _strayintr(SB); BYTE $0xE1
	CALL _strayintr(SB); BYTE $0xE2
	CALL _strayintr(SB); BYTE $0xE3
	CALL _strayintr(SB); BYTE $0xE4
	CALL _strayintr(SB); BYTE $0xE5
	CALL _strayintr(SB); BYTE $0xE6
	CALL _strayintr(SB); BYTE $0xE7
	CALL _strayintr(SB); BYTE $0xE8
	CALL _strayintr(SB); BYTE $0xE9
	CALL _strayintr(SB); BYTE $0xEA
	CALL _strayintr(SB); BYTE $0xEB
	CALL _strayintr(SB); BYTE $0xEC
	CALL _strayintr(SB); BYTE $0xED
	CALL _strayintr(SB); BYTE $0xEE
	CALL _strayintr(SB); BYTE $0xEF
	CALL _strayintr(SB); BYTE $0xF0
	CALL _strayintr(SB); BYTE $0xF1
	CALL _strayintr(SB); BYTE $0xF2
	CALL _strayintr(SB); BYTE $0xF3
	CALL _strayintr(SB); BYTE $0xF4
	CALL _strayintr(SB); BYTE $0xF5
	CALL _strayintr(SB); BYTE $0xF6
	CALL _strayintr(SB); BYTE $0xF7
	CALL _strayintr(SB); BYTE $0xF8
	CALL _strayintr(SB); BYTE $0xF9
	CALL _strayintr(SB); BYTE $0xFA
	CALL _strayintr(SB); BYTE $0xFB
	CALL _strayintr(SB); BYTE $0xFC
	CALL _strayintr(SB); BYTE $0xFD
	CALL _strayintr(SB); BYTE $0xFE
	CALL _strayintr(SB); BYTE $0xFF
