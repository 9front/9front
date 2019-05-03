#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mach.h>

typedef struct Opcode Opcode;
struct Opcode
{
	char	*p;
	char	*o;
	char	*a;
};

typedef struct	Instr	Instr;
struct	Instr
{
	Opcode	*op;
	Map	*map;
	uvlong	addr;
	ulong	w;

	char	*curr;		/* fill point in buffer */
	char	*end;		/* end of buffer */
};

static	void	format(char*, Instr*, char*);
static	char	FRAMENAME[] = ".frame";

/*
 * Arm64-specific debugger interface
 */
static	char*	arm64excep(Map*, Rgetter);
static	int	arm64foll(Map*, uvlong, Rgetter, uvlong*);
static	int	arm64inst(Map*, uvlong, char, char*, int);
static	int	arm64das(Map*, uvlong, char*, int);
static	int	arm64instlen(Map*, uvlong);

/*
 *	Debugger interface
 */
Machdata arm64mach =
{
	{0x00, 0x00, 0x20, 0xD4},	/* break point 0xD4200000 */
	4,		/* break point size */
	leswab,		/* short to local byte order */
	leswal,		/* long to local byte order */
	leswav,		/* long to local byte order */
	risctrace,	/* C traceback */
	riscframe,	/* Frame finder */
	arm64excep,	/* print exception */
	0,		/* breakpoint fixup */
	0,		/* single precision float printer */
	0,		/* double precision float printer */
	arm64foll,	/* following addresses */
	arm64inst,	/* print instruction */
	arm64das,	/* dissembler */
	arm64instlen,	/* instruction size */
};

