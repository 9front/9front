#include "arm.s"
#include "mem.h"

TEXT _start(SB), 1, $-4
	MOVW	$setR12(SB), R12
	ADD	$(PHYSDRAM - KZERO), R12
	
	MOVW	$(PsrDirq | PsrDfiq | PsrMsvc), CPSR
	
	MOVW	$0x48020014, R1
uartloop:
	MOVW	(R1), R0
	AND.S	$(1<<6), R0
	B.EQ	uartloop
	
	EWAVE('\r')
	EWAVE('\n')
	
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BIC	$(CpCmmu), R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl

	EWAVE('P')
	
	MOVW	$KZERO, R1
	MOVW	$(PHYSDRAM|PTEDRAM), R2
	MOVW	$256, R3
	BL	_mapmbs(SB)
	MOVW	$PHYSDRAM, R1
	MOVW	$(PHYSDRAM|PTEDRAM), R2
	MOVW	$256, R3
	BL	_mapmbs(SB)
	MOVW	$0x48000000, R1
	MOVW	$(0x48000000| L1AP(Krw) | Section | PTEIO), R2
	MOVW	$1, R3
	BL	_mapmbs(SB)
	
	EWAVE('l')
	
	MOVW	$L1PT, R1
	MCR	CpSC, 0, R1, C(CpTTB), C(0), CpTTB0
	MCR	CpSC, 0, R1, C(CpTTB), C(0), CpTTB1

	EWAVE('a')

	MOVW	$Client, R1
	MCR	CpSC, 0, R1, C(CpDAC), C(0)
	MOVW	$0, R1
	MCR	CpSC, 0, R1, C(CpPID), C(0x0)

	EWAVE('n')

	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	ORR	$(CpCmmu|CpChv|CpCsw), R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl

	EWAVE(' ')
		
	BL	_jumphi(SB)
	
	EWAVE('9')
	
	MOVW	$setR12(SB), R12
	MOVW	$KTZERO, R13
	
	EWAVE(' ')
	
	BL	main(SB)
a:
	WFI
	B a
	BL	_div(SB) /* hack */

/* R1: virtual start, R2: physical start, R3: number of MB */
TEXT _mapmbs(SB), 1, $-4
	MOVW	$L1PT, R11
	ADD	R1>>18, R11, R1
mapmbsl:
	MOVW.P	R2, 4(R1)	
	ADD	$MiB, R2
	SUB.S	$1, R3
	B.NE	mapmbsl
	MOVW	R14, PC

TEXT _jumphi(SB), 1, $-4
	ADD	$(KZERO - PHYSDRAM), R14
	MOVW	R14, PC

TEXT coherence(SB), 1, $-4
	BARRIERS
	RET

TEXT splhi(SB), 1, $-4
	MOVW	CPSR, R0
	CPSID
	MOVW	$(MACHADDR + 4), R11
	MOVW	R14, (R11)
	RET

TEXT spllo(SB), 1, $-4
	MOVW	CPSR, R0
	CPSIE
	RET

TEXT splx(SB), 1, $-4
	MOVW	CPSR, R1
	MOVW	R0, CPSR
	MOVW	R1, R0
	RET

TEXT islo(SB), 1, $-4
	MOVW	CPSR, R0
	AND	$PsrDirq, R0
	EOR	$PsrDirq, R0
	RET

TEXT tas(SB), $-4
spintas:
	LDREX(0,1)
	CMP.S	$0, R1
	B.NE	tasnope
	MOVW	$1, R3
	STREX(0,3,2)
	CMP.S	$0, R2
	B.NE	spintas
tasnope:
	CLREX
	MOVW	R1, R0
	RET

TEXT cmpswap(SB), $-4
	MOVW	4(FP), R3
	MOVW	8(FP), R4
casspin:
	LDREX(0,1)
	CMP.S	R3, R1
	B.NE	casfail
	STREX(0,1,2)
	CMP.S	$0, R2
	B.NE	casspin
	MOVW	$1, R0
	RET
casfail:
	CLREX
	MOVW	$0, R0
	RET

TEXT ainc(SB), $-4
TEXT _xinc(SB), $-4
spinainc:
	LDREX(0,1)
	ADD	$1, R1
	STREX(0,1,2)
	CMP.S	$0, R2
	B.NE	spinainc
	MOVW	R1, R0
	RET

