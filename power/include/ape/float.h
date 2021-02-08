#ifndef __FLOAT
#define __FLOAT
/* IEEE, default rounding */

#define FLT_ROUNDS	1
#define FLT_RADIX	2

#define FLT_DIG		6
#define FLT_EPSILON	1.19209290e-07
#define FLT_MANT_DIG	24
#define FLT_MAX		3.40282347e+38
#define FLT_MAX_10_EXP	38
#define FLT_MAX_EXP	128
#define FLT_MIN		1.17549435e-38
#define FLT_MIN_10_EXP	-37
#define FLT_MIN_EXP	-125

#define DBL_DIG		15
#define DBL_EPSILON	2.2204460492503131e-16
#define DBL_MANT_DIG	53
#define DBL_MAX		1.797693134862315708145e+308
#define DBL_MAX_10_EXP	308
#define DBL_MAX_EXP	1024
#define DBL_MIN		2.225073858507201383090233e-308
#define DBL_MIN_10_EXP	-307
#define DBL_MIN_EXP	-1021
#define LDBL_MANT_DIG	DBL_MANT_DIG
#define LDBL_EPSILON	DBL_EPSILON
#define LDBL_DIG	DBL_DIG
#define LDBL_MIN_EXP	DBL_MIN_EXP
#define LDBL_MIN	DBL_MIN
#define LDBL_MIN_10_EXP	DBL_MIN_10_EXP
#define LDBL_MAX_EXP	DBL_MAX_EXP
#define LDBL_MAX	DBL_MAX
#define LDBL_MAX_10_EXP	DBL_MAX_10_EXP

typedef 	union FPdbleword FPdbleword;
union FPdbleword
{
	double	x;
	struct {	/* big endian */
		long hi;
		long lo;
	};
};

#ifdef _RESEARCH_SOURCE
/* define stuff needed for floating conversion */
#define IEEE_MC68k	1
#define Sudden_Underflow 1
#endif
#ifdef _PLAN9_SOURCE
/* FPSCR */
#define	FPSFX	(1<<31)	/* exception summary (sticky) */
#define	FPSEX	(1<<30)	/* enabled exception summary */
#define	FPSVX	(1<<29)	/* invalid operation exception summary */
#define	FPSOX	(1<<28)	/* overflow exception OX (sticky) */
#define	FPSUX	(1<<27)	/* underflow exception UX (sticky) */
#define	FPSZX	(1<<26)	/* zero divide exception ZX (sticky) */
#define	FPSXX	(1<<25)	/* inexact exception XX (sticky) */
#define	FPSVXSNAN (1<<24)	/* invalid operation exception for SNaN (sticky) */
#define	FPSVXISI (1<<23)	/* invalid operation exception for â-â (sticky) */
#define	FPSVXIDI (1<<22)	/* invalid operation exception for â/â (sticky) */
#define	FPSVXZDZ (1<<21)	/* invalid operation exception for 0/0 (sticky) */
#define	FPSVXIMZ (1<<20)	/* invalid operation exception for â*0 (sticky) */
#define	FPSVXVC	(1<<19)	/* invalid operation exception for invalid compare (sticky) */
#define	FPSFR	(1<<18)	/* fraction rounded */
#define	FPSFI	(1<<17)	/* fraction inexact */
#define	FPSFPRF	(1<<16)	/* floating point result class */
#define	FPSFPCC	(0xF<<12)	/* <, >, =, unordered */
#define	FPVXCVI	(1<<8)	/* enable exception for invalid integer convert (sticky) */
#define	FPVE	(1<<7)	/* invalid operation exception enable */
#define	FPOVFL	(1<<6)	/* enable overflow exceptions */
#define	FPUNFL	(1<<5)	/* enable underflow */
#define	FPZDIV	(1<<4)	/* enable zero divide */
#define	FPINEX	(1<<3)	/* enable inexact exceptions */
#define	FPRMASK	(3<<0)	/* rounding mode */
#define	FPRNR	(0<<0)
#define	FPRZ	(1<<0)
#define	FPRPINF	(2<<0)
#define	FPRNINF	(3<<0)
#define	FPPEXT	0
#define	FPPSGL	0
#define	FPPDBL	0
#define	FPPMASK	0
#define	FPINVAL	FPVE

#define	FPAOVFL	FPSOX
#define	FPAINEX	FPSXX
#define	FPAUNFL	FPSUX
#define	FPAZDIV	FPSZX
#define	FPAINVAL	FPSVX
#endif
#endif /* __FLOAT */
