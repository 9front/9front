#include "xendefs.h"
#include "mem.h"

#define ENTRY(X) TEXT X(SB), $0 

/*
 * XXX there's a race in here because we can get an upcall
 * betwen the spllo() (in xenupcall) and the rti.  This will make
 * handlers stack, which could lead to a blown stack.  Probably
 * not worth fixing (but possibly worth detecting and panicing).
 *
 * For fun get some popcorn and turn off the lights and read the
 * linux solution (search for scrit/ecrit).
 */
ENTRY(hypervisor_callback)
	SUBL	$8, SP		/* space for ecode and trap type */
	PUSHL	DS			/* save DS */
	PUSHL	$(KDSEL)
	POPL	DS			/* fix up DS */
	PUSHL	ES			/* save ES */
	PUSHL	$(KDSEL)
	POPL	ES			/* fix up ES */

	PUSHL	FS			/* save the rest of the Ureg struct */
	PUSHL	GS
	PUSHAL

	PUSHL	SP			/* Ureg* argument to trap */
	CALL xenupcall+0(SB)
	POPL	AX

	POPAL
	POPL	GS
	POPL	FS
	POPL	ES
	POPL	DS
	ADDL	$8, SP			/* pop error code and trap type */
	IRETL

/* Hypervisor uses this for application faults while it executes.*/
ENTRY(failsafe_callback)
	IRETL
	PUSHL	AX
	CALL 	install_safe_pf_handler(SB)
	MOVL	32(SP), BX
	MOVW	BX, DS
	MOVL	36(SP), BX
	MOVW	BX, ES
	MOVL	40(SP), BX
	MOVW	BX, FS
	MOVL	44(SP), BX
	MOVW	BX, GS
	CALL	install_normal_pf_handler(SB)
	POPL		AX
	ADDL	$16, SP
	IRETL

/* xen traps with varying argument counts */
TEXT xencall6(SB), $0
	MOVL	VDI+20(FP), DI
TEXT xencall5(SB), $0
	MOVL	VSI+16(FP), SI
TEXT xencall4(SB), $0
	MOVL	VDX+12(FP), DX
TEXT xencall3(SB), $0
	MOVL	VCX+8(FP), CX
TEXT xencall2(SB), $0
	MOVL	VBX+4(FP), BX
TEXT xencall1(SB), $0
	MOVL	op+0(FP), AX
	INT	$0x82
	RET