static Opcode opcodes[] =
{
	"0AA10000AAAAAAAAAAAAAAAAAAAddddd",	"ADR",		"$%A,R%d",
	"1PP10000PPPPPPPPPPPPPPPPPPPddddd",	"ADRP",		"$%P,R%d",
	"00011000lllllllllllllllllllddddd",	"MOVWU",	"%l,R%d",
	"01011000LLLLLLLLLLLLLLLLLLLddddd",	"MOV",		"%L,R%d",
	"10011000lllllllllllllllllllddddd",	"MOVW",		"%l,R%d",
	"11011000lllllllllllllllllllddddd",	"PRFM",		"%l,$%d",
	"1111100100uuuuuuuuuuuu11111ddddd",	"MOV",		"R%d,%u(SP)",
	"1111100100uuuuuuuuuuuunnnnnddddd",	"MOV",		"R%d,%u(R%n)",
	"WW11100100uuuuuuuuuuuu11111ddddd",	"MOV%WU",	"R%d,%u(SP)",
	"WW11100100uuuuuuuuuuuunnnnnddddd",	"MOV%WU",	"R%d,%u(R%n)",
	"1111100101uuuuuuuuuuuu11111ddddd",	"MOV",		"%u(SP),R%d",
	"1111100101uuuuuuuuuuuunnnnnddddd",	"MOV",		"%u(R%n),R%d",
	"WW11100101uuuuuuuuuuuu11111ddddd",	"MOV%WU",	"%u(SP),R%d",
	"WW11100101uuuuuuuuuuuunnnnnddddd",	"MOV%WU",	"%u(R%n),R%d",
	"WW11100110uuuuuuuuuuuu11111ddddd",	"MOV%W",	"%u(SP),R%d",
	"WW11100110uuuuuuuuuuuunnnnnddddd",	"MOV%W",	"%u(R%n),R%d",
	"11111000000ooooooooo0011111ddddd",	"MOV",		"R%d,%o(SP)",
	"11111000000ooooooooo00nnnnnddddd",	"MOV",		"R%d,%o(R%n)",
	"WW111000000ooooooooo0011111ddddd",	"MOV%W",	"R%d,%o(SP)",
	"WW111000000ooooooooo00nnnnnddddd",	"MOV%W",	"R%d,%o(R%n)",
	"11111000010ooooooooo0011111ddddd",	"MOV",		"%o(SP),R%d",
	"11111000010ooooooooo00nnnnnddddd",	"MOV",		"%o(R%n),R%d",
	"WW111000010ooooooooo0011111ddddd",	"MOV%WU",	"%o(SP),R%d",
	"WW111000010ooooooooo00nnnnnddddd",	"MOV%WU",	"%o(R%n),R%d",
	"WW111000100ooooooooo0011111ddddd",	"MOV%W",	"%o(SP),R%d",
	"WW111000100ooooooooo00nnnnnddddd",	"MOV%W",	"%o(R%n),R%d",
	"11111000000ooooooooo0111111ddddd",	"MOV",		"R%d,(SP)%o!",
	"WW111000000ooooooooo0111111ddddd",	"MOV%WU",	"R%d,(SP)%o!",
	"WW111000000ooooooooo01nnnnnddddd",	"MOV%WU",	"R%d,(R%n)%o!",
	"11111000000ooooooooo1111111ddddd",	"MOV",		"R%d,%o(SP)!",
	"WW111000000ooooooooo1111111ddddd",	"MOV%WU",	"R%d,%o(SP)!",
	"WW111000000ooooooooo11nnnnnddddd",	"MOV%WU",	"R%d,%o(R%n)!",
	"11111000010ooooooooo0111111ddddd",	"MOV",		"(SP)%o!,R%d",
	"11111000010ooooooooo01nnnnnddddd",	"MOV",		"(R%n)%o!,R%d",
	"WW111000010ooooooooo0111111ddddd",	"MOV%WU",	"(SP)%o!,R%d",
	"WW111000010ooooooooo01nnnnnddddd",	"MOV%WU",	"(R%n)%o!,R%d",
	"WW111000100ooooooooo0111111ddddd",	"MOV%W",	"(SP)%o!,R%d",
	"WW111000100ooooooooo01nnnnnddddd",	"MOV%W",	"(R%n)%o!,R%d",
	"11111000010ooooooooo1111111ddddd",	"MOV",		"%o(SP)!,R%d",
	"11111000010ooooooooo11nnnnnddddd",	"MOV",		"%o(R%n)!,R%d",
	"WW111000010ooooooooo1111111ddddd",	"MOV%WU",	"%o(SP)!,R%d",
	"WW111000010ooooooooo11nnnnnddddd",	"MOV%WU",	"%o(R%n)!,R%d",
	"WW111000100ooooooooo1111111ddddd",	"MOV%W",	"%o(SP)!,R%d",
	"WW111000100ooooooooo11nnnnnddddd",	"MOV%W",	"%o(R%n)!,R%d",
	"11111000001mmmmmeeei10nnnnnddddd",	"MOV",		"R%d,(R%n)(R%m%e)",
	"11111000111mmmmmeeei10nnnnnddddd",	"MOV",		"(R%n)(R%m%e),R%d",
	"WW111000001mmmmmeeei10nnnnnddddd",	"MOV%W",	"R%d,(R%n)(R%m%e)",
	"WW111000011mmmmmeeei10nnnnnddddd",	"MOV%WU",	"(R%n)(R%m%e),R%d",
	"WW111000101mmmmmeeei10nnnnnddddd",	"MOV%W",	"(R%n)(R%m%e),R%d",
	"WW111000111mmmmmeeei10nnnnnddddd",	"MOV%WW",	"(R%n)(R%m%e),R%d",
	"W00100101ssKKKKKKKKKKKKKKKKddddd",	"MOVN%W",	"$%K,R%d",
	"W10100101ssKKKKKKKKKKKKKKKKddddd",	"MOVZ%W",	"$%K,R%d",
	"W11100101ssKKKKKKKKKKKKKKKKddddd",	"MOVK%W",	"$%K,R%d",
	"W0010001--00000000000011111ddddd",	"MOV%W",	"SP,R%d",
	"W0010001--000000000000nnnnn11111",	"MOV%W",	"R%n,SP",
	"W0010001ssIIIIIIIIIIII1111111111",	"ADD%W",	"$%I,SP,SP",
	"W0010001ssIIIIIIIIIIII11111ddddd",	"ADD%W",	"$%I,SP,R%d",
	"W0010001ssIIIIIIIIIIIInnnnn11111",	"ADD%W",	"$%I,R%n,SP",
	"W0110001ssIIIIIIIIIIII1111111111",	"ADDS%W",	"$%I,SP,SP",
	"W0110001ssIIIIIIIIIIII11111ddddd",	"ADDS%W",	"$%I,SP,R%d",
	"W0110001ssIIIIIIIIIIIInnnnn11111",	"ADDS%W",	"$%I,R%n,SP",
	"W1010001ssIIIIIIIIIIII1111111111",	"SUB%W",	"$%I,SP,SP",
	"W1010001ssIIIIIIIIIIII11111ddddd",	"SUB%W",	"$%I,SP,R%d",
	"W1010001ssIIIIIIIIIIIInnnnn11111",	"SUB%W",	"$%I,R%n,SP",
	"W1110001ssIIIIIIIIIIII1111111111",	"CMP%W",	"$%I,SP",
	"W1110001ssIIIIIIIIIIIInnnnn11111",	"CMP%W",	"$%I,R%n",
	"W1110001ssIIIIIIIIIIII11111ddddd",	"SUBS%W",	"$%I,SP,R%d",
	"W0010001ssIIIIIIIIIIIInnnnnddddd",	"ADD%W",	"$%I,R%n,R%d",
	"W0110001ssIIIIIIIIIIIInnnnnddddd",	"ADDS%W",	"$%I,R%n,R%d",
	"W1010001ssIIIIIIIIIIIInnnnnddddd",	"SUB%W",	"$%I,R%n,R%d",
	"W1110001ssIIIIIIIIIIIInnnnnddddd",	"SUBS%W",	"$%I,R%n,R%d",
	"W00100100MMMMMMMMMMMMMnnnnn11111",	"AND%W",	"$%M,R%n,SP",
	"W01100100MMMMMMMMMMMMMnnnnn11111",	"ORR%W",	"$%M,R%n,SP",
	"W10100100MMMMMMMMMMMMMnnnnn11111",	"EOR%W",	"$%M,R%n,SP",
	"W11100100MMMMMMMMMMMMMnnnnn11111",	"ANDS%W",	"$%M,R%n,SP",
	"W00100100MMMMMMMMMMMMMnnnnnddddd",	"AND%W",	"$%M,R%n,R%d",
	"W01100100MMMMMMMMMMMMMnnnnnddddd",	"ORR%W",	"$%M,R%n,R%d",
	"W10100100MMMMMMMMMMMMMnnnnnddddd",	"EOR%W",	"$%M,R%n,R%d",
	"W11100100MMMMMMMMMMMMMnnnnnddddd",	"ANDS%W",	"$%M,R%n,R%d",
	"1001001101000000011111nnnnnddddd",	"SXTW",		"R%n,R%d",
	"0101001100iiiiii011111nnnnnddddd",	"LSRW",		"$%i,R%n,R%d",
	"1101001101iiiiii111111nnnnnddddd",	"LSR",		"$%i,R%n,R%d",
	"W00100110-iiiiiijjjjjjnnnnnddddd",	"SBFM%W",	"$%i,$%j,R%n,R%d",
	"W01100110-iiiiiijjjjjjnnnnnddddd",	"BFM%W",	"$%i,$%j,R%n,R%d",
	"W10100110-iiiiiijjjjjjnnnnnddddd",	"UBFM%W",	"$%i,$%j,R%n,R%d",
	"W1011010000mmmmm00000011111ddddd",	"NGC%W",	"R%m,R%d",
	"W1111010000mmmmm00000011111ddddd",	"NGCS%W",	"R%m,R%d",
	"W0011010000mmmmm000000nnnnnddddd",	"ADC%W",	"R%m,R%n,R%d",
	"W0111010000mmmmm000000nnnnnddddd",	"ADCS%W",	"R%m,R%n,R%d",
	"W1011010000mmmmm000000nnnnnddddd",	"SBC%W",	"R%m,R%n,R%d",
	"W1111010000mmmmm000000nnnnnddddd",	"SBCS%W",	"R%m,R%n,R%d",
	"W0101011ss0mmmmmiiiiiinnnnn11111",	"CMN%W",	"R%m%s,R%n",
	"W1101011ss0mmmmmiiiiiinnnnn11111",	"CMP%W",	"R%m%s,R%n",
	"W1001011ss0mmmmmiiiiii11111ddddd",	"NEG%W",	"R%m%s,R%d",
	"W1101011ss0mmmmmiiiiii11111ddddd",	"NEGS%W",	"R%m%s,R%d",
	"W0001011ss0mmmmmiiiiiinnnnnddddd",	"ADD%W",	"R%m%s,R%n,R%d",
	"W0101011ss0mmmmmiiiiiinnnnnddddd",	"ADDS%W",	"R%m%s,R%n,R%d",
	"W1001011ss0mmmmmiiiiiinnnnnddddd",	"SUB%W",	"R%m%s,R%n,R%d",
	"W1101011ss0mmmmmiiiiiinnnnnddddd",	"SUBS%W",	"R%m%s,R%n,R%d",
	"W0001011001mmmmmeeeiii1111111111",	"ADD%W",	"R%m%e,SP,SP",
	"W0001011001mmmmmeeeiii11111ddddd",	"ADD%W",	"R%m%e,SP,R%d",
	"W0001011001mmmmmeeeiiinnnnn11111",	"ADD%W",	"R%m%e,R%n,SP",
	"W0101011001mmmmmeeeiii1111111111",	"ADDS%W",	"R%m%e,SP,SP",
	"W0101011001mmmmmeeeiii11111ddddd",	"ADDS%W",	"R%m%e,SP,R%d",
	"W0101011001mmmmmeeeiiinnnnn11111",	"ADDS%W",	"R%m%e,R%n,SP",
	"W1001011001mmmmmeeeiii1111111111",	"SUB%W",	"R%m%e,SP,SP",
	"W1001011001mmmmmeeeiii11111ddddd",	"SUB%W",	"R%m%e,SP,R%d",
	"W1001011001mmmmmeeeiiinnnnn11111",	"SUB%W",	"R%m%e,R%n,SP",
	"W1101011001mmmmmeeeiii1111111111",	"SUBS%W",	"R%m%e,SP,SP",
	"W1101011001mmmmmeeeiii11111ddddd",	"SUBS%W",	"R%m%e,SP,R%d",
	"W1101011001mmmmmeeeiiinnnnn11111",	"SUBS%W",	"R%m%e,R%n,SP",
	"W0001011001mmmmmeeeiiinnnnnddddd",	"ADD%W",	"R%m%e,R%n,R%d",
	"W0101011001mmmmmeeeiiinnnnnddddd",	"ADDS%W",	"R%m%e,R%n,R%d",
	"W1001011001mmmmmeeeiiinnnnnddddd",	"SUB%W",	"R%m%e,R%n,R%d",
	"W1101011001mmmmmeeeiiinnnnnddddd",	"SUBS%W",	"R%m%e,R%n,R%d",
	"W0101010000mmmmm-0000011111ddddd",	"MOV%W",	"R%m,R%d",
	"W0101010ss1mmmmmiiiiii11111ddddd",	"NVM%W",	"R%m%s,R%d",
	"W1101010ss0mmmmmiiiiiinnnnn11111",	"TST%W",	"R%m%s,R%n",
	"W0001010ss0mmmmmiiiiiinnnnnddddd",	"AND%W",	"R%m%s,R%n,R%d",
	"W1101010ss0mmmmmiiiiiinnnnnddddd",	"ANDS%W",	"R%m%s,R%n,R%d",
	"W0001010ss1mmmmmiiiiiinnnnnddddd",	"BIC%W",	"R%m%s,R%n,R%d",
	"W1101010ss1mmmmmiiiiiinnnnnddddd",	"BICS%W",	"R%m%s,R%n,R%d",
	"W1001010ss0mmmmmiiiiiinnnnnddddd",	"EOR%W",	"R%m%s,R%n,R%d",
	"W1001010ss1mmmmmiiiiiinnnnnddddd",	"EON%W",	"R%m%s,R%n,R%d",
	"W0101010ss0mmmmmiiiiiinnnnnddddd",	"ORR%W",	"R%m%s,R%n,R%d",
	"W0101010ss1mmmmmiiiiiinnnnnddddd",	"ORN%W",	"R%m%s,R%n,R%d",
	"W0011010110mmmmm001000nnnnnddddd",	"LSL%W",	"R%m,R%n,R%d",
	"W0011010110mmmmm001001nnnnnddddd",	"LSR%W",	"R%m,R%n,R%d",
	"W0011010110mmmmm001010nnnnnddddd",	"ASR%W",	"R%m,R%n,R%d",
	"W0011010110mmmmm001011nnnnnddddd",	"ROR%W",	"R%m,R%n,R%d",
	"W0011010110mmmmm000010nnnnnddddd",	"UDIV%W",	"R%m,R%n,R%d",
	"W0011010110mmmmm000011nnnnnddddd",	"SDIV%W",	"R%m,R%n,R%d",
	"W0011011000mmmmm011111nnnnnddddd",	"MUL%W",	"R%m,R%n,R%d",
	"W0011011000mmmmm111111nnnnnddddd",	"MNEG%W",	"R%m,R%n,R%d",
	"W0011011000mmmmm0aaaaannnnnddddd",	"MADD%W",	"R%m,R%n,R%a,R%d",
	"W0011011000mmmmm1aaaaannnnnddddd",	"MSUB%W",	"R%m,R%n,R%a,R%d",
	"10011011001mmmmm011111nnnnnddddd",	"SMULL",	"R%m,R%n,R%d",
	"10011011001mmmmm111111nnnnnddddd",	"SMNEGL",	"R%m,R%n,R%d",
	"10011011001mmmmm0aaaaannnnnddddd",	"SMADDL",	"R%m,R%n,R%a,R%d",
	"10011011001mmmmm1aaaaannnnnddddd",	"SMSUBL",	"R%m,R%n,R%a,R%d",
	"10011011101mmmmm011111nnnnnddddd",	"UMULL",	"R%m,R%n,R%d",
	"10011011101mmmmm111111nnnnnddddd",	"UMNEGL",	"R%m,R%n,R%d",
	"10011011101mmmmm0aaaaannnnnddddd",	"UMADDL",	"R%m,R%n,R%a,R%d",
	"10011011101mmmmm1aaaaannnnnddddd",	"UMSUBL",	"R%m,R%n,R%a,R%d",
	"W0110100TTTTTTTTTTTTTTTTTTTddddd",	"CBZ%W",	"R%d,%T",
	"W0110101TTTTTTTTTTTTTTTTTTTddddd",	"CBNZ%W",	"R%d,%T",
	"01010100TTTTTTTTTTTTTTTTTTT0CCCC",	"B%C",		"%T",
	"000101TTTTTTTTTTTTTTTTTTTTTTTTTT",	"B",		"%T",
	"100101TTTTTTTTTTTTTTTTTTTTTTTTTT",	"BL",		"%T",
	"1101011000011111000000nnnnn00000",	"BR",		"R%n",
	"1101011000111111000000nnnnn00000",	"BLR",		"R%n",
	"11010110010111110000001111000000",	"RETURN",	nil,
	"1101011001011111000000nnnnn00000",	"RET",		"R%n",
	"11010110100111110000001111100000",	"ERET",		nil,
	"11010110101111110000001111100000",	"DRPS",		nil,
	"11010100000iiiiiiiiiiiiiiii00001",	"SVC",		"$%i",
	"11010100000iiiiiiiiiiiiiiii00010",	"HVC",		"$%i",
	"11010100000iiiiiiiiiiiiiiii00011",	"SMC",		"$%i",
	"11010100001iiiiiiiiiiiiiiii00000",	"BRK",		"$%i",
	"11010100010iiiiiiiiiiiiiiii00000",	"HLT",		"$%i",
	"11010100101iiiiiiiiiiiiiiii00001",	"DCPS1",	"$%i",
	"11010100101iiiiiiiiiiiiiiii00010",	"DCPS2",	"$%i",
	"11010100101iiiiiiiiiiiiiiii00011",	"DCPS3",	"$%i",
	"11010101000000110010000000011111",	"NOP",		nil,
	"11010101000000110010000000111111",	"YIELD",	nil,
	"11010101000000110010000001011111",	"WFE",		nil,
	"11010101000000110010000001111111",	"WFI",		nil,
	"11010101000000110010000010011111",	"SEV",		nil,
	"11010101000000110010000010111111",	"SEVL",		nil,
	"11010101000000110011xxxx01011111",	"CLREX",	"$%x",
	"11010101000000110011xxxx10011111",	"DSB",		"$%x",
	"11010101000000110011xxxx10111111",	"DMB",		"$%x",
	"11010101000000110011xxxx11011111",	"ISB",		"$%x",
	"1101010100001YYYYYYYYYYYYYY11111",	"SYS",		"%Y",
	"1101010100001YYYYYYYYYYYYYYddddd",	"SYS",		"R%d,%Y",
	"1101010100101YYYYYYYYYYYYYYddddd",	"SYSL",		"%Y,R%d",
	"11010101000000000100xxxx10111111",	"MSR",		"$%x,SP",
	"11010101000000110100xxxx11011111",	"MSR",		"$%x,DAIFSet",
	"11010101000000110100xxxx11111111",	"MSR",		"$%x,DAIFClr",
	"11010101000YYYYYYYYYYYYYYYYddddd",	"MSR",		"R%d,%Y",
	"11010101001YYYYYYYYYYYYYYYYddddd",	"MRS",		"%Y,R%d",
	"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",	"WORD",		"$%x",
};

