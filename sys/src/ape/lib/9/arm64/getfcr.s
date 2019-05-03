#define	SYSARG5(op0,op1,Cn,Cm,op2)	((op0)<<19|(op1)<<16|(Cn)<<12|(Cm)<<8|(op2)<<5)

#define	FPCR		SPR(SYSARG5(3,3,4,4,0))
#define	FPSR		SPR(SYSARG5(3,3,4,4,1))

TEXT	setfcr(SB), $0
	MSR	R0, FPCR
	RETURN

TEXT	getfcr(SB), $0
	MRS	FPCR, R0
	RETURN

TEXT	getfsr(SB), $0
	MRS	FPSR, R0
	RETURN

TEXT	setfsr(SB), $0
	MSR	R0, FPSR
	RETURN

