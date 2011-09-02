#include "arm.h"

/* arm v7 arch defines these */
#define WFI	WORD	$0xe320f003	/* wait for interrupt */
#define DMB	WORD	$0xf57ff05f	/* data mem. barrier; last f = SY */
#define DSB	WORD	$0xf57ff04f	/* data synch. barrier; last f = SY */
#define ISB	WORD	$0xf57ff06f	/* instr. sync. barrier; last f = SY */
#define NOOP	WORD	$0xe320f000
#define CLZ(s, d) WORD	$(0xe16f0f10 | (d) << 12 | (s))	/* count leading 0s */
#define CPSIE	WORD	$0xf1080080	/* intr enable: zeroes I bit */
#define CPSID	WORD	$0xf10c0080	/* intr disable: sets I bit */

#define EWAVE(n)\
	MOVW	$0x48020000, R0; \
	MOVW	$n, R1; \
	MOVW	R1, (R0);

#define WAVE(n)\
	MOVW	$0xE0000000, R0; \
	MOVW	$n, R1; \
	MOVW	R1, (R0);

#define	LDREX(a,r)	WORD	$(0xe<<28|0x01900f9f | (a)<<16 | (r)<<12)
#define	STREX(a,v,r)	WORD	$(0xe<<28|0x01800f90 | (a)<<16 | (r)<<12 | (v)<<0)
#define CLREX		WORD	$0xf57ff01f

#define BARRIERS\
	MOVW	$0, R11; \
	MCR	CpSC, 0, R11, C(CpCACHE), C(CpCACHEinvi), CpCACHEflushbtc; \
	DSB; \
	ISB;
