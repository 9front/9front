#include "mem.h"
#include "io.h"

TEXT vectors(SB), $-4
	MOVW $_start-KZERO(SB), R15
	MOVW $_vexc(SB), R15
	MOVW $_vsvc(SB), R15
	MOVW $_viabt(SB), R15
	MOVW $_vexc(SB), R15
	MOVW $vectors(SB), R15
	MOVW $_vexc(SB), R15
	MOVW $_vexc(SB), R15

TEXT _viabt(SB), $-4
	CPS(CPSID)
	CLREX
	DSB
	MOVW R14, 8(R13)
	MOVW SPSR, R14
	MOVW R14, 4(R13)
	MOVW CPSR, R14
	AND $0x1e, R14
	B _exc
	

TEXT _vexc(SB), $-4
	CPS(CPSID)
	CLREX
	DSB
	MOVW R14, 8(R13)
	MOVW SPSR, R14
	MOVW R14, 4(R13)
	MOVW CPSR, R14
	AND $0x1f, R14
_exc:
	MOVW R14, 0(R13)
	CPS(CPSMODE | PsrMsvc)

	SUB $(18*4), R13
	MOVM.IA [R0-R14], (R13)

	/* get Mach* from TPIDRPRW */
	MRC 15, 0, R(Rmach), C(13), C(0), 4
	MOVW 8(R(Rmach)), R(Rup)
	MOVW $setR12(SB), R12
	
	ADD $12, R(Rmach), R0
	MOVM.IA (R0), [R1-R3]
	ADD $(15*4), R13, R0
	MOVM.IA [R1-R3], (R0)
	
	AND.S $0xf, R2
	ADD.NE $(18*4), R13, R0
	MOVW.NE R0, (13*4)(R13)
	ADD.EQ $(13*4), R13, R0
	MOVM.IA.S.EQ [R13-R14], (R0)
	
	MOVW R13, R0
	SUB $8, R13
	BL trap(SB)
	ADD $8, R13
	
	MOVW (16*4)(R13), R0
	MOVW R0, SPSR
	AND.S $0xf, R0
	BEQ _uret
	MOVW R(Rmach), (Rmach*4)(R13)
	MOVM.IA (R13), [R0-R14]
	DSB
	MOVM.DB.S (R13), [R15]

TEXT _vsvc(SB), $-4
	CLREX
	DSB
	MOVW.W R14, -4(R13)
	MOVW SPSR, R14
	MOVW.W R14, -4(R13)
	MOVW $PsrMsvc, R14
	MOVW.W R14, -4(R13)
	MOVM.DB.S [R0-R14], (R13)
	SUB $(15*4), R13
	
	/* get Mach* from TPIDRPRW */
	MRC 15, 0, R(Rmach), C(13), C(0), 4
	MOVW 8(R(Rmach)), R(Rup)
	MOVW $setR12(SB), R12
	
	MOVW R13, R0
	SUB $8, R13
	BL syscall(SB)
	ADD $8, R13

	MOVW (16*4)(R13), R0
	MOVW R0, SPSR
_uret:
	MOVM.IA.S (R13), [R0-R14]
	ADD $(17*4), R13
	DSB
	ISB
	MOVM.IA.S.W (R13), [R15]