#define	SYSARG5(op0,op1,Cn,Cm,op2)	((op0)<<19|(op1)<<16|(Cn)<<12|(Cm)<<8|(op2)<<5)

static ulong
smask(char *s, char c)
{
	ulong m;
	int i;

	m = 0;
	for(i=0; i<32 && *s != '\0'; i++, s++)
		m |= (*s == c)<<(31-i);
	return m;
}

static int
nbits(ulong v)
{
	int n = 0;
	while(v != 0){
		v &= v-1;
		n++;
	}
	return n;
}

static int
nones(ulong v)
{
	int n = 0;
	while(v & 1){
		v >>= 1;
		n++;
	}
	return n;
}

static int
nshift(ulong m)
{
	if(m == 0 || m == ~0UL)
		return 0;
	return nones(~m);
}

static ulong
unshift(ulong w, ulong m)
{
	int s = nshift(m);
	w >>= s, m >>= s;
	if((m+1 & m) != 0){	// 0bxxx0000yyyyyy -> 0byyyyyyxxx
		ulong b = (1UL<<nones(m))-1;
		return ((w & b) << nbits(m & ~b)) | unshift(w, m & ~b);
	}
	return w & m;
}

static long
sext(ulong u, int n)
{
	long l = (long)u;
	if(n > 0){
		l <<= sizeof(l)*8 - n;
		l >>= sizeof(l)*8 - n;
	}
	return l;
}

