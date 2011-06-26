#include "x16.h"
#include "mem.h"

#define RELOC 0x7c00

TEXT _magic(SB), $0
	BYTE $0xEB; BYTE $0x58;		/* jmp .+ 0x58  (_start0x5A) */
	BYTE $0x90			/* nop */
TEXT _version(SB), $0
	BYTE $0x00; BYTE $0x00; BYTE $0x00; BYTE $0x00;
	BYTE $0x00; BYTE $0x00; BYTE $0x00; BYTE $0x00
TEXT _sectsize(SB), $0
	BYTE $0x00; BYTE $0x00
TEXT _clustsize(SB), $0
	BYTE $0x00
TEXT _nresrv(SB), $0
	BYTE $0x00; BYTE $0x00
TEXT _nfats(SB), $0
	BYTE $0x00
TEXT _rootsize(SB), $0
	BYTE $0x00; BYTE $0x00
TEXT _volsize(SB), $0
	BYTE $0x00; BYTE $0x00
TEXT _mediadesc(SB), $0
	BYTE $0x00
TEXT _fatsize(SB), $0
	BYTE $0x00; BYTE $0x00
TEXT _trksize(SB), $0
	BYTE $0x00; BYTE $0x00
TEXT _nheads(SB), $0
	BYTE $0x00; BYTE $0x00
TEXT _nhiddenlo(SB), $0
	BYTE $0x00; BYTE $0x00
TEXT _nhiddenhi(SB), $0
	BYTE $0x00; BYTE $0x00;
TEXT _bigvolsize(SB), $0
	BYTE $0x00; BYTE $0x00; BYTE $0x00; BYTE $0x00;
/* FAT32 structure, starting @0x24 */
TEXT _fatsz32lo(SB), $0
	BYTE $0x00; BYTE $0x00
TEXT _fatsz32hi(SB), $0
	BYTE $0x00; BYTE $0x00
TEXT _extflags(SB), $0
	BYTE $0x00; BYTE $0x00
TEXT _fsver(SB), $0
	BYTE $0x00; BYTE $0x00
TEXT _rootclust(SB), $0
	BYTE $0x00; BYTE $0x00; BYTE $0x00; BYTE $0x00
TEXT _fsinfo(SB), $0
	BYTE $0x00; BYTE $0x00
TEXT _bkboot(SB), $0
	BYTE $0x00; BYTE $0x00
TEXT _reserved0(SB), $0
	BYTE $0x00; BYTE $0x00; BYTE $0x00; BYTE $0x00;
	BYTE $0x00; BYTE $0x00; BYTE $0x00; BYTE $0x00;
	BYTE $0x00; BYTE $0x00; BYTE $0x00; BYTE $0x00
TEXT _driveno(SB), $0
	BYTE $0x00
TEXT _reserved1(SB), $0
	BYTE $0x00
TEXT _bootsig(SB), $0
	BYTE $0x00
TEXT _volid(SB), $0
	BYTE $0x00; BYTE $0x00; BYTE $0x00; BYTE $0x00;
TEXT _label(SB), $0
	BYTE $0x00; BYTE $0x00; BYTE $0x00; BYTE $0x00;
	BYTE $0x00; BYTE $0x00; BYTE $0x00; BYTE $0x00
	BYTE $0x00; BYTE $0x00; BYTE $0x00
TEXT _type(SB), $0
	BYTE $0x00; BYTE $0x00; BYTE $0x00; BYTE $0x00;
	BYTE $0x00; BYTE $0x00; BYTE $0x00; BYTE $0x00

_start0x5A:
	CLI
	CLR(rAX)
	MTSR(rAX, rSS)			/* 0000 -> rSS */
	MTSR(rAX, rDS)			/* 0000 -> rDS, source segment */
	MTSR(rAX, rES)

	LWI(0x100, rCX)
	LWI(RELOC, rSI)
	MW(rSI, rSP)
	LWI(_magic(SB), rDI)
	CLD
	REP; MOVSL			/* MOV DS:[(E)SI] -> ES:[(E)DI] */

	MW(rSP, rBP)

	PUSHR(rCX)
	PUSHI(start16(SB))
	BYTE $0xCB			/* FAR RET */

TEXT halt(SB), $0
_halt:
	JMP _halt

TEXT start16(SB), $0
	STI

	LWI(hello(SB), rSI)
	CALL16(print16(SB))

	PUSHR(rDX)	/* drive */

	CLR(rDX)
	LW(_fatsize(SB), rAX)
	CLR(rCX)
	LB(_nfats(SB), rCL)
	MUL(rCX)
	OR(rAX, rAX)
	JNE _fatszok	/* zero? it's FAT32 */

	LW(_fatsz32hi(SB), rBX)
	IMUL(rCX, rBX)
	LW(_fatsz32lo(SB), rAX)
	MUL(rCX)
	ADD(rBX, rDX)

_fatszok:
	LW(_nhiddenlo(SB), rCX)
	ADD(rCX, rAX)
	LW(_nhiddenhi(SB), rCX)
	ADC(rCX, rDX)

	CLR(rBX)
	LW(_nresrv(SB), rCX)
	ADD(rCX, rAX)
	ADC(rDX, rBX)

	SW(rAX, _volid(SB))	/* save for later use */
	SW(rBX, _volid+2(SB))

	POPR(rDX)	/* drive */

	PUSHR(rBP)
	LW(_sectsize(SB), rCX)
	SUB(rCX, rSP)
	MW(rSP, rBP)
	MW(rSP, rSI)