TEXT adec(SB), $-4
TEXT _xdec(SB), $-4
spinadec:
	LDREX(0,1)
	SUB	$1, R1
	STREX(0,1,2)
	CMP.S	$0, R2
	B.NE	spinadec
	MOVW	R1, R0
	RET

TEXT setlabel(SB), 1, $-4
	MOVW	R13, 0(R0)
	MOVW	R14, 4(R0)
	MOVW	$0, R0
	RET

TEXT gotolabel(SB), 1, $-4
	MOVW	0(R0), R13
	MOVW	4(R0), R14
	MOVW	$1, R0
	RET

TEXT idlehands(SB), 1, $-4
	BARRIERS
	WFI
	RET

TEXT flushtlb(SB), $-4
	BARRIERS
	MCR	CpSC, 0, R1, C(8), C(7), 0
	RET

#define TRAP(n,a)\
	SUB	$n, R14;\
	WORD	$0xf96d0513;\
	WORD	$0xf10e0093;\
	MOVW	R14, -8(R13);\
	MOVW	$a, R14;\
	MOVW	R14, -4(R13);\
	B _trap(SB)

TEXT _reset(SB), 1, $-4
	TRAP(4, 0)
TEXT _undefined(SB), 1, $-4
	TRAP(4, 1)
TEXT _prefabort(SB), 1, $-4
	TRAP(4, 3)
TEXT _dataabort(SB), 1, $-4
	TRAP(8, 4)
TEXT _wtftrap(SB), 1, $-4
	TRAP(4, 5)
TEXT _irq(SB), 1, $-4
	TRAP(4, 6)
TEXT _fiq(SB), 1, $-4
	TRAP(4, 7)

TEXT _trap(SB), 1, $-4
	SUB	$64, R13
	MOVM.IA	[R0-R12], (R13)
	MOVW	$setR12(SB), R12
	MOVW	64(R13), R0
	MOVW	68(R13), R1
	MOVW	R0, 68(R13)
	MOVW	R1, 64(R13)
	ADD	$72, R13, R0
	MOVW	R0, 52(R13)
	MOVW	R13, R0
	SUB	$8, R13
	BL trap(SB)
	MOVW	72(R13), R0
	AND	$PsrMask, R0
	CMP	$PsrMusr, R0
	B.EQ	_forkret
	ADD	$8, R13
	MOVW	68(R13), R0
	MOVW	R0, 60(R13)
	MOVW	64(R13), R0
	MOVW	R0, SPSR
	MOVW	R13, R0
	ADD	$72, R13
	WORD	$0xE8D0FFFF

TEXT _syscall(SB), 1, $-4
	WORD	$0xf96d0513
	WORD	$0xf10e0093
	SUB	$64, R13
	MOVM.IA.S	[R0-R14], (R13)
	MOVW	$setR12(SB), R12
	MOVW	64(R13), R0
	MOVW	68(R13), R1
	MOVW	R0, 68(R13)
	MOVW	R1, 64(R13)
	MOVW	R13, R0
	SUB	$8, R13
	BL	syscall(SB)

TEXT forkret(SB), 1, $-4
_forkret:
	ADD	$8, R13
	MOVW	R13, R0
	ADD	$72, R13

TEXT touser(SB), 1, $-4
	ADD	$52, R0
	MOVM.IA.S	(R0), [R13-R14]
	SUB	$52, R0
	MOVW	68(R0), R1
	MOVW	R1, 52(R0)
	MOVW	64(R0), R1
	MOVW	R1, SPSR
	WORD	$0xE8D09FFF

TEXT fillureguser(SB), $-4
	ADD	$52, R0
	MOVM.IA.S	[R13-R14], (R0)
	RET


TEXT dumpstack(SB), 0, $8
	MOVW	R14, 8(R13)
	ADD	$12, R13, R0
	BL	_dumpstack(SB)
	RET

TEXT getdfsr(SB), 0, $-4
	MRC	CpSC, 0, R0, C(5), C(0), 0
	RET

TEXT getifsr(SB), 0, $-4
	MRC	CpSC, 0, R0, C(5), C(0), 1
	RET

TEXT getdfar(SB), 0, $-4
	MRC	CpSC, 0, R0, C(6), C(0), 0
	RET

TEXT getifar(SB), 0, $-4
	MRC	CpSC, 0, R0, C(6), C(0), 2
	RET

TEXT getr13(SB), 0, $-4
	MOVW	R13, R0
	RET