static char*
arm64excep(Map *, Rgetter)
{
//	uvlong c = (*rget)(map, "TYPE");
	return "???";
}

static int
decode(Map *map, uvlong pc, Instr *i)
{
	static ulong tab[2*nelem(opcodes)];
	static int once;
	ulong w, j;

	if(!once){
		Opcode *o;

		/* precalculate value/mask table */
		for(j=0, o=opcodes; j<nelem(tab); j+=2, o++){
			tab[j] = smask(o->p, '1');
			tab[j|1] = tab[j] | smask(o->p, '0');
		}

		once = 1;
	}

	if(get4(map, pc, &w) < 0) {
		werrstr("can't read instruction: %r");
		return -1;
	}
	i->addr = pc;
	i->map = map;
	i->w = w;

	for(j=0; j<nelem(tab); j+=2){
		if((w & tab[j|1]) == tab[j]){
			i->op = &opcodes[j/2];
			return 1;
		}
	}

	/* should not happen */
	return 0;
}

#pragma	varargck	argpos	bprint	2

static void
bprint(Instr *i, char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	i->curr = vseprint(i->curr, i->end, fmt, arg);
	va_end(arg);
}

static
char*	shtype[4] =
{
	"<<",	">>",	"->",	"@>"
};

static
char*	rextype[8] =
{
	"UB", "UH", "UW", "UX",
	"SB", "SH", "SW", "SX"
};

