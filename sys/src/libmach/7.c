/*
 * arm definition
 */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mach.h>

#include "/arm64/include/ureg.h"

#define	REGSIZE		sizeof(struct Ureg)
#define	FPREGSIZE	520

#define	REGOFF(x)	(uintptr)(&((struct Ureg *) 0)->x)
#define	FP_REG(x)	(REGSIZE+16*(x))
#define	FP_CTL(x)	(FP_REG(32)+4*(x))

#define SP		REGOFF(sp)
#define PC		REGOFF(pc)

Reglist arm64reglist[] =
{
	{"TYPE",	REGOFF(type),		RINT|RRDONLY, 'Y'},
	{"PSR",		REGOFF(psr),		RINT|RRDONLY, 'Y'},
	{"PC",		PC,			RINT, 'Y'},
	{"SP",		SP,			RINT, 'Y'},
	{"R30",		REGOFF(r30),		RINT, 'Y'},
	{"R29",		REGOFF(r29),		RINT, 'Y'},
	{"R28",		REGOFF(r28),		RINT, 'Y'},
	{"R27",		REGOFF(r27),		RINT, 'Y'},
	{"R26",		REGOFF(r26),		RINT, 'Y'},
	{"R25",		REGOFF(r25),		RINT, 'Y'},
	{"R24",		REGOFF(r24),		RINT, 'Y'},
	{"R23",		REGOFF(r23),		RINT, 'Y'},
	{"R22",		REGOFF(r22),		RINT, 'Y'},
	{"R21",		REGOFF(r21),		RINT, 'Y'},
	{"R20",		REGOFF(r20),		RINT, 'Y'},
	{"R19",		REGOFF(r19),		RINT, 'Y'},
	{"R18",		REGOFF(r18),		RINT, 'Y'},
	{"R17",		REGOFF(r17),		RINT, 'Y'},
	{"R16",		REGOFF(r16),		RINT, 'Y'},
	{"R15",		REGOFF(r15),		RINT, 'Y'},
	{"R14",		REGOFF(r14),		RINT, 'Y'},
	{"R13",		REGOFF(r13),		RINT, 'Y'},
	{"R12",		REGOFF(r12),		RINT, 'Y'},
	{"R11",		REGOFF(r11),		RINT, 'Y'},
	{"R10",		REGOFF(r10),		RINT, 'Y'},
	{"R9",		REGOFF(r9),		RINT, 'Y'},
	{"R8",		REGOFF(r8),		RINT, 'Y'},
	{"R7",		REGOFF(r7),		RINT, 'Y'},
	{"R6",		REGOFF(r6),		RINT, 'Y'},
	{"R5",		REGOFF(r5),		RINT, 'Y'},
	{"R4",		REGOFF(r4),		RINT, 'Y'},
	{"R3",		REGOFF(r3),		RINT, 'Y'},
	{"R2",		REGOFF(r2),		RINT, 'Y'},
	{"R1",		REGOFF(r1),		RINT, 'Y'},
	{"R0",		REGOFF(r0),		RINT, 'Y'},

	{"FPSR",	FP_CTL(1),		RINT, 'X'},
	{"FPCR",	FP_CTL(0),		RINT, 'X'},

	{"F31",		FP_REG(31),		RFLT, 'F'}, /* double */
	{"F30",		FP_REG(30),		RFLT, 'F'},
	{"F29",		FP_REG(29),		RFLT, 'F'},
	{"F28",		FP_REG(28),		RFLT, 'F'},
	{"F27",		FP_REG(27),		RFLT, 'F'},
	{"F26",		FP_REG(26),		RFLT, 'F'},
	{"F25",		FP_REG(25),		RFLT, 'F'},
	{"F24",		FP_REG(24),		RFLT, 'F'},
	{"F23",		FP_REG(23),		RFLT, 'F'},
	{"F22",		FP_REG(22),		RFLT, 'F'},
	{"F21",		FP_REG(21),		RFLT, 'F'},
	{"F20",		FP_REG(20),		RFLT, 'F'},
	{"F19",		FP_REG(19),		RFLT, 'F'},
	{"F18",		FP_REG(18),		RFLT, 'F'},
	{"F17",		FP_REG(17),		RFLT, 'F'},
	{"F16",		FP_REG(16),		RFLT, 'F'},
	{"F15",		FP_REG(15),		RFLT, 'F'},
	{"F14",		FP_REG(14),		RFLT, 'F'},
	{"F13",		FP_REG(13),		RFLT, 'F'},
	{"F12",		FP_REG(12),		RFLT, 'F'},
	{"F11",		FP_REG(11),		RFLT, 'F'},
	{"F10",		FP_REG(10),		RFLT, 'F'},
	{"F9",		FP_REG(9),		RFLT, 'F'},
	{"F8",		FP_REG(8),		RFLT, 'F'},
	{"F7",		FP_REG(7),		RFLT, 'F'},
	{"F6",		FP_REG(6),		RFLT, 'F'},
	{"F5",		FP_REG(5),		RFLT, 'F'},
	{"F4",		FP_REG(4),		RFLT, 'F'},
	{"F3",		FP_REG(3),		RFLT, 'F'},
	{"F2",		FP_REG(2),		RFLT, 'F'},
	{"F1",		FP_REG(1),		RFLT, 'F'},
	{"F0",		FP_REG(0),		RFLT, 'F'},

	{"f31",		FP_REG(31),		RFLT, 'f'}, /* double */
	{"f30",		FP_REG(30),		RFLT, 'f'},
	{"f29",		FP_REG(29),		RFLT, 'f'},
	{"f28",		FP_REG(28),		RFLT, 'f'},
	{"f27",		FP_REG(27),		RFLT, 'f'},
	{"f26",		FP_REG(26),		RFLT, 'f'},
	{"f25",		FP_REG(25),		RFLT, 'f'},
	{"f24",		FP_REG(24),		RFLT, 'f'},
	{"f23",		FP_REG(23),		RFLT, 'f'},
	{"f22",		FP_REG(22),		RFLT, 'f'},
	{"f21",		FP_REG(21),		RFLT, 'f'},
	{"f20",		FP_REG(20),		RFLT, 'f'},
	{"f19",		FP_REG(19),		RFLT, 'f'},
	{"f18",		FP_REG(18),		RFLT, 'f'},
	{"f17",		FP_REG(17),		RFLT, 'f'},
	{"f16",		FP_REG(16),		RFLT, 'f'},
	{"f15",		FP_REG(15),		RFLT, 'f'},
	{"f14",		FP_REG(14),		RFLT, 'f'},
	{"f13",		FP_REG(13),		RFLT, 'f'},
	{"f12",		FP_REG(12),		RFLT, 'f'},
	{"f11",		FP_REG(11),		RFLT, 'f'},
	{"f10",		FP_REG(10),		RFLT, 'f'},
	{"f9",		FP_REG(9),		RFLT, 'f'},
	{"f8",		FP_REG(8),		RFLT, 'f'},
	{"f7",		FP_REG(7),		RFLT, 'f'},
	{"f6",		FP_REG(6),		RFLT, 'f'},
	{"f5",		FP_REG(5),		RFLT, 'f'},
	{"f4",		FP_REG(4),		RFLT, 'f'},
	{"f3",		FP_REG(3),		RFLT, 'f'},
	{"f2",		FP_REG(2),		RFLT, 'f'},
	{"f1",		FP_REG(1),		RFLT, 'f'},
	{"f0",		FP_REG(0),		RFLT, 'f'},
	{  0 }
};

	/* the machine description */
Mach marm64 =
{
	"arm64",
	MARM64,		/* machine type */
	arm64reglist,	/* register set */
	REGSIZE,	/* register set size */
	FPREGSIZE,		/* fp register set size */
	"PC",		/* name of PC */
	"SP",		/* name of SP */
	"R30",		/* name of link register */
	"setSB",	/* static base register name */
	0,		/* static base register value */
	0x10000,	/* page size (for segment alignment) */
	0xFFFFFFFF80000000ULL,	/* kernel base */
	0xFFFF800000000000ULL,	/* kernel text mask */
	0x00007FFFFFFF0000ULL,	/* user stack top */
	4,		/* quantization of pc */
	8,		/* szaddr */
	8,		/* szreg */
	4,		/* szfloat */
	8,		/* szdouble */
};