_nextsect:
	PUSHR(rAX)
	CALL16(readsect16(SB))
	OR(rAX, rAX)
	JNE _halt

	LW(_sectsize(SB), rCX)
	SHRI(5, rCX)

_nextdir:
	PUSHR(rCX)
	PUSHR(rSI)			/* save for later if it matches */
	LWI(bootname(SB), rDI)
	LW(bootnamelen(SB), rCX)
	CLD
	REP
	CMPSB
	POPR(rSI)
	POPR(rCX)
	JEQ _found
	ADDI(0x20, rSI)	
	LOOP _nextdir
	POPR(rAX)
	ADDI(1, rAX)
	ADC(rCX, rBX)
	JMP _nextsect

_found:
	PUSHR(rDX)			/* drive */

	CLR(rBX)

	LW(_rootsize(SB), rAX)		/* calculate and save Xrootsz */
	LWI(0x20, rCX)
	MUL(rCX)
	LW(_sectsize(SB), rCX)
	PUSHR(rCX)
	DEC(rCX)
	ADD(rCX, rAX)
	ADC(rBX, rDX)
	POPR(rCX)			/* _sectsize(SB) */
	DIV(rCX)
	PUSHR(rAX)			/* Xrootsz */

	LXW(0x1a, xSI, rAX)		/* starting sector address */
	DEC(rAX)			/* that's just the way it is */
	DEC(rAX)
	LB(_clustsize(SB), rCL)
	CLRB(rCH)
	MUL(rCX)
	LW(_volid(SB), rCX)		/* Xrootlo */
	ADD(rCX, rAX)
	LW(_volid+2(SB), rCX)		/* Xroothi */
	ADC(rCX, rDX)
	POPR(rCX)			/* Xrootsz */
	ADD(rCX, rAX)
	ADC(rBX, rDX)

	PUSHR(rAX)			/* calculate how many sectors to read */
	PUSHR(rDX)
	LXW(0x1c, xSI, rAX)
	LXW(0x1e, xSI, rDX)
	LW(_sectsize(SB), rCX)
	PUSHR(rCX)
	DEC(rCX)
	ADD(rCX, rAX)
	ADC(rBX, rDX)
	POPR(rCX)			/* _sectsize(SB) */
	DIV(rCX)
	MW(rAX, rCX)
	POPR(rBX)
	POPR(rAX)
	POPR(rDX)			/* drive */

	LWI(RELOC, rSI)
	PUSHR(rSI)

_loadnext:
	PUSHR(rCX)
	PUSHR(rAX)
	CALL16(readsect16(SB))
	OR(rAX, rAX)
	JNE _loaderror
	POPR(rAX)
	CLR(rCX)
	ADDI(1, rAX)
	ADC(rCX, rBX)
	LW(_sectsize(SB), rCX)
	ADD(rCX, rSI)
	POPR(rCX)
	LOOP _loadnext
	CLI
	RET

_loaderror:
	LWI(ioerror(SB), rSI)
	CALL16(print16(SB))
	CALL16(halt(SB))

TEXT print16(SB), $0
	PUSHA
	CLR(rBX)
_printnext:
	LODSB
	ORB(rAL, rAL)
	JEQ _printret
	LBI(0x0E, rAH)
	BIOSCALL(0x10)
	JMP _printnext
_printret:
	POPA
	RET

/*
 * in:
 *	DL drive
 *	AX:BX lba32,
 *	0000:SI buffer
 */
TEXT readsect16(SB), $0
	PUSHA
	CLR(rCX)

	PUSHR(rCX)		/* qword lba */
	PUSHR(rCX)
	PUSHR(rBX)
	PUSHR(rAX)

	PUSHR(rCX)		/* dword buffer */
	PUSHR(rSI)

	INC(rCX)
	PUSHR(rCX)		/* word # of sectors */

	PUSHI(0x0010)		/* byte reserved, byte packet size */

	MW(rSP, rSI)
	LWI(0x4200, rAX)
	BIOSCALL(0x13)
	JCC _readok
	ADDI(0x10, rSP)
	POPA
	CLR(rAX)
	DEC(rAX)
	RET
_readok:
	ADDI(0x10, rSP)
	POPA
	CLR(rAX)
	RET

TEXT bootnamelen(SB), $0
	WORD $8
TEXT bootname(SB), $0
	BYTE $'9'; BYTE $'B'; BYTE $'O'; BYTE $'O';
	BYTE $'T'; BYTE $'F'; BYTE $'A'; BYTE $'T';
	BYTE $0

TEXT ioerror(SB), $0
	BYTE $'i'; BYTE $'/'; BYTE $'o'; BYTE $'-';
	BYTE $'e'; BYTE $'r'; BYTE $'r'; BYTE $0

TEXT hello(SB), $0
	BYTE $'p'; BYTE $'b'; BYTE $'s'; BYTE $'\r';
	BYTE $'\n'; BYTE $0
