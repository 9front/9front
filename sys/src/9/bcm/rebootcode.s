/*
 * armv6/armv7 reboot code
 */
#include "arm.s"

#define WFI	WORD	$0xe320f003	/* wait for interrupt */
#define WFE	WORD	$0xe320f002	/* wait for event */

TEXT	main(SB), 1, $-4
	MOVW	$setR12(SB), R12

	MOVW	R0, entry+0(FP)
	CMP	$0, R0
	BEQ	shutdown

	MOVW	entry+0(FP), R8
	MOVW	code+4(FP), R9
	MOVW	size+8(FP), R6	

	/* round to words */
	BIC	$3, R8
	BIC	$3, R9
	ADD	$3, R6
	BIC	$3, R6

memloop:
	MOVM.IA.W	(R9), [R1]
	MOVM.IA.W	[R1], (R8)
	SUB.S	$4, R6
	BNE	memloop

shutdown:
	/* clean dcache using appropriate code for armv6 or armv7 */
	MRC	CpSC, 0, R1, C(CpID), C(CpIDfeat), 7	/* Memory Model Feature Register 3 */
	TST	$0xF, R1	/* hierarchical cache maintenance? */
	BNE	l2wb
	DSB
	MOVW	$0, R0
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEwb), CpCACHEall
	B	l2wbx
l2wb:
	BL	cachedwb(SB)
	BL	l2cacheuwb(SB)
l2wbx:
	/* load entry before turning off mmu */
	MOVW	entry+0(FP), R8

	/* disable caches */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BIC	$(CpCdcache|CpCicache|CpCpredict), R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BARRIERS

	/* invalidate icache */
	MOVW	$0, R0
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEinvi), CpCACHEall
	BARRIERS

	/* turn off mmu */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BIC	$CpCmmu, R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BARRIERS

	/* turn SMP off */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpAuxctl
	BIC	$CpACsmp, R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpAuxctl
	ISB
	DSB

	/* have entry? */
	CMP	$0, R8
	BNE	bootcpu

	/* other cpus wait for inter processor interrupt */
	CPUID(R2)
dowfi:
	WFE			/* wait for event signal */
	MOVW	$0x400000CC, R1	/* inter-core .startcpu mailboxes */
	ADD	R2<<4, R1	/* mailbox for this core */
	MOVW	0(R1), R8	/* content of mailbox */
	CMP	$0, R8		
	BEQ	dowfi		/* if zero, wait again */

bootcpu:
	BIC	$KSEGM, R8	/* entry to physical */
	ORR	$PHYSDRAM, R8
	BL	(R8)
	B	dowfi

#define ICACHELINESZ	32
#include "cache.v7.s"