static
char*	scond[16] =
{
	"EQ", "NE", "HS", "LO",
	"MI", "PL", "VS", "VC",
	"HI", "LS", "GE", "LT",
	"GT", "LE",   "", "NV",
};

static uvlong
decodebitmask(int n, int s, int r)
{
	uvlong w;
	int e;

	if(n)
		n = 6;
	else {
		for(n = 5; n >= 1 && ((~s & 0x3F) & (1<<n)) == 0; n--)
			;
	}
	e = 1 << n;
	s &= e-1;
	r &= e-1;

	w = 1ULL << s;
	w |= w-1;

	if(r != 0){
		w = (w >> r) | (w << (e-r));
		if(e < 64)
			w &= (1ULL << e)-1;
	}
	while(e < 64){
		w |= w << e;
		e <<= 1;
	}
	return w;
}

static void
format(char *mnemonic, Instr *i, char *f)
{
	Symbol s;
	uvlong v;
	ulong w, u, m;

	if(mnemonic)
		format(0, i, mnemonic);
	if(f == 0)
		return;
	if(mnemonic)
		if(i->curr < i->end)
			*i->curr++ = '\t';
	for ( ; *f && i->curr < i->end; f++) {
		if(*f != '%') {
			*i->curr++ = *f;
			continue;
		}
		m = smask(i->op->p, *++f);
		u = unshift(i->w, m);
		switch (*f) {
		case 'C':	// Condition
			bprint(i, "%s", scond[u & 15]);
			break;

		case 'W':	// Width
			if(nbits(m) == 1) u += 2;
			u &= 3;
			if(u < 3)
				*i->curr++ = "BHW"[u];
			break;

		case 'd':	// Register Numbers
		case 'n':
		case 'a':
		case 'm':
			bprint(i, "%lud", u);
			break;

		case 's':	// Register shift
			w = unshift(i->w, smask(i->op->p, 'i'));
			if(w != 0)
				bprint(i, "%s%lud", shtype[u & 3], w);
			break;

		case 'e':	// Register extension
			u &= 7;
			bprint(i, ".%s", rextype[u]);
			w = unshift(i->w, smask(i->op->p, 'i'));
			if(w != 0 && u == 2+(i->w>>31))
				bprint(i, "<<%lud", w);
			break;

		case 'M':	// Bitmask
			v = decodebitmask((u>>12)&1, u&0x3F, (u>>6)&0x3F);
			if((i->w & (1<<31)) == 0)
				v &= 0xFFFFFFFF;
			bprint(i, "%llux", v);
			break;

		case 'I':	// Shifted Immediate (12 bit)
		case 'K':	// Shifted Immediate (16 bit)
			w = unshift(i->w, smask(i->op->p, 's'));
			if(u != 0 && w != 0)
				bprint(i, "(%lux<<%ld)", u, w*(*f == 'I' ? 12 : 16));
			else
				bprint(i, "%lud", u);
			break;

		case 'o':	// Signed byte offset
			bprint(i, "%ld", sext(u, nbits(m)));
			break;
		case 'u':	// Unsigned offset
			u <<= (i->w >> 30)&3;
			/* wet floor */
		case 'i':	// Decimal
		case 'j':
			bprint(i, "%lud", u);
			break;

		case 'x':	// Hex
			bprint(i, "%lux", u);
			break;

		case 'l':	// 32-bit Literal
			if(get4(i->map, i->addr + sext(u, nbits(m))*4, &w) < 0)
				goto Ptext;
			bprint(i, "$%lux", w);
			break;
		case 'L':	// 64-bit Literal
			if(get8(i->map, i->addr + sext(u, nbits(m))*4, &v) < 0)
				goto Ptext;
			bprint(i, "$%llux", v);
			break;
		case 'T':	// Text address (PC relative)
		Ptext:
			v = i->addr + sext(u, nbits(m))*4;
			if(findsym(v, CTEXT, &s)){
				bprint(i, "%s", s.name);
				if(v < s.value)
					bprint(i, "%llx", v - s.value);
				else if(v > s.value)
					bprint(i, "+%llx", v - s.value);
				bprint(i, "(SB)");
				break;
			}
			bprint(i, "%llux(SB)", v);
			break;
		case 'A':	// Data address (PC relative)
			v = i->addr + sext(u, nbits(m));
			goto Pdata;
		case 'P':	// Page address (PC relative)
			v = i->addr + ((vlong)sext(u, nbits(m)) << 12);
		Pdata:
			if(findsym(v, CANY, &s)){
				bprint(i, "%s", s.name);
				if(v < s.value)
					bprint(i, "%llx", v - s.value);
				else if(v > s.value)
					bprint(i, "+%llx", v - s.value);
				bprint(i, "(SB)");
				break;
			}
			bprint(i, "%llux(SB)", v);
			break;

		case 'Y':
			if(nbits(m) == 14){ // SYS/SYSL operands
				bprint(i, "%lud,%lud,%lud,%lud",
					(u>>(4+4+3))&7,	// op1
					(u>>(4+3))&15,	// CRn
					(u>>3)&15,	// CRm
					(u)&7);		// op2
				break;
			}
			/* see /sys/src/cmd/7c/7.out.h */
			switch(i->w & m){
			case SYSARG5(3,3,4,2,1): bprint(i, "DAIF"); break;
			case SYSARG5(3,3,4,2,0): bprint(i, "NZCV"); break;
			case SYSARG5(3,3,4,4,1): bprint(i, "FPSR"); break;
			case SYSARG5(3,3,4,4,0): bprint(i, "FPCR"); break;
			case SYSARG5(3,0,4,0,0): bprint(i, "SPSR_EL1"); break;
			case SYSARG5(3,0,4,0,1): bprint(i, "ELR_EL1"); break;
			case SYSARG5(3,4,4,0,0): bprint(i, "SPSR_EL2"); break;
			case SYSARG5(3,4,4,0,1): bprint(i, "ELR_EL2"); break;
			case SYSARG5(3,0,4,2,2): bprint(i, "CurrentEL"); break;
			case SYSARG5(3,0,4,1,0): bprint(i, "SP_EL0"); break;
			case SYSARG5(3,0,4,2,0): bprint(i, "SPSel"); break;
			default: bprint(i, "SPR(%lux)", i->w & m);
			}
			break;

		case '\0':
			*i->curr++ = '%';
			return;

		default:
			bprint(i, "%%%c", *f);
			break;
		}
	}
	*i->curr = 0;
}

