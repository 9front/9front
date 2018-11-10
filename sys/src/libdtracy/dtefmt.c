#include <u.h>
#include <libc.h>
#include <dtracy.h>

char *dtvarnames[DTNVARS] = {
	[DTV_ARG0] "arg0",
	[DTV_ARG1] "arg1",
	[DTV_ARG2] "arg2",
	[DTV_ARG3] "arg3",
	[DTV_ARG4] "arg4",
	[DTV_ARG5] "arg5",
	[DTV_ARG6] "arg6",
	[DTV_ARG7] "arg7",
	[DTV_ARG8] "arg8",
	[DTV_ARG9] "arg9",
	[DTV_PID] "pid",
	[DTV_TIME] "time",
	[DTV_PROBE] "probe",
	[DTV_MACHNO] "machno",
};

int
dtefmt(Fmt *f)
{
	u32int ins;
	u8int op, a, b, c;
	u64int x;
	static char *opcodes[] = {
		[DTE_ADD] "ADD",
		[DTE_SUB] "SUB",
		[DTE_MUL] "MUL",
		[DTE_UDIV] "UDIV",
		[DTE_UMOD] "UMOD",
		[DTE_SDIV] "SDIV",
		[DTE_SMOD] "SMOD",
		[DTE_AND] "AND",
		[DTE_OR] "OR",
		[DTE_XOR] "XOR",
		[DTE_XNOR] "XNOR",
		[DTE_LSL] "LSL",
		[DTE_LSR] "LSR",
		[DTE_ASR] "ASR",
		[DTE_SEQ] "SEQ",
		[DTE_SNE] "SNE",
		[DTE_SLT] "SLT",
		[DTE_SLE] "SLE",
		[DTE_LDI] "LDI",
		[DTE_XORI] "XORI",
		[DTE_BEQ] "BEQ",
		[DTE_BNE] "BNE",
		[DTE_BLT] "BLT",
		[DTE_BLE] "BLE",
		[DTE_LDV] "LDV",
		[DTE_RET] "RET",
		[DTE_ZXT] "ZXT",
		[DTE_SXT] "SXT",
	};
	
	ins = va_arg(f->args, u32int);
	op = ins >> 24;
	a = ins >> 16;
	b = ins >> 8;
	c = ins;
	switch(op){
	case DTE_ADD:
	case DTE_SUB:
	case DTE_MUL:
	case DTE_UDIV:
	case DTE_UMOD:
	case DTE_SDIV:
	case DTE_SMOD:
	case DTE_AND:
	case DTE_OR:
	case DTE_XOR:
	case DTE_XNOR:
	case DTE_LSL:
	case DTE_LSR:
	case DTE_ASR:
	case DTE_SEQ:
	case DTE_SNE:
	case DTE_SLT:
	case DTE_SLE:
		fmtprint(f, "%s R%d, R%d, R%d", opcodes[op], a, b, c);
		break;
	case DTE_LDI:
	case DTE_XORI:
		x = (s64int)ins << 40 >> 54 << (ins >> 8 & 63);
		fmtprint(f, "%s $%#llx, R%d", opcodes[op], x, c);
		break;
	case DTE_BEQ:
	case DTE_BNE:
	case DTE_BLT:
	case DTE_BLE:
		fmtprint(f, "%s R%d, R%d, +%d", opcodes[op], a, b, c);
		break;
	case DTE_LDV:
		if(a >= DTNVARS || dtvarnames[a] == nil)
			fmtprint(f, "%s V%d, R%d", opcodes[op], a, b);
		else
			fmtprint(f, "%s %s, R%d", opcodes[op], dtvarnames[a], b);
		break;
	case DTE_ZXT:
	case DTE_SXT:
		fmtprint(f, "%s R%d, $%d, R%d", opcodes[op], a, b, c);
		break;
	case DTE_RET:
		fmtprint(f, "RET R%d", a);
		break;
	default:
		fmtprint(f, "??? (%#.8ux)", op);
		break;
	}
	return 0;
}
