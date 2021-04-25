/*
 * Common startup for armv6 and armv7
 * The rest of l.s has been moved to armv[67].s
 */

#include "arm.s"

/*
 * on bcm2836, only cpu0 starts here
 * other cpus enter at cpureset in armv7.s
 */
TEXT _start(SB), 1, $-4
	/* save R2 in extern register R10 (Mach *m) */
	MOVW	R2, R10

	/*
	 * load physical base for SB addressing while mmu is off
	 * keep a handy zero in R0 until first function call
	 */
	MOVW	$setR12(SB), R12
	SUB	$KZERO, R12
	ADD	$PHYSDRAM, R12
	MOVW	$0, R0

	/*
	 * start stack at top of mach (physical addr)
	 */
	MOVW	$PADDR(MACHADDR+MACHSIZE-4), R13

	/*
	 * do arch-dependent startup (no return)
	 */
	BL	,armstart(SB)
	B	,0(PC)

	RET
