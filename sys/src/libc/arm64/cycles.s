#define	SYSREG(op0,op1,Cn,Cm,op2)	SPR(((op0)<<19|(op1)<<16|(Cn)<<12|(Cm)<<8|(op2)<<5))
#define CNTVCT_EL0			SYSREG(3,3,14,0,2)

TEXT cycles(SB), 1, $-4
	MRS	CNTVCT_EL0, R1
	MOV	R1, (R0)
	RETURN