static int
printins(Map *map, uvlong pc, char *buf, int n)
{
	Instr i[1];

	i->curr = buf;
	i->end = buf+n-1;
	if(decode(map, pc, i) < 0)
		return -1;
	format(i->op->o, i, i->op->a);
	return 4;
}

static int
arm64inst(Map *map, uvlong pc, char modifier, char *buf, int n)
{
	USED(modifier);
	return printins(map, pc, buf, n);
}

static int
arm64das(Map *map, uvlong pc, char *buf, int n)
{
	Instr i[1];

	i->curr = buf;
	i->end = buf+n;
	if(decode(map, pc, i) < 0)
		return -1;
	if(i->end-i->curr > 8)
		i->curr = _hexify(buf, i->w, 7);
	*i->curr = 0;
	return 4;
}

static int
arm64instlen(Map*, uvlong)
{
	return 4;
}

static uvlong
readreg(Instr *i, Rgetter rget, int rc)
{
	ulong m;
	uvlong v;
	char reg[4];
	snprint(reg, sizeof(reg), "R%lud", unshift(i->w, smask(i->op->p, rc)));
	v = (*rget)(i->map, reg);
	m = smask(i->op->p, 'W');
	if(m != 0 && unshift(i->w, m) == 0)
		v &= 0xFFFFFFFFULL;
	return v;
}

