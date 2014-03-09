/*
 * 386 definition
 */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mach.h>

#pragma pack on
#include "/386/include/ureg.h"
#pragma pack off

#define	REGOFF(x)	(uintptr)(&((struct Ureg *) 0)->x)

#define PC		REGOFF(pc)
#define SP		REGOFF(sp)
#define	AX		REGOFF(ax)

#define	REGSIZE		sizeof(struct Ureg)
#define FP_CTLS(x)	(REGSIZE+2*(x))
#define FP_CTL(x)	(REGSIZE+4*(x))
#define FP_REG(x)	(FP_CTL(8)+16*(x))
#define XM_REG(x)	(FP_CTL(8)+8*16+16*(x))
#define	FPREGSIZE	512

Reglist i386reglist[] = {
	{"DI",		REGOFF(di),	RINT, 'X'},
	{"SI",		REGOFF(si),	RINT, 'X'},
	{"BP",		REGOFF(bp),	RINT, 'X'},
	{"BX",		REGOFF(bx),	RINT, 'X'},
	{"DX",		REGOFF(dx),	RINT, 'X'},
	{"CX",		REGOFF(cx),	RINT, 'X'},
	{"AX",		REGOFF(ax),	RINT, 'X'},
	{"GS",		REGOFF(gs),	RINT, 'X'},
	{"FS",		REGOFF(fs),	RINT, 'X'},
	{"ES",		REGOFF(es),	RINT, 'X'},
	{"DS",		REGOFF(ds),	RINT, 'X'},
	{"TRAP",	REGOFF(trap), 	RINT, 'X'},
	{"ECODE",	REGOFF(ecode),	RINT, 'X'},
	{"PC",		PC,		RINT, 'X'},
	{"CS",		REGOFF(cs),	RINT, 'X'},
	{"EFLAGS",	REGOFF(flags),	RINT, 'X'},
	{"SP",		SP,		RINT, 'X'},
	{"SS",		REGOFF(ss),	RINT, 'X'},

	{"FCW",		FP_CTLS(0),	RFLT, 'x'},
	{"FSW",		FP_CTLS(1),	RFLT, 'x'},
	{"FTW",		FP_CTLS(2),	RFLT, 'b'},
	{"FOP",		FP_CTLS(3),	RFLT, 'x'},
	{"FIP",		FP_CTL(2),	RFLT, 'X'},
	{"FCS",		FP_CTLS(6),	RFLT, 'x'},
	{"FDP",		FP_CTL(4),	RFLT, 'X'},
	{"FDS",		FP_CTLS(10),	RFLT, 'x'},
	{"MXCSR",	FP_CTL(6),	RFLT, 'X'},
	{"MXCSRMASK",	FP_CTL(7),	RFLT, 'X'},

	{"M0",		FP_REG(0),	RFLT, 'F'},	/* assumes double */
	{"M1",		FP_REG(1),	RFLT, 'F'},
	{"M2",		FP_REG(2),	RFLT, 'F'},
	{"M3",		FP_REG(3),	RFLT, 'F'},
	{"M4",		FP_REG(4),	RFLT, 'F'},
	{"M5",		FP_REG(5),	RFLT, 'F'},
	{"M6",		FP_REG(6),	RFLT, 'F'},
	{"M7",		FP_REG(7),	RFLT, 'F'},

	{"X0",		XM_REG(0),	RFLT, 'F'},	/* assumes double */
	{"X1",		XM_REG(1),	RFLT, 'F'},
	{"X2",		XM_REG(2),	RFLT, 'F'},
	{"X3",		XM_REG(3),	RFLT, 'F'},
	{"X4",		XM_REG(4),	RFLT, 'F'},
	{"X5",		XM_REG(5),	RFLT, 'F'},
	{"X6",		XM_REG(6),	RFLT, 'F'},
	{"X7",		XM_REG(7),	RFLT, 'F'},

	{"F0",		FP_REG(0),	RFLT, '3'},
	{"F1",		FP_REG(1),	RFLT, '3'},
	{"F2",		FP_REG(2),	RFLT, '3'},
	{"F3",		FP_REG(3),	RFLT, '3'},
	{"F4",		FP_REG(4),	RFLT, '3'},
	{"F5",		FP_REG(5),	RFLT, '3'},
	{"F6",		FP_REG(6),	RFLT, '3'},
	{"F7",		FP_REG(7),	RFLT, '3'},

	{  0 }
};

Mach mi386 =
{
	"386",
	MI386,		/* machine type */
	i386reglist,	/* register list */
	REGSIZE,	/* size of registers in bytes */
	FPREGSIZE,	/* size of fp registers in bytes */
	"PC",		/* name of PC */
	"SP",		/* name of SP */
	0,		/* link register */
	"setSB",	/* static base register name (bogus anyways) */
	0,		/* static base register value */
	0x1000,		/* page size */
	0xF0100000ULL,	/* kernel base */
	0xF0000000ULL,	/* kernel text mask */
	0x7FFFFFFFULL,	/* user stack top */
	1,		/* quantization of pc */
	4,		/* szaddr */
	4,		/* szreg */
	4,		/* szfloat */
	8,		/* szdouble */
};