static int
passcond(Instr *i, Rgetter rget)
{
	uvlong psr;
	uchar n;
	uchar z;
	uchar c;
	uchar v;

	psr = (*rget)(i->map, "PSR");
	n = (psr >> 31) & 1;
	z = (psr >> 30) & 1;
	c = (psr >> 29) & 1;
	v = (psr >> 28) & 1;

	switch(unshift(i->w, smask(i->op->p, 'C'))) {
	default:
	case 0:		return z;
	case 1:		return !z;
	case 2:		return c;
	case 3:		return !c;
	case 4:		return n;
	case 5:		return !n;
	case 6:		return v;
	case 7:		return !v;
	case 8:		return c && !z;
	case 9:		return !c || z;
	case 10:	return n == v;
	case 11:	return n != v;
	case 12:	return !z && (n == v);
	case 13:	return z || (n != v);
	case 14:	return 1;
	case 15:	return 0;
	}
}

static uvlong
jumptarg(Instr *i)
{
	ulong m = smask(i->op->p, 'T');
	return i->addr + sext(unshift(i->w, m), m)*4;
}

static int
arm64foll(Map *map, uvlong pc, Rgetter rget, uvlong *foll)
{
	Instr i[1];
	char *o;

	if(decode(map, pc, i) < 0)
		return -1;

	o = i->op->o;
	if(strcmp(o, "ERET") == 0)
		return -1;

	if(strcmp(o, "RET") == 0 || strcmp(o, "BR") == 0 || strcmp(o, "BLR") == 0){
		foll[0] = readreg(i, rget, 'n');
		return 1;
	}
	if(strcmp(o, "B") == 0 || strcmp(o, "BL") == 0){
		foll[0] = jumptarg(i);
		return 1;
	}
	if(strcmp(o, "B%C") == 0){
		if(passcond(i, rget)){
			foll[0] = jumptarg(i);
			return 1;
		}
	}
	if(strcmp(o, "CBZ%W") == 0){
		if(readreg(i, rget, 'd') == 0){
			foll[0] = jumptarg(i);
			return 1;
		}
	}
	if(strcmp(o, "CBNZ%W") == 0){
		if(readreg(i, rget, 'd') != 0){
			foll[0] = jumptarg(i);
			return 1;
		}
	}

	foll[0] = i->addr+4;
	return 1;
}
